# RENKU arm64

RenkuOS (https://github.com/RenkuOS/Source) built for `arm64` so it runs
natively on Apple Silicon under Hypervisor.framework, instead of emulating
the x86 image. The tree has its own native arm64 support already; what this
directory adds is in `arm64-patch/` -- audio, the arm64 clock and PSCI
power-off, R Chromium's kernel fixes, curl/wget/grep/tar/gzip, and
WebPositive, none of which the `@minimum-mmc`/`@minimum-anyboot` profiles
carry on their own. See `arm64-patch/README.md` for what each of the 14
patches does and why.

There are exactly two scripts here. Nothing else -- if a third one shows up,
it is scratch work that should not still be around.

## Building the ISO

```sh
./build-renku-arm64-iso.sh              # -> ./renku-arm64.iso
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh   # reuse a cached toolchain
```

Clones `RenkuOS/Source` at the `nightly` tag (a moving tag -- resolves to
whatever it currently points at), applies `arm64-patch/patches/`, builds
the arm64 cross-toolchain, and runs `jam @minimum-anyboot`: a hybrid ISO
that boots directly (attached as a CD-ROM) and that the Installer can also
install from, onto a separate disk. Runs inside a native-architecture
(arm64) Docker container -- forcing amd64 on Apple Silicon was tried first
and made gcc occasionally corrupt an emulated compile under both Rosetta
and QEMU translation, a different translation unit each time.

`SKIP_CROSS_TOOLS=1` reuses an already-built cross-toolchain (that step
alone is roughly an hour).

## Launching QEMU

When asked to "boot it", "run it", or "start QEMU", run:

```sh
./run-renku-arm64.sh                # boot the installed disk, in a window
./run-renku-arm64.sh --install      # boot the ISO with the disk attached
./run-renku-arm64.sh --headless     # no window
```

Do not hand-assemble a `qemu-system-aarch64` command line. This script
already pins what the port requires -- disks attached over USB (QEMU's
edk2-aarch64 firmware has no SATA driver, so AHCI is invisible to it and
the machine drops to the UEFI Shell), the installer medium and the target
disk on separate xhci controllers (four devices on one controller breaks
the guest's USB keyboard/tablet), `-cpu host` under hvf -- and getting any
of them wrong produces a hang or a controller with no working input, not a
clear error.

The disk (`renku-arm64-vm.img`) persists between runs.

## Status

Verified: boots to a desktop under `hvf` at native speed, with audio,
WebPositive, and the R* apps (installed separately from the hosted RENKU
Apps package repository, https://pkgman.rainygirl.com/arm64, via `pkgman`,
not baked into the ISO).
