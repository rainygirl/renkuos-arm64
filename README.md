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
| Shutdown / reboot | Never completes | Works |
| Installable medium | `@minimum-anyboot` does not build for arm64 at all | Builds, boots, and installs to a disk |
| Korean / Japanese / Chinese text | Draws as empty boxes | A bundled font covers it |
| R Chromium | Missing kernel fixes it needs | Included |
| SSH | Not on HaikuPorts for arm64 at all | `sshd` runs at boot |

The technical detail -- exactly what each of the 16 patches changes and
why -- is in [`AGENTS.md`](AGENTS.md) and [`arm64-patch/README.md`](arm64-patch/README.md).

## Requirements

- A Mac (Apple Silicon for native speed; Intel works too, under emulation)
- Docker Desktop, with Settings > General > "Use Virtualization Framework" ON
- `brew install qemu`

## Building the ISO

```sh
./build-renku-arm64-iso.sh              # -> ./renku-arm64.iso
```

First run takes a few hours, most of it the cross-toolchain. Once it exists:

```sh
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh
```

reuses it and takes much less time.

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
