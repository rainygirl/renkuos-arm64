# arm64 media stack patch for Haiku

Gives an arm64 Haiku built with the `@minimum-mmc` profile a working audio
path: `/dev/audio`, `media_server`, and the add-ons a recording or playback
application needs.

[한국어](README.ko.md)

## The problem

The `minimum` image definition does not merely leave media applications out,
it removes the media stack itself:

```
SYSTEM_ADD_ONS_DRIVERS_AUDIO = ;
SYSTEM_ADD_ONS_MEDIA = ;
```

and `media_server` is absent from `SYSTEM_SERVERS`. An image built from it
boots with no `/dev/audio` at all, and the serial log says

```
Launching x-vnd.haiku-media_server failed: No such file or directory
```

Any application that opens the Media Kit reports that there is no audio
device. This is not a driver problem and nothing installed afterwards fixes
it: `media_server` is part of the `haiku` package, so which profile the image
was built from decides whether it exists.

`@nightly-mmc` would include the media stack, but it does not build on arm64:

- `libmidi.so` is only built when the `fluidlite` build feature is on, and
  there is no `fluidlite` package for arm64. The image definition asks for
  the library unconditionally, so jam stops with `don't know how to make
  libmidi.so`.
- The WonderBrush translator's Jamfile never adds its own `support/`
  directory to the header search path, so `bitmap_compression.h` and
  `blending.h` are not found.

## What the patches change

`patches/0001-minimum-media-stack.patch` adds to the `minimum` definition:

| Added | Why |
| --- | --- |
| `media_server`, `media_addon_server` | the servers themselves were missing |
| `libmedia.so` | the servers and applications link against it |
| `libgame.so` | `media_addon_server` needs it, and fails to load without it |
| `hda`, `usb_audio` | the audio driver list was empty |
| `hmulti_audio.media_addon`, `mixer.media_addon` | the media add-on list was empty |

`patches/0002-wonderbrush-support-headers.patch` fixes the header search path
in the WonderBrush translator. It is not needed for `@minimum-mmc`; it is here
because it is a real bug anyone building `@nightly-mmc` will hit.

`patches/0003-efi-cd-boot.patch` makes `@minimum-cd` build on an EFI target.
It is a build fix only: the ISO it produces starts the boot loader but cannot
be booted to a desktop, for the reason given under
[Installable media on arm64](#installable-media-on-arm64).

`patches/0004-arm64-anyboot.patch` makes `@minimum-anyboot` build for arm64.
That is the one that yields a medium you can boot *and* install from.


### Beyond audio: 0005-0013

What testing every application on an installed arm64 system turned up, fixed
in the image rather than worked around in it:

| Patch | What was wrong |
| --- | --- |
| `0005-arm64-rtc-and-psci` | The clock started at 1970 on every boot (the PL031 was never read), so no TLS certificate was valid; and shutdown/reboot never finished (no PSCI call). Taken from haiku-rwebpositive-arm64 and adapted. |
| `0006-arm64-rchromium-fixes` | R Chromium needs kernel and runtime_loader fixes that only a separate system package carried, and that package has no media stack. They are now in this image's own `haiku` package, which provides `haiku_rchromium_fixes`. |
| `0007-arm64-pte-and-layer-text` | Two more fixes from haiku-rwebpositive-arm64 (page table query, text bounds in app_server layers). |
| `0008-minimum-image-certs-and-repos` | No root certificates; a HaikuPorts config for a repository that does not exist on arm64; and Haiku's own repository, whose newer `haiku` package would replace this patched one on `pkgman update`. Now: certificates, and the RENKU Apps repository preconfigured instead. |
| `0009-arm64-zstd-feature` | packagefs could not read zstd packages, which is every HaikuPorts package (the certificates included). Needs the three packages in `packages/`. |
| `0010-media-recorder-start-producer` | Recording apps connected to the sound card and then received no buffers: BMediaRecorder never started the producer. |
| `0011-app-server-cjk-fallbacks` | Korean/Japanese/Chinese drew as boxes unless a font named "Noto Sans CJK JP" was installed, which on arm64 it cannot be. |
| `0012-minimum-cli-tools` | A fresh guest could fetch nothing but through pkgman: no curl, wget, tar, gzip, grep or sed, no python, bash without `/dev/tcp`. Ships grep, less and sed (bootstrap packages) and curl, wget, tar, gzip (cross-built by pkgman-repo `scripts/cross-arm64-cli.sh`; the hpkgs are in `packages/`). Build with `HAIKU_NO_DOWNLOADS=1`. |
| `0013-kernel-sock-nonblock` | `socket(SOCK_NONBLOCK)` marked only the fd O_NONBLOCK and left the socket blocking; curl sat in `recv()` for the server's idle timeout (30-400 s) on every run. The stack is now told, as `fcntl(F_SETFL)` already did. |
| `0014-minimum-webpositive` | WebPositive was not in the image at all: `@minimum-mmc` never builds it (needs `haikuwebkit_devel`, a full browser engine to link against) and `@minimum-anyboot` inherits the same definition. Ships the already-built `webpositive` package instead, the same way 0012 ships prebuilt curl/wget rather than compiling them. Needs `packages/haikuwebkit`, `sqlite3`, `dav1d`, `libavif1.0` and `noto_sans_cjk_kr` alongside it. Also declares `openssl3`/`openssl3_devel` in the arm64 repository list, which turned out to be missing entirely: 0008 already lists `openssl3` in `AddHaikuImageSystemPackages`, but nothing had ever declared a matching repository entry, so `AddRepositoryPackage` silently dropped it under `HAIKU_NO_DOWNLOADS=1` (no error -- it just isn't in the local index) and the dependency solver had nothing to offer `haikuwebkit`'s `lib:libcrypto` requirement. It worked before only because some earlier build's `download/` had the hpkg in it by hand, never captured in a patch. |
| `0015-arm64-webpositive-icu-data` | WebPositive crashed opening any page needing real text layout (google.com, reliably): `WTF::TextBreakIteratorICU`'s constructor calls `RELEASE_ASSERT` when ICU's `ubrk_open()` fails, and it failed even for the empty/root locale. Cause: arm64's bootstrap-profile `icu74` package ships a `libicudata.so.74` that is a ~130 KiB stub, not the ~30 MiB dataset WebKit needs for break iteration -- that full dataset sits unused right next to it as a loose `data/icu/74.1/icudt74l.dat`. Fix: `data/system/boot/SetupEnvironment` now exports `ICU_DATA` at that path on arm64, which is ICU's own documented override for exactly this. |
| `0016-arm64-openssh` | arm64 had no `ssh`/`sshd` at all -- not even on HaikuPorts' real (non-bootstrap) arm64 repository, which is empty. `packages/openssh-10.4p1-1-arm64.hpkg` is cross-built the same way 0012's curl/wget are, using HaikuPorts' own net-misc/openssh patchset, minus libedit (not in the arm64 package set; only gives sftp line-editing). Starts at boot via `data/launch/sshd` (a `job`/`service` pair, after `data/system/boot/SshdKeygen` generates host keys), both newly registered in `build/jam/packages/Haiku` alongside `SetupEnvironment`/`data/launch/system`/`user`, the only way files under `data/` actually ship. The service launches through `/bin/sh -c "sshd -D"` rather than a direct `launch` -- settled by hitting the failure: direct launch leaves sshd running and listening by its own debug log, yet refuses every connection, and going through one layer of `sh -c` fixes it every time; not root-caused inside launch_daemon. |

## Use

```sh
../build-renku-arm64-iso.sh
```

applies every patch in `patches/`, stages `packages/*.hpkg` into the
generated directory's `download/` (where 0009's zstd packages and 0014's
WebPositive set need to be), and builds `@minimum-anyboot`. It is
idempotent in the sense that matters: it always starts from a fresh
checkout of the pinned commit, so there's no tree to half-patch.

Applying the patches by hand against some other RenkuOS/Source checkout
(for `@minimum-mmc` instead, say) works the same way `git apply` always
does:

```sh
for p in patches/*.patch; do git -C /path/to/haiku apply "$p"; done
cp packages/*.hpkg /path/to/generated.arm64/download/
```

## Verified

Derived from and verified against RenkuOS/Source commit
[`47367b99ab8af5934adb4e709e9c0eaeb6485255`](https://github.com/RenkuOS/Source/commit/47367b99ab8af5934adb4e709e9c0eaeb6485255)
(2026-09-03), which the `nightly` tag pointed at on 2026-09-29. That tag
moves; if it has moved far enough the patches will no longer apply, and
`build-renku-arm64-iso.sh` says so rather than leaving a half-patched tree.

Built from that commit and booted under QEMU (`qemu-system-aarch64`, `hvf`,
`-device intel-hda -device hda-duplex`) on Apple Silicon:

```
HDA: Detected controller @ PCI:0:3:0, IRQ:38, type 8086/2668
hda: HDA v1.0, O:4/I:4/B:0, #SDO:1, 64bit:yes
loaded driver /boot/system/add-ons/kernel/drivers/dev/audio/hmulti/hda
```

`ls /dev/audio` shows `hmulti`, and R Spectrum - a spectrum analyzer that
takes the system's audio input - reports `96000 Hz  48 bands  peak -78.0
dBFS` instead of `No audio input device`.


## Installable media on arm64

`patches/0004-arm64-anyboot.patch` makes `@minimum-anyboot` build for arm64,
which gives what the profile gives on x86: one file that boots and that you
install *from*, onto a separate disk.

```sh
../build-renku-arm64-iso.sh       # applies the patches and runs both jam
                                   # steps below itself; shown separately
                                   # here only to name the quirk
jam -q -j8 haiku-boot-cd          # see the quirk below
jam -q -j8 '@minimum-anyboot'     # -> haiku-minimum-anyboot.iso
```

The result is a hybrid: an ISO9660 image with a UEFI El Torito entry, and at
the same time an MBR-partitioned disk carrying a real BFS partition and an
ESP.

```
MBR signature 0xaa55
  part0  type 0xeb  bootable  offset 4194304   300.0 MiB  (BFS, "Haiku")
  part1  type 0xef            offset 318767104   2.8 MiB  (FAT32, ESP)
El Torito boot img : 1 UEFI -> /esp.image
```

That BFS partition is the point. `haiku_loader` has no iso9660 driver, so it
can never read a system off a pure ISO9660 medium - a plain `@minimum-cd`
ISO starts the loader and then stops at *"Cannot continue booting (Boot
volume is not valid)"*, before any kernel exists. Here the loader finds a
filesystem it can read on the same medium and boots normally. DriveSetup on
the running system shows it: the ISO appears as a disk whose partition 0 is
Be File System, mounted at `/boot`.

### Installing from it

Verified end to end under QEMU on Apple Silicon, the anyboot ISO attached as
USB storage and an empty 8 GB disk as the target:

1. DriveSetup: initialise the target with a **GUID Partition Map**, create a
   64 MiB partition of type *EFI system data* formatted FAT32, and a
   Be File System partition over the rest.
2. Installer: install onto the BFS partition. It reports
   *"Installation completed. Boot sector has been written to ..."*
3. Copy the boot loader into the new ESP - the Installer does not do this on
   EFI:

   ```sh
   mountvolume -all
   cp -r "/haiku esp/EFI" "/new fat vol/"
   ```

4. Detach the medium and boot the disk. `/boot` is then the installed BFS
   volume, and `ps` shows `media_server` and `media_addon_server` running.

The raw `.image` / `.mmc` medium from `@minimum-mmc` works the same way and
is still the better choice for writing to an SD card.

### Two things to know

- **Attach the disk over USB, not AHCI.** QEMU's `edk2-aarch64` firmware has
  no SATA driver, so with `-device ahci` it finds no boot device at all and
  drops to the UEFI Shell with `map: No mapping found`. Haiku itself sees the
  AHCI disk fine once booted - it is the firmware that cannot start from it.
- **Run `jam haiku-boot-cd` before `@minimum-anyboot`.** `haiku-boot-cd.iso`
  is an `RmTemps` target, so the previous run deletes it and the next one
  fails with `failed to open ISO file`. This is an upstream quirk, not
  something these patches introduce - the TODO comment in `AnybootImage`
  already refers to it.

## Known limitation: no audio/video in WebPositive

`haikuwebkit`'s `buildMediaEnginesVector()` (`Source/WebCore/platform/graphics/MediaPlayer.cpp`)
registers Cocoa, GStreamer, Media Foundation and HolePunch media engines, but
none for Haiku -- `PlatformMediaEngineClassName` is defined for
`PLATFORM(HAIKU)` but never used to register one. `installedMediaEngines()`
comes back empty, so every `canPlayType()` returns nothing and WebPositive
cannot play HTML5 audio or video (YouTube: "this browser can't play this
video"). Confirmed present in HaikuWebKit 1.9.19 and 1.9.26 on x86; the
arm64 image here ships 1.10.0 from the same codebase and almost certainly
has the same gap, though it has not been tested directly.

This is a HaikuWebKit source defect, not something `0014` or any package
staged in `packages/` can fix -- `haikuwebkit` here is consumed prebuilt,
never compiled from source. Fixing it means patching HaikuWebKit itself and
rebuilding the `haikuwebkit`/`haikuwebkit_devel` packages, which is a
separate undertaking from this image. (Found and being fixed for x86 in a
parallel session; worth checking whether that fix reached 1.10.0 before
reinvestigating here.)

## Known limitation: SSH password authentication

`ssh`/`sshd` work -- key-based login verified end to end, host machine to
guest, through the `hostfwd=tcp::2222-:22` `run-qemu-renku-arm64.sh` already
sets up. Password authentication does not: every password is rejected,
confirmed with `sshd -d -d -d` showing `mm_answer_authpassword: sending
result 0` for a password just set with `passwd` seconds earlier.

Cause: Haiku's own `crypt()` (`src/system/libroot/posix/crypt/crypt.cpp`)
uses a Haiku-specific scrypt-based hash (`$s$<n>$<salt>$<hash>`), and its
actual password store is a registrar service reached by BMessage, not a
POSIX shadow file -- `getspnam()` is a compatibility shim over that, not a
real file read. OpenSSH's password path (`auth-passwd.c` /
`openbsd-compat/xcrypt.c`) is written for the POSIX shadow-file model, and
the mismatch is deep enough that it is not a one-line fix; not attempted
here. Key-based login is unaffected and is the supported way in with this
image.

## License

MIT

## AI disclosure

These patches were prepared with Claude.
