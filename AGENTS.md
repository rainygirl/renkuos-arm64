# RENKU arm64 -- technical notes

[한국어](AGENTS.ko.md)

What `README.md` doesn't cover: how the build works, what the patches do,
and traps worth knowing before touching any of it. If you're an AI agent
working in this directory, read this file before doing anything.

## Commands

There are exactly two scripts here. Nothing else -- if a third one shows
up, it is scratch work that should not still be around.

```sh
./build-renku-arm64-iso.sh              # -> ./renku-arm64.iso
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh   # reuse a cached toolchain
```

When asked to "boot it", "run it", or "start QEMU":

```sh
./run-qemu-renku-arm64.sh                # boot the installed disk, in a window
./run-qemu-renku-arm64.sh --install      # boot the ISO with the disk attached
./run-qemu-renku-arm64.sh --headless     # no window
./run-qemu-renku-arm64.sh --bridged      # add a LAN-reachable NIC (needs sudo)
./run-qemu-renku-arm64.sh --clipboard    # sync the Mac's clipboard with the guest's
```

Do not hand-assemble a `qemu-system-aarch64` command line. `run-qemu-renku-arm64.sh`
already pins what the port requires -- disks attached over USB (QEMU's
edk2-aarch64 firmware has no SATA driver, so AHCI is invisible to it and
the machine drops to the UEFI Shell), the installer medium and the target
disk on separate xhci controllers (four devices on one controller breaks
the guest's USB keyboard/tablet), `-cpu host` under hvf -- and getting any
of them wrong produces a hang or a controller with no working input, not a
clear error. The disk (`renku-arm64-vm.img`) persists between runs.

## The patch

`arm64-patch/patches/` is 17 patches against RenkuOS/Source, applied by
`build-renku-arm64-iso.sh`. What each one changes and why is documented
file-by-file in [`arm64-patch/README.md`](arm64-patch/README.md) -- this
section only summarizes the shape of it.

RenkuOS/Source's `@minimum-mmc`/`@minimum-anyboot` profiles are deliberately
minimal on every architecture: no media stack, no browser, barely any CLI
tools. The patch adds those back (0001, 0012, 0014), fixes three things
that only break on arm64 (0005 the PL031 clock and PSCI power-off, 0006 and
0007 kernel/runtime_loader/app_server fixes, 0009 zstd package support so
packagefs can read anything HaikuPorts ships), and makes `@minimum-anyboot`
buildable for arm64 at all (0003, 0004 -- upstream's anyboot image is
BIOS+EFI and three of its pieces are x86-only).

Regenerating the patch (if the upstream tree moves and it stops applying)
means diffing a RenkuOS/Source checkout with the fixes made directly in it
against the pinned base commit -- see the individual patch files for the
exact commit each was verified against.

## Build pipeline

`build-renku-arm64-iso.sh` runs entirely inside a Docker container matching
the **host's own CPU architecture** (arm64 on Apple Silicon, amd64 on
Intel). This was not the original design -- forcing `linux/amd64` on Apple
Silicon was tried first, and made the whole build unreliable: under both
Rosetta and QEMU's own translation, gcc occasionally corrupted an emulated
compile of some ordinary translation unit, a different one each run
(`libiberty/vprintf-support.c`, then `pexecute.o`, then zlib's `gzlib.o`).
That looks exactly like a flaky compiler bug until the same source builds
clean natively. Matching the container to the host's CPU fixed both the
reliability and the speed.

`HAIKU_NO_DOWNLOADS=1` is required: the patches change the arm64 repository
package list, which changes its checksum, and jam would otherwise ask
Haiku's CDN for an index filed under that new checksum and 404. With it set,
jam builds the repository index from `download/` instead, which is why
`arm64-patch/packages/*.hpkg` has to be staged there before the build (the
script does this automatically).

One gap this surfaced: `openssl3`/`openssl3_devel` were never actually
declared in the arm64 repository list, despite an existing
`AddHaikuImageSystemPackages openssl3` for the root-certificate fix. Under
normal (downloading) builds this went unnoticed because the package came
from the network; under `HAIKU_NO_DOWNLOADS=1` with a clean `download/`,
`AddRepositoryPackage` silently drops any package with no matching file --
no error, it's just missing from the local index -- so it surfaced only once
`haikuwebkit` needed `lib:libcrypto` and the dependency solver had nothing
to offer it. Fixed in `0014-minimum-webpositive.patch`, which is also where
WebPositive itself, HaikuWebKit, and their other runtime dependencies
(sqlite3, dav1d, libavif1.0, noto_sans_cjk_kr) get declared the same way.

That same silent-drop behavior means `download/` has to hold *every*
package the repository file declares, not just `arm64-patch/packages/`'s
own -- generic upstream bootstrap packages too (freetype, ncurses6, gcc,
icu74, and two dozen more), which nothing commits to this repo because
they aren't this port's own. A `haiku-builder` container that already has
them in `generated.arm64/download/` from an earlier build never notices;
a fresh container or a fresh machine does, failing minutes into the slow
part with `fatal error: float.h: No such file or directory` or similar,
for a cause that has nothing to do with the real error at the point it
surfaces. `build-renku-arm64-iso.sh` now closes that gap itself: right
after checking out the pristine tree (before patching changes its
checksum), it hashes `build/jam/repositories/HaikuPorts/arm64`, and after
staging `arm64-patch/packages/`, it parses that same file (now patched)
for every package it declares and fetches whatever's still missing from
`download/` under `https://eu.hpkg.haiku-os.org/haikuports/master/build-packages/<that pristine hash>/packages/<name>-<version>-<arch>.hpkg`
-- the exact URL template `RemoteRepositoryFetchPackage` in
`build/jam/RepositoryRules` itself uses, with the one checksum that's
actually valid for packages predating this port's own patches. Packages
`arm64-patch/patches/` added itself (curl, webpositive, openssh, ...) were
never published anywhere under that hash and 404 if asked for, but
they're already staged by the time this runs, so the fetch loop never
asks. A real gap (`curl_source`, since our cross-built curl replaces
HaikuPorts' own and was never published as a source package at all) prints
a warning and moves on rather than failing the whole build --
`AddRepositoryPackage`'s own silent-drop means a package that's genuinely
unneeded never gets missed later either.

## QEMU gotchas

Settled by hitting the failure, not preference:

- **The boot/install medium must be attached over USB.** EDK2's
  `edk2-aarch64` firmware has no SATA driver; an AHCI disk is invisible to
  it and the machine drops to the UEFI Shell with `map: No mapping found`.
  Haiku's own driver sees AHCI disks fine once the kernel is running -- it
  is only the firmware that can't start from one.
- **Two USB mass-storage devices hang the boot splash -- but only once the
  second one has a real, readable filesystem on it.** A blank/unpartitioned
  second USB disk is fine (this is what `--install` normally attaches,
  since the whole point is to partition it). Re-attaching an *already
  installed* disk alongside the ISO on a later boot reproduces the hang.
  Put that disk on `-device ahci` instead (`ide-hd` under it) when you need
  to inspect or compare an installed disk against the live medium; Haiku
  sees it fine once booted.
- **Shut the guest down properly before quitting QEMU.** This is the
  install procedure's one real requirement, confirmed by reproducing the
  failure twice: forcing QEMU closed (`quit` over QMP, or just closing the
  window) right after `Installer` finishes or right after copying the EFI
  loader leaves Haiku's write-back cache unflushed. On the next boot:
  - The copied `BOOTAA64.EFI` can come up corrupted (byte-identical in
    size, different MD5) -- the EFI Shell reports `Command Error Status:
    Unsupported` trying to load it.
  - A system package (observed: `zstd-1.5.6-2-arm64.hpkg`) can come up
    corrupted the same way, and since `libbe.so` links against
    `libzstd.so.1` from it, `launch_daemon` fails to start and the boot
    hangs at the splash screen with the serial log reading
    `runtime_loader: Cannot open file libzstd.so.1 ... error starting
    "/boot/system/servers/launch_daemon"`.

  Both are **corrupted-copy symptoms, not a packaging or packagefs bug** --
  rebuilding the same install and using Deskbar > Shutdown > Power off
  before ever quitting QEMU produced byte-identical files (MD5-verified)
  and a clean boot to a full desktop, WebPositive included, on the first
  try. A clean guest shutdown also powers QEMU off by itself (`arm64-patch`
  0005's PSCI support), so there is no reason to force it closed.

## Known limitations

**WebPositive plays no HTML5 audio or video.** HaikuWebKit's
`buildMediaEnginesVector()` (`Source/WebCore/platform/graphics/MediaPlayer.cpp`)
registers Cocoa, GStreamer, Media Foundation and HolePunch media engines,
but none for Haiku -- `PlatformMediaEngineClassName` is defined under
`PLATFORM(HAIKU)` but never used to register one, so
`installedMediaEngines()` comes back empty and every `canPlayType()`
returns nothing. Confirmed on HaikuWebKit 1.9.19 and 1.9.26 (x86); this
image ships 1.10.0 from the same codebase and almost certainly has the same
gap, though it hasn't been tested directly here. This is a HaikuWebKit
source defect -- fixing it means patching HaikuWebKit itself and rebuilding
the `haikuwebkit`/`haikuwebkit_devel` packages, a separate undertaking from
this image, which consumes `haikuwebkit` prebuilt. (A parallel session is
fixing this for x86; worth checking whether that reached 1.10.0 before
reinvestigating here.)

## Verified

- **Live boot** (ISO attached as the only USB disk): desktop, WebPositive,
  `curl`/`wget`/`grep`/`tar`/`gzip`, and a real HTTPS request
  (`curl -sI https://example.com` -> `200 OK`) all confirmed working.
- **Install** (DriveSetup -> Installer -> copy EFI loader -> clean
  shutdown -> reboot with only the installed disk attached): boots to a
  full desktop with WebPositive present, confirmed end to end.
- **SSH**: `sshd` starts at boot. Key-based login from the host to the
  guest confirmed working. Password login does not work, because of
  Haiku's own authentication model (see `arm64-patch/README.md`).
- **Bridged networking** (`--bridged`): the second NIC comes up and gets a
  DHCP lease on the real LAN; sshd on it answered from outside the guest's
  NAT subnet, confirmed with a clean shutdown afterward (see "Bridged
  networking" in the script's own header for the sudo requirement and why).
- **Clipboard sync** (`--clipboard`): `clipboard/clipboard-cli` cross-built
  and confirmed round-trip both directions -- `pbcopy` on the Mac reaching
  the guest's clipboard, and the guest's clipboard reaching `pbpaste` --
  over the existing SSH port, polled once a second. Text only; no
  virtio-console exists in Haiku to do this through QEMU directly instead
  (checked, not assumed).
- **R\* apps**: install via `pkgman` from the hosted repository,
  https://pkgman.rainygirl.com/arm64, after boot -- not baked into the ISO.
