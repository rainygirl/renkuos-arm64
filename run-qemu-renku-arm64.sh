#!/bin/bash
#
# Boot RenkuOS arm64 in QEMU, on Apple Silicon (hvf) or under emulation.
#
# Usage:
#   ./run-qemu-renku-arm64.sh                boot the installed disk, full screen
#   ./run-qemu-renku-arm64.sh --install      boot the ISO with the disk attached,
#                                       to partition and install with it
#   ./run-qemu-renku-arm64.sh --headless     no window; QMP control socket only
#   ./run-qemu-renku-arm64.sh --tcg          force emulation over hvf
#   ./run-qemu-renku-arm64.sh --bridged      add a second NIC on the real LAN
#                                       (see "Bridged networking" below)
#   ./run-qemu-renku-arm64.sh --clipboard    sync the Mac's clipboard with
#                                       the guest's (see "Clipboard sync")
#   ./run-qemu-renku-arm64.sh --window       start in a window instead of full
#                                       screen (Cmd-F toggles either way);
#                                       --fullscreen is accepted, the default
#
# The disk (renku-arm64-vm.img, created empty on first run) persists between
# runs. The installer medium is renku-arm64.iso, built by
# build-renku-arm64-iso.sh.
#
# Settings this port needs -- not preferences, each one earned by someone
# hitting the failure:
#   - Disks must be attached over USB. QEMU's edk2-aarch64 firmware has no
#     SATA driver, so an AHCI disk is invisible to it and the machine drops
#     to the UEFI Shell with "map: No mapping found".
#   - The installer medium and the target disk need SEPARATE xhci
#     controllers. Four devices on one controller makes the guest's xhci
#     driver fail to address the keyboard and tablet ("unable to set
#     address: I/O error"), and input stops working entirely.
#   - Do not mark the installer medium readonly: the live system writes to
#     its own boot volume.
#   - A guest built with arm64-patch 0005 reads the PL031 clock; without it
#     the guest starts at 1970 and every TLS certificate fails. Shutting
#     down from inside the guest (Deskbar > Shut Down, or `shutdown`) ends
#     QEMU by itself through PSCI, which RenkuOS/Source has had since
#     024aeb68a7; an older tree stops at "safe to turn off" instead.
#   - RENKU_AUDIO=none swaps the CoreAudio backend for silence; with
#     CoreAudio, a recording app only gets samples if the terminal running
#     this has microphone permission in macOS System Settings.
#
# Bridged networking (--bridged):
#   The default NIC is QEMU usermode NAT (hostfwd tcp::2222-:22) -- the guest
#   is only reachable from this Mac, never from other machines on the LAN.
#   --bridged adds a SECOND NIC on vmnet-bridged, which puts the guest
#   directly on the real network with its own DHCP-assigned IP, reachable
#   from any machine on the LAN (sshd already runs at boot -- see AGENTS.md).
#   It is additive, not a replacement: the first NIC and port 2222 keep
#   working exactly as before.
#
#   vmnet-bridged needs the com.apple.vm.networking entitlement to run
#   unprivileged, which Apple does not hand out to ordinary signed/unsigned
#   binaries (Homebrew's qemu doesn't have it) -- confirmed against QEMU's
#   own issue tracker and Apple's developer forums, not guessed. Without it,
#   vmnet-bridged needs root. --bridged re-execs this script's qemu call
#   through sudo for that reason; it is not needed and not used without the
#   flag. The bridge interface is auto-detected from the default route
#   (`route -n get default`); override with RENKU_NET_IFACE=en1 etc. if that
#   picks the wrong one.
#
#   The guest's bridged IP is DHCP-assigned and not predictable from the
#   host side -- check it in the guest's own Network preferences after boot.
#
# Full screen and the notch:
#   macOS keeps a native full-screen window below a MacBook's camera notch,
#   which left a black strip across the top of the guest. The cocoa display
#   is therefore run with tools/qemu-notch/libqemu-notch.dylib injected
#   (built on first use). Full screen stays macOS's own (a Space of its own,
#   Cmd-Tab works); the dylib moves QEMU's view into a window over the whole
#   screen inside that Space, the strip beside the notch included. The guest
#   is offered the Mac's main display resolution (2560x1664 on a 13" MacBook
#   Air) and its real size, and R Retina picks the UI scale from that;
#   RENKU_RES=WxH overrides the resolution, RENKU_GUEST_SCALE multiplies it,
#   RENKU_NOTCH=0 leaves QEMU's own full screen alone.
#
# Clipboard sync (--clipboard):
#   QEMU's cocoa display has no clipboard support at all (no suboption for
#   it, unlike its GTK/SPICE backends) -- confirmed against `qemu -help`'s
#   own -display listing -- and Haiku has no virtio-console/virtio-serial
#   driver either, so there is no hypervisor-channel shortcut. --clipboard
#   polls instead, over the SSH port that's already open (2222): a
#   background loop pushes `pbpaste` to the guest and pulls the guest's
#   clipboard back with clipboard/clipboard-cli (see clipboard/build.sh)
#   whenever either side changes, once a second. Text only.
#
#   This needs the guest's authorized_keys to already trust a key on this
#   Mac -- the same requirement the SSH section in README.md already has,
#   nothing new. Default identity: ~/.ssh/id_ed25519; override with
#   RENKU_SSH_KEY=/path/to/key if that's not the one the guest trusts.
#
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DISK="${RENKU_DISK:-$DIR/renku-arm64-vm.img}"
ISO="${RENKU_ISO:-$DIR/renku-arm64.iso}"

INSTALL=0; HEADLESS=0; FORCE_TCG=0; BRIDGED=0; CLIPBOARD=0; FULLSCREEN=1
for a in "$@"; do
	case "$a" in
		--install)   INSTALL=1 ;;
		--headless)  HEADLESS=1 ;;
		--tcg)       FORCE_TCG=1 ;;
		--bridged)   BRIDGED=1 ;;
		--clipboard) CLIPBOARD=1 ;;
		--fullscreen) FULLSCREEN=1 ;;
		--window)    FULLSCREEN=0 ;;
		*) echo "unknown option: $a" >&2; exit 1 ;;
	esac
done

# A --bridged run execs qemu through sudo, so the QMP socket and serial log
# it creates end up root-owned; a later plain run, as a normal user, can't
# reuse or overwrite them (/tmp's sticky bit blocks removing the socket,
# and the log file's permissions block reopening it for write). Giving
# --bridged its own filenames avoids that collision entirely instead of
# trying to clean up across privilege levels.
QMP="/tmp/renku-arm64.qmp"
SERIAL="$DIR/serial.log"
NET_ARGS=(-netdev user,id=n0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n0)
RUNNER=()
if [ "$BRIDGED" = "1" ]; then
	QMP="/tmp/renku-arm64-bridged.qmp"
	SERIAL="$DIR/serial-bridged.log"
	IFACE="${RENKU_NET_IFACE:-$(route -n get default 2>/dev/null | awk '/interface:/{print $2}')}"
	[ -n "$IFACE" ] || { echo "could not auto-detect the default network interface -- set RENKU_NET_IFACE=en0 (or similar) and retry" >&2; exit 1; }
	NET_ARGS+=(-netdev vmnet-bridged,id=n1,ifname="$IFACE" -device virtio-net-pci,netdev=n1)
	echo "bridged:  $IFACE (needs sudo -- vmnet-bridged has no entitlement to run unprivileged)"
	if [ "$(id -u)" != "0" ]; then
		RUNNER=(sudo)
	fi
fi

if pgrep -f "qemu-system-aarch64.*$DISK" >/dev/null 2>&1; then
	echo "already running against $DISK -- two QEMU instances on the same disk will corrupt it" >&2
	exit 1
fi

if [ ! -f "$DISK" ]; then
	[ "$INSTALL" = "1" ] || echo "no disk yet at $DISK -- creating an empty one, but it has no OS on it; run with --install first" >&2
	qemu-img create -f raw "$DISK" 8G
fi

FW=""
for c in /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
         /usr/local/share/qemu/edk2-aarch64-code.fd; do
	[ -f "$c" ] && { FW="$c"; break; }
done
[ -n "$FW" ] || { echo "no aarch64 EFI firmware found (brew install qemu)" >&2; exit 1; }

ACCEL=tcg; CPU=max
if [ "$FORCE_TCG" = "0" ] && [ "$(uname -m)" = "arm64" ] \
   && qemu-system-aarch64 -accel help 2>/dev/null | grep -q hvf; then
	ACCEL=hvf; CPU=host
fi

MEDIA=()
if [ "$INSTALL" = "1" ]; then
	if [ ! -f "$ISO" ]; then
		echo "installer medium not found: $ISO -- building it now (this can take a while)" >&2
		"$DIR/build-renku-arm64-iso.sh"
	fi
	# ISO first (boots), target disk on its own controller.
	MEDIA=(-drive file="$ISO",if=none,id=drv0,format=raw
	       -device usb-storage,bus=usb.0,drive=drv0
	       -device qemu-xhci,id=usb2
	       -drive file="$DISK",if=none,id=drv1,format=raw
	       -device usb-storage,bus=usb2.0,drive=drv1)
	echo "medium:   $ISO"
else
	MEDIA=(-drive file="$DISK",if=none,id=drv0,format=raw
	       -device usb-storage,bus=usb.0,drive=drv0)
fi

# The guest's screen: the Mac's main display, in pixels, unless overridden.
RES="${RENKU_RES:-$(system_profiler SPDisplaysDataType 2>/dev/null \
	| awk '/Resolution:/ { print $2 "x" $4; exit }')}"
case "$RES" in
	[0-9]*x[0-9]*) ;;
	*) RES=2560x1600 ;;
esac
GPU=(-device virtio-gpu-pci,xres="${RES%x*}",yres="${RES#*x}")
# What full screen offers the guest (with the notch dylib): the panel times
# RENKU_GUEST_SCALE each way, 1 by default -- the panel's own 2560x1664.
# RenkuOS chooses its UI scale from the pixel density (R Retina, UI scale
# Automatic: 2x at 224 dpi). A scale of 2 needs a virtio_gpu frame buffer
# larger than R Retina's 5120x2880.
GUEST_SCALE="${RENKU_GUEST_SCALE:-1}"
GUEST_FULL="$(( ${RES%x*} * GUEST_SCALE ))x$(( ${RES#*x} * GUEST_SCALE ))"

ENV_ARGS=()
if [ "$HEADLESS" = "1" ]; then
	DISPLAY_ARGS=(-display none "${GPU[@]}" -serial file:"$SERIAL")
else
	# zoom-interpolation: the guest is scaled into the window's backing store
	# (2940x1912 on a 2560x1664 panel set to "looks like 1470x956"), and
	# QEMU's default nearest-neighbour scaling dithers; smooth it instead
	COCOA=cocoa,zoom-to-fit=on,zoom-interpolation=on
	[ "$FULLSCREEN" = "1" ] && COCOA="$COCOA,full-screen=on"
	DISPLAY_ARGS=(-display "$COCOA" "${GPU[@]}" -serial file:"$SERIAL")
	if [ "${RENKU_NOTCH:-1}" = "1" ]; then
		NOTCH="$DIR/tools/qemu-notch/libqemu-notch.dylib"
		[ -f "$NOTCH" ] || "$DIR/tools/qemu-notch/build.sh" >/dev/null \
			|| echo "could not build $NOTCH -- full screen will stop below the notch" >&2
		[ -f "$NOTCH" ] && ENV_ARGS=(env DYLD_INSERT_LIBRARIES="$NOTCH" QEMU_NOTCH_SIZE="$GUEST_FULL")
		# Hold the guest (-S) until full screen has offered the panel's size:
		# app_server picks the UI scale once, at boot, and a guest that booted
		# while QEMU was still a small window kept the largest scale. The
		# dylib sends "cont" over QMP once in full screen (or after 30 s).
		if [ -f "$NOTCH" ] && [ "$FULLSCREEN" = "1" ]; then
			ENV_ARGS+=(QEMU_NOTCH_QMP="$QMP")
			HOLD_ARGS=(-S)
		fi
	fi
fi

rm -f "$QMP"
echo "disk:     $DISK"
echo "accel:    $ACCEL   cpu: $CPU"
echo "serial:   $SERIAL"
echo "qmp:      $QMP"
echo "screen:   $RES"

QEMU_BIN="$(command -v qemu-system-aarch64)"
[ -n "$QEMU_BIN" ] || { echo "qemu-system-aarch64 not found in PATH" >&2; exit 1; }

qemu_args_for() {
	# $1 = vCPU count for this attempt
	printf '%s\n' \
		-M virt -cpu "$CPU" -accel "$ACCEL" -smp "$1" -m 4096 \
		-bios "$FW" \
		-device qemu-xhci,id=usb
	printf '%s\n' "${MEDIA[@]}"
	printf '%s\n' \
		-device usb-kbd,bus=usb.0 \
		-device usb-tablet,bus=usb.0
	printf '%s\n' "${NET_ARGS[@]}"
	printf '%s\n' \
		-audiodev "${RENKU_AUDIO:-coreaudio}",id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0 \
		-qmp unix:"$QMP",server,nowait
	printf '%s\n' "${DISPLAY_ARGS[@]}"
	if [ -n "${HOLD_ARGS+set}" ] && [ "${#HOLD_ARGS[@]}" -gt 0 ]; then
		printf '%s\n' "${HOLD_ARGS[@]}"
	fi
	return 0
}

# Launches qemu in the background (not exec'd -- the boot watchdog below
# needs this script's own process alive to watch serial.log and, if it
# must, kill and retry) and prints its PID.
LAST_LAUNCH_PID=""
launch_qemu() {
	local args=() line
	while IFS= read -r line; do args+=("$line"); done < <(qemu_args_for "$1")
	rm -f "$SERIAL"
	"${RUNNER[@]+"${RUNNER[@]}"}" "${ENV_ARGS[@]+"${ENV_ARGS[@]}"}" "$QEMU_BIN" "${args[@]}" &
	LAST_LAUNCH_PID=$!
}

# A spurious-interrupt storm on the virtio network device's PCI IRQ is a
# known QEMU/hvf issue on this Mac (not this port's code: reproduces across
# every disk image, ISO, accelerator and device combination tried -- see
# AGENTS.md). Its serial-console signature is "Last message repeated" lines
# arriving back to back without the kernel boot log ever moving past them.
# Lower vCPU counts make the race far less likely to ever trigger, at the
# cost of a slower boot, so a hung attempt is retried once at -smp 2 and
# once more at -smp 1 before giving up. Returns 0 (hang) or 1 (looks fine,
# or the guest exited on its own -- a real shutdown, not this bug).
boot_watchdog() {
	local budget="$1" pid="$2"
	local elapsed=0 consecutive_repeats=0 stalled_checks=0 last_size=-1 cur_size last_line
	while [ "$elapsed" -lt "$budget" ]; do
		if ! kill -0 "$pid" 2>/dev/null; then
			return 1
		fi
		if [ -f "$SERIAL" ]; then
			cur_size="$(wc -c <"$SERIAL" 2>/dev/null || echo 0)"
			if [ "$cur_size" = "$last_size" ]; then
				stalled_checks=$((stalled_checks + 1))
			else
				stalled_checks=0
			fi
			last_size="$cur_size"
			last_line="$(tail -n 1 "$SERIAL" 2>/dev/null || true)"
			case "$last_line" in
				"Last message repeated"*) consecutive_repeats=$((consecutive_repeats + 1)) ;;
				*) consecutive_repeats=0 ;;
			esac
			if [ "$consecutive_repeats" -ge 4 ]; then
				return 0
			fi
			# Prolonged total silence further into boot than firmware/splash
			# would normally still be running is the same hang without the
			# storm (seen under some vCPU/GIC combinations) -- 20s of an
			# unmoving byte count, itself not checked before 20s in, so a
			# legitimately slow early stage is never mistaken for it.
			if [ "$elapsed" -ge 20 ] && [ "$stalled_checks" -ge 20 ]; then
				return 0
			fi
		fi
		sleep 1
		elapsed=$((elapsed + 1))
	done
	return 1
}

QEMU_PID=""
for SMP_TRY in 4 2 1; do
	[ "$SMP_TRY" = "4" ] || echo "boot watchdog: retrying at -smp $SMP_TRY after a hung attempt (see AGENTS.md's QEMU gotchas)" >&2
	launch_qemu "$SMP_TRY"
	TRY_PID="$LAST_LAUNCH_PID"
	BUDGET=40
	[ "$SMP_TRY" = "2" ] && BUDGET=60
	[ "$SMP_TRY" = "1" ] && BUDGET=90
	if boot_watchdog "$BUDGET" "$TRY_PID"; then
		echo "boot watchdog: hung at -smp $SMP_TRY, killing and falling back" >&2
		kill -9 "$TRY_PID" 2>/dev/null || true
		wait "$TRY_PID" 2>/dev/null || true
		continue
	fi
	QEMU_PID="$TRY_PID"
	break
done

if [ -z "$QEMU_PID" ]; then
	echo "RenkuOS did not boot cleanly even at -smp 1 -- this is the host-side QEMU/hvf issue documented in AGENTS.md, not something a retry here can work around" >&2
	exit 1
fi

# --clipboard needs to clean up its background sync loop once qemu exits;
# every other mode just waits for the PID the watchdog above settled on,
# propagating its exit status like `exec` used to.
if [ "$CLIPBOARD" = "0" ]; then
	wait "$QEMU_PID"
	exit $?
fi

clipboard_sync_loop() {
	local key="${RENKU_SSH_KEY:-$HOME/.ssh/id_ed25519}"
	local ssh_opts=(-i "$key" -o IdentitiesOnly=yes -o StrictHostKeyChecking=no
	                 -o UserKnownHostsFile=/dev/null -o ControlMaster=auto
	                 -o ControlPath=/tmp/renku-arm64-clip.ctl -o ControlPersist=120
	                 -p 2222)
	local remote=baron@127.0.0.1
	local remote_bin=/boot/home/.renku-clipboard-cli

	local waited=0
	until ssh "${ssh_opts[@]}" -o ConnectTimeout=2 -o BatchMode=yes "$remote" true 2>/dev/null; do
		waited=$((waited + 2))
		if [ "$waited" -ge 120 ]; then
			echo "clipboard: guest sshd never came up after 120s -- giving up" >&2
			return 1
		fi
		sleep 2
	done

	scp -i "$key" -o IdentitiesOnly=yes -o StrictHostKeyChecking=no \
		-o UserKnownHostsFile=/dev/null -P 2222 \
		"$DIR/clipboard/clipboard-cli" "$remote:$remote_bin" 2>/dev/null
	ssh "${ssh_opts[@]}" "$remote" "chmod +x $remote_bin" 2>/dev/null

	local last_mac="" last_guest="" mac_now guest_now
	while true; do
		if mac_now="$(pbpaste 2>/dev/null)" && [ "$mac_now" != "$last_mac" ]; then
			printf '%s' "$mac_now" | ssh "${ssh_opts[@]}" "$remote" "$remote_bin set" 2>/dev/null
			last_mac="$mac_now"
			last_guest="$mac_now"
		fi
		if guest_now="$(ssh "${ssh_opts[@]}" "$remote" "$remote_bin get" 2>/dev/null)" \
		   && [ "$guest_now" != "$last_guest" ]; then
			printf '%s' "$guest_now" | pbcopy
			last_guest="$guest_now"
			last_mac="$guest_now"
		fi
		sleep 1
	done
}

clipboard_sync_loop &
CLIP_PID=$!
cleanup_clipboard() {
	kill "$CLIP_PID" 2>/dev/null
	ssh -o ControlPath=/tmp/renku-arm64-clip.ctl -O exit baron@127.0.0.1 2>/dev/null
}
trap cleanup_clipboard EXIT
echo "clipboard: syncing with the Mac (ssh key: ${RENKU_SSH_KEY:-$HOME/.ssh/id_ed25519})"

wait "$QEMU_PID"
