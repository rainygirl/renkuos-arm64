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


### Beyond audio: 0005-0045

What testing every application on an installed arm64 system turned up, fixed
in the image rather than worked around in it:

| Patch | What was wrong |
| --- | --- |
| `0005-arm64-pl031-rtc` | The clock started at 1970 on every boot (the PL031 was never read), so no TLS certificate was valid. Taken from haiku-rwebpositive-arm64 and adapted. It used to also add PSCI reboot and power-off; RenkuOS/Source has had that since 024aeb68a7, so that half was dropped. Regenerated once since, when upstream's `2fe0ded501` added an unrelated field (`cacheable_memory_range`) to the same struct right where this patch's own `pl031_rtc` field went -- same change, new context, `pl031_rtc` now sits just ahead of it instead. |
| `0006-arm64-rchromium-fixes` | R Chromium needs kernel and runtime_loader fixes that only a separate system package carried, and that package has no media stack. They are now in this image's own `haiku` package, which provides `haiku_rchromium_fixes`. The TLS descriptor resolver preserves all required SIMD bits, and per-relocation arguments are released with the image. |
| `0007-app-server-layer-text` | From haiku-rwebpositive-arm64: text bounds in app_server layers. It used to also fix the page table query in VMSAv8TranslationMap::Query; RenkuOS/Source has had that since b71ca76473, so that half was dropped. |
| `0008-minimum-image-certs-and-repos` | No root certificates; a HaikuPorts config for a repository that does not exist on arm64; and Haiku's own repository, whose newer `haiku` package would replace this patched one on `pkgman update`. Now: certificates, and the RENKU Apps repository preconfigured instead. |
| `0009-arm64-zstd-feature` | packagefs could not read zstd packages, which is every HaikuPorts package (the certificates included). Needs the three packages in `packages/`. |
| `0010-media-recorder-start-producer` | Recording apps connected to the sound card and then received no buffers: BMediaRecorder never started the producer. |
| `0011-app-server-cjk-fallbacks` | Korean/Japanese/Chinese drew as boxes unless a font named "Noto Sans CJK JP" was installed, which on arm64 it cannot be. |
| `0012-minimum-cli-tools` | A fresh guest could fetch nothing but through pkgman: no curl, wget, tar, gzip, grep or sed, no python, bash without `/dev/tcp`. Ships grep, less and sed (bootstrap packages) and curl, wget, tar, gzip (cross-built by pkgman-repo `scripts/cross-arm64-cli.sh`; the hpkgs are in `packages/`). Build with `HAIKU_NO_DOWNLOADS=1`. |
| `0013-kernel-sock-nonblock` | `socket(SOCK_NONBLOCK)` marked only the fd O_NONBLOCK and left the socket blocking; curl sat in `recv()` for the server's idle timeout (30-400 s) on every run. The stack is now told, as `fcntl(F_SETFL)` already did. |
| `0014-minimum-webpositive` | WebPositive was not in the image at all: `@minimum-mmc` never builds it (needs `haikuwebkit_devel`, a full browser engine to link against) and `@minimum-anyboot` inherits the same definition. Ships the already-built `webpositive` package instead, the same way 0012 ships prebuilt curl/wget rather than compiling them. Needs `packages/haikuwebkit`, `sqlite3`, `dav1d`, `libavif1.0` and `noto_sans_cjk_kr` alongside it. Also declares `openssl3`/`openssl3_devel` in the arm64 repository list, which turned out to be missing entirely: 0008 already lists `openssl3` in `AddHaikuImageSystemPackages`, but nothing had ever declared a matching repository entry, so `AddRepositoryPackage` silently dropped it under `HAIKU_NO_DOWNLOADS=1` (no error -- it just isn't in the local index) and the dependency solver had nothing to offer `haikuwebkit`'s `lib:libcrypto` requirement. It worked before only because some earlier build's `download/` had the hpkg in it by hand, never captured in a patch. |
| `0015-arm64-webpositive-icu-data` | WebPositive crashed opening any page needing real text layout (google.com, reliably): `WTF::TextBreakIteratorICU`'s constructor calls `RELEASE_ASSERT` when ICU's `ubrk_open()` fails, and it failed even for the empty/root locale. Cause: arm64's bootstrap-profile `icu74` package ships a `libicudata.so.74` that is a ~130 KiB stub, not the ~30 MiB dataset WebKit needs for break iteration -- that full dataset sits unused right next to it as a loose `data/icu/74.1/icudt74l.dat`. Fix: `data/system/boot/SetupEnvironment` now exports `ICU_DATA` at that path on arm64, which is ICU's own documented override for exactly this. |
| `0016-arm64-openssh` | arm64 had no `ssh`/`sshd` at all -- not even on HaikuPorts' real (non-bootstrap) arm64 repository, which is empty. `packages/openssh-10.4p1-1-arm64.hpkg` is cross-built the same way 0012's curl/wget are, using HaikuPorts' own net-misc/openssh patchset, minus libedit (not in the arm64 package set; only gives sftp line-editing). Starts at boot via `data/launch/sshd` (a `job`/`service` pair, after `data/system/boot/SshdKeygen` generates host keys), both newly registered in `build/jam/packages/Haiku` alongside `SetupEnvironment`/`data/launch/system`/`user`, the only way files under `data/` actually ship. The service launches through `/bin/sh -c "sshd -D"` rather than a direct `launch` -- settled by hitting the failure: direct launch leaves sshd running and listening by its own debug log, yet refuses every connection, and going through one layer of `sh -c` fixes it every time; not root-caused inside launch_daemon. |
| `0017-tcp-timer-teardown` | The kernel panicked (`_mutex_lock(): using uninitialized lock` in `TCPEndpoint::_RetransmitTimer`, thread "net timer") when a TCP endpoint was deleted while its retransmit timer was already firing: the destructor destroyed the endpoint's lock before waiting for the timer hooks. It now marks the endpoint, unlocks, waits for the hooks, and destroys the lock last. Upstream Haiku has the same race; a release kernel hides it, the image's KDEBUG kernel panics. The mark is `FLAG_DELETING = 0x2000`; RenkuOS/Source has since taken 0x800 and 0x1000. |
| *(0018 retired)* | `0018-tcp-spawn-unbound` fixed a `_Spawn()` panic (`bound endpoint ... not in hash!`) the same way upstream's own `96d06c2e46` ("tcp: _Spawn() resets local address", cherry-picked from mainline Haiku) now does -- identical one-line fix, same file, same spot. Removed outright rather than kept empty like 0005/0007: nothing of it is this project's own anymore. The gap in the numbering is deliberate, not a renumber of everything after it. |
| `0019-net-notifications-keep-loaded` | The boot could deadlock. net_server was unloading the network notifications module and held the module lock while `unregister_generic_syscall()` waited for an in-flight `net_notifications_control()` call. That call (launch_daemon adding a listener) was itself waiting for the module lock in `FirstAdded()` -> `get_module()`. The module is now `B_KEEP_LOADED`, and `FirstAdded()`/`LastRemoved()` no longer call `get_module()`/`put_module()`. Seen once in 14 x86_64 boots. |
| `0020-minimum-rappmarket` | R AppMarket (browse and install every app in each pkgman repository it knows about, from a window) had to be `pkgman install`ed by hand before a fresh guest could use it to install anything else in RENKU Apps. Ships the package the same way 0012/0016 ship prebuilt curl/openssh: `packages/rappmarket-1.0.0-4-arm64.hpkg`, declared in the arm64 repository list and `AddHaikuImageSystemPackages`. |
| `0021-arm64-libroot-swap-float` | `__swap_float`/`__swap_double` (`B_HOST_TO_BENDIAN_FLOAT` and friends) had their `fmov` operands reversed and returned garbage: `__swap_float(1599.0f)` gave `a73ef000`, not `00e0c744`. Every image translator writes its bitmap header's bounds with it, so BBitmapStream made a bitmap of a random size and overran it -- Tracker crashed drawing a JPEG background, ShowImage crashed opening one. Fixed instructions checked on the guest for float, double and a round trip. |
| `0022-minimum-image-translators` | The minimum profile ships STXTTranslator alone, so ShowImage opened nothing and a JPEG set in Backgrounds was never drawn. Adds `packages/renku_imagetranslators-1.0.0-1-arm64.hpkg` (AVIF, BMP, GIF, HVIF, ICO, JPEG, PCX, PNG, PPM, PSD, RAW, RTF, SGI, TGA, WebP, WonderBrush), prebuilt because arm64 HaikuPorts has no libjpeg/libpng/libwebp/libavif -devel packages; they link the copies haikuwebkit and libavif1.0 already put on the image. How it was built: `imagetranslators/`. |
| `0023-arm64-kernel-empty-translation-map` | A failed `fork()` panicked the machine. A user `VMSAv8TranslationMap` gets its page table on the first `Map()`; one destroyed before that -- `fork_team()` giving up for lack of memory -- has `fPageTable` 0, and the destructor freed it anyway, walking physical page 0 (`unhandled page fault in kernel space at 0xffffff0000000000`, in `~VMSAv8TranslationMap` from `fork_team`). Hit twice on a 4 GB VM under memory pressure, from sshd and from R Chromium. |
| `0024-libbe-app-launched-by-exec` | Apps launched through a script that `exec()`s them (R Chromium, the web-app launchers) used a whole CPU core. The roster creates the launched team's looper port, `exec()` deletes the team's ports, and `BApplication` adopted the dead port anyway, so `task_looper()` spun on `B_BAD_PORT_ID`. The launch messages went with it, so `ReadyToRun()` never ran either (DeskCalc started that way had no window). It now keeps its own port unless the roster's is alive and owned by the team, and then posts `B_ARGV_RECEIVED` and `B_READY_TO_RUN` to itself. |
| `0025-usb-disk-recover-after-timeout` | The VM stopped in the kernel debugger with `PANIC: last transaction (853) still open!`. A USB disk transfer that took longer than 10 s (QEMU not run by a backgrounded or busy host while the guest clock went on) was cancelled without a Bulk-Only reset, the device then stalled every later command, and block reads/writes were never retried, so BFS could not write its log. usb_disk now resets after any failed transfer and retries READ/WRITE up to four times. |
| `0026-vm-free-shadowed-modified-pages` | The VM stopped in the kernel debugger with `ASSERT FAILED (vm_page.cpp:3172): (page->State() != PAGE_STATE_MODIFIED)` from `exec()` in sshd, under memory pressure. Since upstream's per-device modified queues a MODIFIED page cannot be freed once it has left its cache, and the two anonymous-cache merge paths that free shadowed pages (`_MergePagesSmallerConsumer`, `_MergeSwapPages`) did not move it to CACHED first, as `_FreePageRange()` and `Delete()` do. They now do. Submitted as [RenkuOS/Source#343](https://github.com/RenkuOS/Source/pull/343). |
| `0027-vfs-unlock-io-context-before-free` | The VM stopped in the kernel debugger with `acquire_spinlock(): Failed to acquire spinlock ... (value: deadbeef)` from `fork()` in R Chromium, under memory pressure. Since upstream c02707824b `vfs_new_io_context()` holds the new context's lock in a `WriteLocker`; when the FD table could not be allocated it freed the context and returned, and the locker then unlocked freed memory. It now unlocks and destroys the lock first. |
| `0031-arm64-fix-icache-line-size-shift` | Boot could hang forever at ~100% CPU, anywhere a module/driver gets loaded (first reliably seen loading a network driver, but not specific to it). `arch_cpu_sync_icache()` computed the icache line size from `CTR_EL0` with `ctr_el0 << 0xF` instead of `ctr_el0 & 0xF` -- a left shift of the whole register instead of masking its low 4 bits (`dcache_line_size` two lines below does this correctly). `4 << (huge shift amount)` is undefined behavior and can compile to a line size of 0, which turns the invalidation loop right below it into an infinite one that never advances. Confirmed by sampling the stuck vCPU's PC under QEMU/hvf -- it kept landing inside this function's invalidation loop. Upstream Haiku arm64 code, introduced in `6bcb56f62dfd20138ddc4194972f6eec7101800d`; not specific to this port, worth upstreaming once confirmed. |
| `0032-launchdaemon-restart-throttle` | A `service` job that exits right after every launch attempt (observed with `media_server` and, separately, `sshd`, when a NIC is attached under QEMU -- see "A network device can keep the desktop from ever appearing" below) was relaunched by `B_SYSTEM_OBJECT_UPDATE`'s `B_TEAM_DELETED` handler as fast as the kernel could create and tear down a team -- confirmed via a kernel thread dump: the job's team ID climbed by ~3000 every 5 seconds with sems/ports/threads/teams/free memory all flat (no leak, just an unthrottled loop). The handler's own `// TODO: take restart throttle into account` comment already flagged the gap. Each `Job` now remembers its last launch time; a team death less than a second after that relaunches through a one-shot `BMessageRunner` instead of immediately. Upstream `src/servers/launch/` code, not this port's own; worth upstreaming regardless of what turns out to be causing the underlying respawns. |
| `0033-media-server-addon-launch-retry` | `media_server` gave up and `exit(1)`ed after one failed attempt (signature lookup, then a path-based fallback) to launch `media_addon_server`. Confirmed via `launch_daemon`'s exit-status logging: status 1 in a boot that never reaches a desktop (NIC attached), status 0 in one that does. Now retries the whole signature-then-path sequence up to 20 times, 100ms apart, before giving up -- a real, well-contained robustness fix on its own, though confirmed NOT sufficient by itself to fix the underlying issue (see below). |
| `0034-autologin-startsession-retry` | `autologin` is a one-shot `job` (not a `service`), so a single failed `getpwuid(0)` or `BLaunchRoster().StartSession()` call means no user session, hence no desktop, ever -- with nothing else about the boot visibly wrong. Confirmed via the same exit-status logging, and traced one step further: `getpwuid(0)` returns `NULL` on every one of 30 retries over 3 seconds when this happens, persistent rather than transient. Now retries both calls together for a few seconds before giving up -- same reasoning as `0033`, and likewise confirmed not sufficient alone: the real bug, found and fixed below, is why `getpwuid(0)` kept failing in the first place. |
| `0036-packagefs-lock-read-against-activation` | The actual root cause behind `0032`/`0033`/`0034`'s symptoms: `packagefs_read()` (`src/add-ons/kernel/file_systems/packagefs/kernel_interface.cpp`) -- the entry point behind every ordinary `read()`, including the registrar's own early `fopen("/etc/passwd")` -- took no lock, unlike `packagefs_read_stat()`/`read_symlink()`/`access()`. A concurrent package activation (`AddPackageNode()`/`RemovePackageNode()`, run under the parent directory's write lock) can swap a node's backing `PackageNode` mid-read, landing an unlocked read on a `file_cache` that's mid-teardown or mid-init and getting zeroed pages back, while a concurrently stat()ed size reports the post-swap version -- confirmed directly: `fstat()` reporting 127 bytes with content entirely zero, where the real file is a static 43 bytes. Now takes a `DirectoryReadLocker` in `packagefs_read()`, matching its sibling functions -- but deliberately *not* in `packagefs_io()` too, despite the identical hazard in principle: `packagefs_io()` is reached reentrantly, same thread, from inside `packagefs_read()` itself (`node->Read()` -> `PackageFile::Read()` -> `file_cache_read()` -> cache miss -> `vfs_read_pages()` -> `vfs_vnode_io()` dispatching back to `packagefs_io()`), and Haiku's `rw_lock` has no per-thread reentrancy tracking, so locking both self-deadlocks whenever a writer is contending at that moment -- confirmed by building and boot-testing that exact version first, which reproduced the corruption in 6 of 6 trials. `packagefs_read()`'s lock already covers the reentrant path into `packagefs_io()`, since it's held for the whole call. Confirmed fixed: 10 of 10 trials reached a working `sshd` afterward, on the same boot configuration that reproduced the hang in 6 of 6 trials before. Upstream Haiku kernel code, not this port's own; worth upstreaming. |

| `0037-app-server-dpi-ui-scale` | With no font sizes chosen, app_server picked the UI scale from resolution alone (x1.5 above 1920x1080, x2 above 3840x2160) -- a 13" 2560x1664 laptop panel and a 27" 2560x1440 monitor got the same scale despite wildly different pixel density. Now reads the panel's physical size from EDID and uses `dpi / 110`, in quarter steps between 1 and 3; no EDID keeps the old resolution rule. Ported from [renkuos-apps/renku-retina](../../renkuos-apps/renku-retina) (its own patch 0001, normally shipped as the `rretina` package for a later `pkgman install`) -- folded into the image itself so the correct scale is already picked during the very first live boot, before Installer ever opens, not only after a later package install. Confirmed on the real target Mac: EDID read as 29x18 cm at 2560x1664 (≈224 dpi) picks 2x, the right scale for that panel. |
| `0038-tracker-desktop-location-scale` | Desktop icon locations are saved in pixels, so when the UI scale changes (a larger default font on a HiDPI display, or a user-chosen font size) the icons and their labels pile on top of each other. Tracker now records the scale positions were saved under (`_trk/pinfo_scale` on the desktop directory) and scales them to the current one when loading. Ported from renku-retina (patch 0002); see `0037` for why it is baked into the image. |
| `0039-ui-scale-setting` | Adds a "UI scale" menu (Automatic, 100%, 150%, 200%) to the Screen preferences, so the choice doesn't have to be four hand-picked font sizes that then stick regardless of what display the system next boots on. "Automatic" leaves `auto` in `~/config/settings/system/app_server/ui_scale` so `0037`'s EDID-based decision runs on every boot instead of a saved size overriding it. Ported from renku-retina (patch 0003); see `0037` for why it is baked into the image. |
| `0040-virtio-gpu-follow-host-size` | Adds a `VIRTIO_GPU_GET_PREFERRED_SIZE` ioctl so a userland helper can ask the host (QEMU) for its current window size without disturbing anything, lets a second process open the device without resetting it, and fixes a command-completion wait that could block forever on a completion it had already missed (an intermittently black screen, confirmed 2 of 5 boots on x86_64). The frame buffer is now allocated for the largest mode it can get contiguous memory for -- tried in order, 7680x4320 (8K), 5120x2880 (5K), 3840x2160 (4K) -- and `set_display_mode()` refuses a mode it cannot hold instead of silently overrunning it, which is what `0037`-era testing confirmed crashing app_server deterministically (every single boot) at 6720x2836, a real `system_profiler`-reported resolution on an Apple Silicon Mac Mini: 5K alone (the original upstream choice) was not enough, since this port's own QEMU launcher offers the guest the host's own resolution verbatim. Confirmed fixed at that exact resolution with the 8K-first cascade. Ported from renku-retina (patch 0004, which also adds the EDID re-read this enables -- see `0037`); the frame-buffer cascade is this port's own addition on top of it. |
| `0041-arm64-auto-install-rchromium` | A fresh install had no browser until someone opened R AppMarket or ran `pkgman` by hand. Adds a `renku-install-rchromium` job to `data/launch/user`'s `desktop` target: gated on `not read_only` (same condition `create-installer-link` already uses to tell a real install apart from the live/install medium, whose write area is far too small for a browser package -- the exact "No space left on device" pkgman gives there), triggered `on network_available` (same condition `update-time` already uses), and gated on a marker the job's own script (`data/system/boot/renku-install-rchromium`) writes only once `pkgman install -y rchromium` actually succeeds, so a boot with no network yet or an unreachable server just retries on the next boot. Deliberately not built on the stock `first-login`/`PostInstallScript` mechanism, since that marker is deleted unconditionally whether the script succeeds or not, giving it exactly one try, ever. |
| `0042-live-image-rchromium` | The USB install medium (above) still needs a real boot with network to get R Chromium. The live medium -- what [MacRenkuOS.app](../macapp/) actually downloads and installs from -- ships it already on the image instead, the same `build-renku-arm64-iso.sh` tree built a second way: `RENKU_LIVE_BUILD=1` adds `rchromium-154.0.8036.0-28` to `AddHaikuImageSystemPackages`, gated in `DefaultBuildProfiles` behind that jam variable so the plain install build never requests it. Needed re-vendoring first: `package_repo` refuses a package whose vendor differs from the rest of this local HaikuPorts-info repository, the exact constraint `0012`'s curl/wget and `0020`'s rappmarket already hit, worked around there by rebuilding each from its own recipe with the vendor forced to "Haiku Project" -- not practical for a multi-hour native Chromium build. `build-renku-arm64-iso.sh` instead re-vendors the already-built hpkg: builds the host `package` tool (`<build>package`, the same way jam builds any other host-side build tool), extracts the downloaded hpkg, edits the vendor line in the `.PackageInfo` extraction writes out, and repacks with that edited copy -- same binaries, only the metadata differs. |
| `0043-live-image-no-installer` | The live medium (`RENKU_LIVE_BUILD=1`) is meant to be run straight off the medium -- there is no install to make there. Filters `Installer` out of both `SYSTEM_APPS` and `DESKBAR_APPLICATIONS` under that same jam variable. `data/launch/user`'s own `create-installer-link` job (the one that puts an Installer shortcut on the Desktop on a read-only/live boot) already gates on `file_exists /boot/system/apps/Installer`, so dropping the binary is enough on its own to stop that shortcut from appearing too -- no change needed there. |
| `0044-live-image-skip-firstboot` | `0043` left FirstBootPrompt's Install/Try it out screen itself still showing on every live boot (the medium is always read-only, which is what `data/launch/user`'s `run` job uses to decide whether to show it), with "Install" now a dead button since the binary is gone (`FirstBootPromptWindow` draws it unconditionally, with no check that Installer still exists). `run`'s own condition only ever enters `first_boot` when `/system/bin/FirstBootPrompt` exists, so filtering it out of `SYSTEM_BIN` under `RENKU_LIVE_BUILD=1` -- same mechanism as `0043` -- is enough on its own to boot the live medium straight to the desktop, no `data/launch/user` change needed. |
| `0045-boot-splash-logo` | Before `app_server` ever starts, QEMU's own window has nothing to show -- EDK2/the EFI boot loader haven't painted anything over virtio-gpu yet, so the cocoa display's own "Display output is not active" placeholder is visible for however long that takes. RenkuOS/Source's own boot loader already draws a logo into that exact gap (`video_display_splash()`, already wired up in `default` builds) -- it was just compiled with an empty 372x96 image, since the non-trademark build removes Haiku's own logo and never substituted one of RenkuOS's own. Ported as-is from [`renkuos-logopatch`](../renkuos-logopatch/) (generated against this same pinned commit): swaps in a high-contrast RenkuOS wordmark in `headers/private/kernel/boot/images.h`, nothing else. Not an arm64 fix -- it's blank on every architecture upstream builds this way -- but worth carrying here since it is otherwise the only moment of this image's boot that shows no RenkuOS branding at all. |

## Which patches are candidates for a RenkuOS/Source PR

Most of this patch set is this port's own: build-profile config
(`@minimum-anyboot` for arm64, package lists, repository definitions) and
packaging for this image specifically. That is not upstream material --
nothing to send anywhere. A handful of patches are different: they fix a
real bug in RenkuOS/Source's own code, found here only because this port
exercises arm64 and QEMU/hvf harder than most testing does, and a fix
belongs upstream whether or not this port exists. Separated out for PR
prep:

**Already sent upstream:**

| Patch | PR |
| --- | --- |
| `0026-vm-free-shadowed-modified-pages` | [RenkuOS/Source#343](https://github.com/RenkuOS/Source/pull/343) |

**Flagged in the notes above as upstream material, not yet sent:**

| Patch | Where the bug actually is |
| --- | --- |
| `0017-tcp-timer-teardown` | `src/system/kernel/...` TCP endpoint teardown vs. its own retransmit timer -- any architecture, any release-kernel build. |
| `0031-arm64-fix-icache-line-size-shift` | `arch_cpu_sync_icache()`, `src/system/kernel/arch/arm64/arch_cpu.cpp` -- any arm64 boot, not specific to this port. |
| `0032-launchdaemon-restart-throttle` | `src/servers/launch/` -- any `service` job that exits right after launch, on any architecture; the handler's own `// TODO: take restart throttle into account` already flagged the gap. |
| `0036-packagefs-lock-read-against-activation` | `src/add-ons/kernel/file_systems/packagefs/kernel_interface.cpp` -- `packagefs_read()`'s missing lock, reachable on any architecture with enough concurrent early reads racing package activation. |

**Look like the same kind of genuine, non-port-specific bug, but have not
been reviewed with upstreaming specifically in mind -- worth a second
look before deciding:**

| Patch | Where |
| --- | --- |
| `0019-net-notifications-keep-loaded` | `net_server`'s module lock vs. `net_notifications_control()` -- a generic deadlock, not arm64-specific. |
| `0021-arm64-libroot-swap-float` | `__swap_float`/`__swap_double` in libroot -- arm64-specific (the `fmov` operands), but a libroot bug on any arm64 build, not this port's own. |
| `0023-arm64-kernel-empty-translation-map` | `~VMSAv8TranslationMap` freeing a page table that was never allocated -- arm64-specific, not this port's own. |
| `0024-libbe-app-launched-by-exec` | `BApplication`/the roster's looper-port handoff across `exec()` -- generic libbe, any architecture. |
| `0025-usb-disk-recover-after-timeout` | `usb_disk`'s missing Bulk-Only reset and retry after a timed-out transfer -- generic USB driver code, any architecture. |
| `0027-vfs-unlock-io-context-before-free` | `vfs_new_io_context()` unlocking freed memory on an allocation failure (rooted in upstream `c02707824b`) -- generic VFS code, any architecture. |

Everything else in the table above is this port's own: arm64 build-profile
work, packaging, and config that only matters because this is a stripped
`@minimum-anyboot` image for arm64 specifically.

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

The build script now pins RenkuOS/Source commit
[`8484655099aae479b59590d098dfcbf6fdc4d908`](https://github.com/RenkuOS/Source/commit/8484655099aae479b59590d098dfcbf6fdc4d908)
(2026-11-06). It is 2 commits beyond the previous pin; those commits only
change `cpu_deep_idle` code in `headers/private/kernel/cpu.h` and
`src/system/kernel/cpu.cpp`, outside the patch set. All 19 patches apply to
a clean checkout of this pin. The ISO build and boot have not yet been
repeated on this newer pin.

Previously re-derived against RenkuOS/Source commit
[`4e5de412da344adf76bb2fa5bb68c8b970e961a3`](https://github.com/RenkuOS/Source/commit/4e5de412da344adf76bb2fa5bb68c8b970e961a3)
(2026-11-06). 154
commits had landed on top of the previous pin; two touched a file one of
these patches also touches. `0018-tcp-spawn-unbound` turned out to be
upstream already -- `96d06c2e46` ("tcp: _Spawn() resets local address") is
the identical fix, cherry-picked from mainline Haiku, so that patch is
gone. `0005-arm64-pl031-rtc` needed regenerating, not removing: an
unrelated commit (`2fe0ded501`, cacheable memory ranges for ACPI) adds its
own new field to `arch_kernel_args` at the exact point this patch's own
`pl031_rtc` field used to go, so the old diff's context no longer matched;
the field now sits just ahead of it instead, same struct, same semantics.
On a clean checkout of that pin all 19 remaining patches apply,
`@minimum-anyboot` builds, and

Re-derived once before that, against RenkuOS/Source commit
[`f45bcca17e2279288a3a466ed4ed22667dd31ceb`](https://github.com/RenkuOS/Source/commit/f45bcca17e2279288a3a466ed4ed22667dd31ceb)
(2026-10-28). On a clean checkout of it all 20 patches (0018 included, not
yet redundant) applied, `@minimum-anyboot` built, and
the ISO, attached as the only USB disk under QEMU (`hvf`, `virtio-gpu-pci`,
`intel-hda`), boots to the desktop: the PL031 clock reads the real date,
the `hda` driver loads and its streams are set up, `sshd` answers on port
22, and Deskbar > Shut down powers the guest off and ends QEMU by itself.
Another tree may not take the patches; `build-renku-arm64-iso.sh` says so
rather than leaving a half-patched tree.

They were first derived against commit
[`47367b99ab8af5934adb4e709e9c0eaeb6485255`](https://github.com/RenkuOS/Source/commit/47367b99ab8af5934adb4e709e9c0eaeb6485255)
(2026-09-03), built from that commit and booted under QEMU (`qemu-system-aarch64`, `hvf`,
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
