#!/bin/bash
#
# Boot RenkuOS arm64 in QEMU, on Apple Silicon (hvf) or under emulation.
#
# Usage:
#   ./run-renku-arm64.sh                boot the installed disk, in a window
#   ./run-renku-arm64.sh --install      boot the ISO with the disk attached,
#                                       to partition and install with it
#   ./run-renku-arm64.sh --headless     no window; QMP control socket only
#   ./run-renku-arm64.sh --tcg          force emulation over hvf
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
#   - A guest built with arm64-patch 0005 reads the PL031 clock and powers
#     off or reboots through PSCI, so shutting down from inside the guest
#     (Deskbar > Shut Down, or `shutdown`) ends QEMU by itself. An image
#     without that patch starts at 1970 (every TLS certificate then fails)
#     and stops at "safe to turn off" without ending QEMU.
#   - RENKU_AUDIO=none swaps the CoreAudio backend for silence; with
#     CoreAudio, a recording app only gets samples if the terminal running
#     this has microphone permission in macOS System Settings.
#
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DISK="$DIR/renku-arm64-vm.img"
ISO="${RENKU_ISO:-$DIR/renku-arm64.iso}"
QMP="/tmp/renku-arm64.qmp"

INSTALL=0; HEADLESS=0; FORCE_TCG=0
for a in "$@"; do
	case "$a" in
		--install)  INSTALL=1 ;;
		--headless) HEADLESS=1 ;;
		--tcg)      FORCE_TCG=1 ;;
		*) echo "unknown option: $a" >&2; exit 1 ;;
	esac
done

if pgrep -f "qemu-system-aarch64.*$DISK" >/dev/null 2>&1; then
	echo "already running against $DISK -- two QEMU instances on the same disk will corrupt it" >&2
	exit 1
fi

if [ ! -f "$DISK" ]; then
	[ "$INSTALL" = "1" ] || echo "no disk yet at $DISK -- creating an empty one, but it has no OS on it; run with --install first" >&2
	qemu-img create -f raw "$DISK" 16G
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
	[ -f "$ISO" ] || { echo "installer medium not found: $ISO -- run ./build-renku-arm64-iso.sh first" >&2; exit 1; }
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

if [ "$HEADLESS" = "1" ]; then
	DISPLAY_ARGS=(-display none -device ramfb -serial file:"$DIR/serial.log")
else
	DISPLAY_ARGS=(-display cocoa -device ramfb -serial file:"$DIR/serial.log")
fi

rm -f "$QMP"
echo "disk:     $DISK"
echo "accel:    $ACCEL   cpu: $CPU"
echo "serial:   $DIR/serial.log"
echo "qmp:      $QMP"

exec qemu-system-aarch64 \
	-M virt -cpu "$CPU" -accel "$ACCEL" -smp 4 -m 4096 \
	-bios "$FW" \
	-device qemu-xhci,id=usb \
	"${MEDIA[@]}" \
	-device usb-kbd,bus=usb.0 \
	-device usb-tablet,bus=usb.0 \
	-netdev user,id=n0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n0 \
	-audiodev "${RENKU_AUDIO:-coreaudio}",id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0 \
	-qmp unix:"$QMP",server,nowait \
	"${DISPLAY_ARGS[@]}"
