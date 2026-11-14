# RENKU arm64

[한국어](README.ko.md)

[RenkuOS](https://github.com/RenkuOS/Source) built for `arm64`, so it runs
natively on Apple Silicon under Hypervisor.framework instead of emulating
the x86 image.

## What this adds over plain RenkuOS/Source

RenkuOS/Source already builds and boots on arm64 by itself -- nothing here
patches the arm64 port. What this directory adds is everything the
`@minimum-mmc`/`@minimum-anyboot` build profiles leave out on any
architecture, plus a few fixes that only bite on arm64:

| | Plain RenkuOS/Source (arm64) | With `arm64-patch/` (this directory) |
|---|---|---|
| Audio | None at all -- no `/dev/audio`, no `media_server` | Full stack: recording and playback |
| Browser | Not built | WebPositive |
| Command line | No `curl`, `wget`, `tar`, `gzip` | All included |
| System clock | Starts at 1970 every boot (so TLS fails) | Reads the real-time clock |
| Installable medium | `@minimum-anyboot` does not build for arm64 at all | Builds, boots, and installs to a disk |
| Korean / Japanese / Chinese text | Draws as empty boxes | A bundled font covers it |
| R Chromium | Missing kernel fixes it needs | Included |
| SSH | Not on HaikuPorts for arm64 at all | `sshd` runs at boot |
| App installer | None | R AppMarket on the image |
| Package repositories | No root certificates; the configured HaikuPorts repository has no arm64 packages | Certificates included; the RENKU Apps repository is preconfigured |
| HaikuPorts packages | packagefs cannot read zstd packages, which is nearly all of them | Read and installed normally |
| Kernel stability | Panics when a TCP connection is torn down mid-timer, or while the network goes down | Fixed |

The technical detail -- exactly what each patch changes and why -- is in [`AGENTS.md`](AGENTS.md) and [`arm64-patch/README.md`](arm64-patch/README.md).

## Requirements

- A Mac (Apple Silicon for native speed; Intel works too, under emulation)
- Docker Desktop, with Settings > General > "Use Virtualization Framework" ON
- `brew install qemu`

## Current ISO

Two variants come out of the same patched tree (see `0042` in
[`arm64-patch/README.md`](arm64-patch/README.md)):

- `renku-arm64.iso` -- the USB install medium. Write it to a USB stick and
  install from it onto a disk; R Chromium comes later, fetched by the
  installed system's own first boot once it has network (`0041`).
- `renku-arm64-live.iso` -- the live medium: R Chromium already on it, no
  Installer (`0042`-`0044`), run straight off the medium. This is what
  [MacRenkuOS](https://github.com/rainygirl/MacRenkuOS) (the `renkuos`
  command) boots, through `run-qemu-renku-arm64.sh --live`.

Both are attached to the
[latest GitHub release](https://github.com/rainygirl/renkuos-arm64/releases/latest)
as `renku-arm64-current.iso` and `renku-arm64-live-current.iso`; MacRenkuOS
fetches the latter by that name, so a release must keep it.

## Building the ISO

```sh
./build-renku-arm64-iso.sh                                          # -> ./renku-arm64.iso (USB install)
RENKU_LIVE_BUILD=1 ./build-renku-arm64-iso.sh renku-arm64-live.iso   # -> ./renku-arm64-live.iso (R Chromium included)
```

First run takes a few hours, most of it the cross-toolchain. Once it exists:

```sh
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh
SKIP_CROSS_TOOLS=1 RENKU_LIVE_BUILD=1 ./build-renku-arm64-iso.sh renku-arm64-live.iso
```

reuses it and takes much less time -- building both variants only redoes
the image-assembly step, not the compile, since that is all that differs
between them.

## Trying it in QEMU

Do this before plain `./run-qemu-renku-arm64.sh`: on a fresh checkout there is
nothing installed yet, so booting without `--install` first just creates an
empty disk with no OS on it.

```sh
./run-qemu-renku-arm64.sh --install
```

boots the ISO with an (empty) disk attached, created automatically the
first time. Then, inside the guest:

1. **DriveSetup**: select the target disk, `Disk > Initialize > GUID
   Partition Map`. Create a ~64 MiB partition, type *EFI system data*,
   format FAT32. Create a second partition with the rest of the disk, type
   *Be File System*, format it.
2. **Installer**: install from `Haiku` onto the Be File System partition
   you just created.
3. **Terminal**, to put the boot loader where EFI firmware looks for it
   (the Installer does not do this step):
   ```sh
   mountvolume -all
   cp -r "/haiku esp/EFI" "/efi/"
   sync
   ```
4. **Shut the guest down properly** -- Deskbar menu > Shutdown > Power
   off -- before closing the QEMU window or quitting it any other way.
   This matters: an installed Haiku disk that loses power before its
   write-back cache is flushed can come up with a corrupted boot loader or
   a corrupted system package on the *next* boot. A normal shutdown avoids
   it entirely, and this port powers QEMU off by itself when you do.

After that, `./run-qemu-renku-arm64.sh` (no `--install`) boots the installed
disk directly. `--headless` drops the window (serial console only); the
disk (`renku-arm64-vm.img`) persists between runs either way.

It starts full screen (Cmd-F toggles it; `--window` starts in a window
instead). Full screen covers the
whole display, the strip beside a MacBook's camera notch included: macOS keeps
QEMU's own full screen below the notch, so the script injects
`tools/qemu-notch/libqemu-notch.dylib` (built on first run). QEMU still goes
into a normal macOS full screen -- its own Space, Cmd-Tab and swipes reach
other apps -- and the dylib moves its view into a window over the whole
screen inside that Space. The guest is offered the display's own resolution
(2560x1664 on a 13" MacBook Air) and its real size in the EDID, so R Retina
(UI scale Automatic) picks 2x itself. `RENKU_RES=WxH` overrides the
resolution, `RENKU_GUEST_SCALE=2` doubles it, `RENKU_NOTCH=0` turns the
dylib off.

## Shared folder

While the guest runs, its boot volume is mounted on the Mac as a network
volume named **RenkuOS** (Finder > Locations), read-write, with no login --
files dragged into it land in the guest, and the other way round. The guest
serves it over WebDAV (`0047`, `renku_webdavd`), reachable from this Mac
only, and `run-qemu-renku-arm64.sh` mounts it once the guest answers and
unmounts it when QEMU exits. `RENKU_SHARE=0` turns it off.

## SSH

`sshd` starts at boot. The script already forwards a host port to it:

```sh
ssh -p 2222 baron@127.0.0.1
```

works with a key in `~/config/settings/ssh/authorized_keys` (Haiku's
`~/.ssh` equivalent). Password login does not work -- see
[`arm64-patch/README.md`](arm64-patch/README.md) for why.

## Known limitations

WebPositive does not play HTML5 audio or video (YouTube says it can't play
the video). This is a HaikuWebKit defect, not something fixed here -- see
[`AGENTS.md`](AGENTS.md).

## License

This repository's own work (scripts, patches, docs) is MIT -- see
[`LICENSE`](LICENSE). RenkuOS itself follows its own license, not this one.
