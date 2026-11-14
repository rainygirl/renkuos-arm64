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

`arm64-patch/patches/` is 19 patches against RenkuOS/Source, applied by
`build-renku-arm64-iso.sh`. What each one changes and why is documented
file-by-file in [`arm64-patch/README.md`](arm64-patch/README.md) -- this
section only summarizes the shape of it.

RenkuOS/Source's `@minimum-mmc`/`@minimum-anyboot` profiles are deliberately
minimal on every architecture: no media stack, no browser, barely any CLI
tools. The patch adds those back (0001, 0012, 0014), fixes three things
that only break on arm64 (0005 the PL031 clock, 0006 and 0007
kernel/runtime_loader/app_server fixes, 0009 zstd package support so
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
  loader leaves Haiku's write-back cache unflushed. `--live`'s own qcow2
  overlay now persists across runs too (it used to be recreated fresh
  every launch, discarding writes on purpose), so it is exactly as exposed
  to this as an installed disk -- the same clean-shutdown requirement now
  applies to it as well. On the next boot:
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
  try. A clean guest shutdown also powers QEMU off by itself (PSCI
  power-off, in RenkuOS/Source since 024aeb68a7), so there is no reason to
  force it closed.
- **A spurious-interrupt storm can freeze the boot before the display ever
  comes up, host-side, not guest-side.** Serial console fills with
  `spurious interrupt (1023)` / `Last message repeated N times.` right
  after `virtio_pci: using legacy interrupt NN` and `register_domain(4,
  link)`, and the guest never gets further. Confirmed NOT caused by this
  port's code, any specific disk image or ISO (reproduces on a disk image
  and ISO copy that have never been booted before), the accelerator (hvf
  and `--tcg` both hit it), the GIC version, or which devices are attached
  (reproduces with the network and audio devices removed, and even with
  `virtio-gpu-pci` replaced by `ramfb` -- i.e. with no virtio-PCI device in
  the command line at all). It also reproduces on a build and a host that
  booted cleanly dozens of times earlier the same session, with nothing
  else about the invocation changed -- this is host QEMU/hvf state
  degrading over a long session of repeatedly starting and `kill -9`ing
  qemu-system-aarch64, not anything reachable from inside the guest or
  this repo. Lower vCPU counts make the race markedly less likely (though
  not impossible) to trigger; `run-qemu-renku-arm64.sh` itself now runs a
  boot watchdog that detects this exact pattern in the serial log and
  retries once at `-smp 2` and once more at `-smp 1` before giving up with
  a clear error instead of hanging silently -- if even `-smp 1` doesn't
  clear it (as happened once, confirmed reproducible for a full 90s budget
  even then), the host itself needs a restart; no combination of QEMU
  flags tried got past that.

  What actually degraded the host to that point, traced after the fact:
  dozens of qemu-system-aarch64 launches in a few hours during one
  investigation, many ended with `pkill -9` instead of a clean shutdown
  or QMP `quit`. Ordinary use of this port -- one boot, one clean
  shutdown from inside the guest -- doesn't do that; this is a note for
  whoever is next at a terminal running repeated manual boot/kill cycles
  while debugging something, not a warning about normal use. If you're
  doing that kind of iteration, prefer QMP `quit` or waiting for the
  guest to exit on its own over `kill -9`, and don't launch the next
  attempt back-to-back with the last one.

- **Most of what looked like "the host/hvf degraded" above turned out to
  be a real, fixed bug: `0031-arm64-fix-icache-line-size-shift`.**
  `arch_cpu_sync_icache()` (upstream `src/system/kernel/arch/arm64/arch_cpu.cpp`,
  not this port's own code) computed the icache invalidation line size
  with `ctr_el0 << 0xF` instead of `ctr_el0 & 0xF`; depending on the exact
  `CTR_EL0` bits QEMU hands the guest, that can compile to a line size of
  0, which turns the invalidation loop right below it into one that never
  advances -- 100% CPU, forever, the instant any module or driver gets
  its code pages written and synced (confirmed by sampling the stuck
  vCPU's PC with QMP `info registers` across dozens of boots: it kept
  landing inside this exact loop). This explains why the symptom above
  reproduced with every accelerator, every GIC version, every device
  combination, and even on builds that had booted cleanly earlier the
  same session: none of those ever mattered, because any boot that got
  far enough to load a second module could hit it depending on nothing
  but that one register's bit pattern. Fixed; confirmed fixed by
  rebuilding with the patch and reaching FirstBootPrompt repeatedly where
  it previously hung every time.

- **A network device could keep the desktop from ever appearing -- fixed
  (`0036-packagefs-lock-read-against-activation`).** With a real NIC
  attached (confirmed with both `virtio-net-pci` and `e1000`; confirmed
  under both hvf and `--tcg`, so not an accelerator-timing issue) and
  DHCP completing successfully in under a millisecond (confirmed with
  QEMU's own `-object filter-dump` packet capture -- this was never a
  DHCP problem), boot could finish at the kernel and `launch_daemon`
  level -- `net_server`, `mount_server`, `package_daemon`,
  `syslog_daemon`, and the registrar (whose team's main thread is
  actually named `roster`, not `registrar` -- easy to miss when grepping
  a thread dump) all start and stay up fine -- and yet no desktop ever
  appeared, with the system otherwise sitting idle.

  Traced (with targeted, temporary `debug_printf` tracing through
  `launch_daemon`, `autologin`, libroot's `user_group_common.cpp`, and
  finally the registrar's `AuthenticationManager::_InitPasswdDB()`
  itself) to a genuine packagefs kernel bug, not anything in
  `launch_daemon`, `autologin`, or the registrar's own logic:
  `packagefs_read()` (`src/add-ons/kernel/file_systems/packagefs/kernel_interface.cpp`)
  -- the entry point behind every ordinary `read()` syscall, including
  the registrar's own early `fopen("/etc/passwd")` -- took no lock at
  all, while `packagefs_read_stat()`, `packagefs_read_symlink()`, and
  `packagefs_access()` all take a `DirectoryReadLocker` on the node's
  parent directory first. Package activation
  (`UnpackingLeafNode::AddPackageNode()`/`RemovePackageNode()`, called
  under that same directory's *write* lock by
  `Volume::_AddPackageContentRootNode()`) can swap which `PackageNode`
  backs a node while an unlocked read is in flight: `NodeReinitVFS()`
  inits the incoming `PackageNode`'s `file_cache` before uninitializing
  the outgoing one, so there's a window where a concurrent read can
  fault in pages against a `file_cache` that's mid-teardown or
  mid-init and get zeroed pages back, while a concurrent *locked*
  `stat()` on the same node reports whichever package version resolved
  after the swap -- so the size and the data visibly disagree. Confirmed
  directly: `fstat()` on `/etc/passwd` reporting 127 bytes with the
  actual read coming back entirely zero-filled, where the file is a
  real, static 43-byte entry ("baron:x:0:0:Root...") once activation
  settles. `autologin`, `media_server`, and `sshd` each failing their
  own early-boot reads the same way (each returning `EXIT_FAILURE` from
  an ordinary, non-crashing error path, confirmed via `launch_daemon`'s
  own `wait_for_thread()` exit-status logging) is the same root cause:
  all three do early reads of files packagefs serves, in the same boot
  window `package_daemon` logs repeated package-activation churn. It
  only needed a NIC attached to show up reliably because the extra
  driver/thread work at boot shifts scheduling enough to make the
  activation race land inside a read far more often -- not because
  networking itself was ever involved.

  First attempt at the fix added the same `DirectoryReadLocker` to
  `packagefs_io()` too (the `io()` entry point, for direct/vectored
  I/O), reasoning it had the identical hazard in principle. Built and
  boot-tested, this made things *worse*: 6 of 6 identical test boots
  reproduced the exact corruption. Root cause of that regression:
  `packagefs_io()` is also reached *reentrantly*, on the same thread,
  from inside `packagefs_read()` itself, via `node->Read()` ->
  `PackageFile::Read()` -> `file_cache_read()` -> (on a cache miss,
  guaranteed on a node whose `file_cache` was just created) ->
  `vfs_read_pages()` -> `vfs_vnode_io()`, which dispatches straight
  back to `packagefs_io()` on the same vnode. Haiku's `rw_lock`
  (`headers/private/kernel/lock.h`) has no per-thread reentrancy
  tracking -- a second read-lock attempt on the same directory, by a
  thread that already holds one, can queue behind a writer that's
  itself waiting on that lock, which can't proceed until the outer
  (same-thread) read reference is released: a self-deadlock. The fix
  that actually works locks only `packagefs_read()`, which already
  holds the lock for the whole duration of its call into `node->Read()`
  -- covering the reentrant path into `packagefs_io()` too -- and
  leaves `packagefs_io()` itself unlocked (callers that reach it
  directly, not through `packagefs_read()`, such as mmap-backed I/O,
  are exactly as unprotected against this race as they always were;
  not a regression this patch introduces). Confirmed fixed: the exact
  boot configuration that reproduced the hang in 6 of 6 trials reached
  a working `sshd` in 10 of 10 trials after the fix (6 with diagnostic
  tracing still in place, 4 on the final clean build).

  This is upstream Haiku kernel code, not anything this port's own
  patches touch, and can affect any Haiku boot (any architecture) with
  enough concurrent early reads racing package activation -- worth
  upstreaming. `0033-media-server-addon-launch-retry` and
  `0034-autologin-startsession-retry`'s bounded retries remain in place
  as harmless defense-in-depth (same reasoning as `0032`), though with
  `0036` they shouldn't actually be needed anymore for this failure
  mode.

- **app_server could segfault in `memcpy` painting the very first desktop
  background -- two distinct causes, both fixed.** Signature (identical
  both times): `debug_server: Thread N entered the debugger: Segment
  violation`, PC inside `memcpy`, called from
  `HWInterface::_CopyBackToFront` -> `CopyBackToFront` ->
  `InvalidateRegion` -> `DrawingEngine::FillRegion` ->
  `Desktop::_SetBackground` -> `Desktop::Init` -> `AppServer::_CreateDesktop`.

  **Cause 1, `0040-virtio-gpu-follow-host-size`:** `virtio_gpu_init_device()`
  (`src/add-ons/kernel/drivers/graphics/virtio/virtio_gpu.cpp`) allocated
  one fixed-size framebuffer area, 3840x2160 (4K), "so we can fit every
  mode" -- but `bytes_per_row`/the scanout resource are sized from the
  *actual* requested display mode, and `virtio_gpu_set_display_mode()`
  reuses the same fixed area for every later mode change rather than
  resizing it. Any mode over 4K in either dimension overflowed it.
  Confirmed crashing deterministically (reproduced on every single boot,
  not intermittent) at 6720x2836 -- a real `system_profiler`-reported
  resolution on an Apple Silicon Mac Mini, not a synthetic value. Fixed
  by `0040` (ported from `renkuos-apps/renku-retina`, whose own fix for
  this same hazard allocates 5120x2880 -- not enough on its own, since
  this port's own QEMU launcher offers the guest the host's actual
  resolution and 6720x2836 exceeds it; this port's own addition on top
  tries 7680x4320 first, falling back to 5120x2880 and 3840x2160).
  Confirmed fixed by rebuilding and boot-testing at the exact same
  6720x2836 resolution that crashed every time before.

  **Cause 2, the qemu-notch dylib's `-S`/QMP `cont` resume (`tools/qemu-notch/qemu-notch.m`):**
  this did *not* explain the second reproduction, on the real target
  MacBook Air at its actual native resolution, 2560x1664 -- well under
  the old 4K cap, so cause 1's fix alone did not help here. Isolated by
  a set of controlled A/B boots at the identical resolution: `--window`
  (no full screen, dylib loaded but its full-screen/`-S` logic never
  triggers) booted clean; `--fullscreen` with the dylib disabled
  entirely booted clean; `--fullscreen` with the dylib's EDID/Retina
  override active but a test-only skip of just the `-S` hold booted
  clean; only the real, unmodified `--fullscreen` path (dylib active,
  `-S` hold, `cont` sent) crashed -- reproducibly, on the real machine,
  twice. Root cause: `EnterOverlay()` resizes the view/window and calls
  `Refresh()`, which only *asks* QEMU's own Cocoa UI thread to apply the
  new size and re-negotiate virtio-gpu's EDID-reported geometry -- it
  does not wait for that to finish -- before `StartGuest()` sends `cont`
  over QMP from a background queue. A guest that starts executing in
  that gap can reach its own EDID query, and then `Desktop::Init()`'s
  first paint, before the resize has actually landed, racing a
  framebuffer configuration that's still being changed out from under
  it. Fixed with a 300 ms `usleep` before `StartGuest()` opens the QMP
  socket -- a plain fixed delay rather than plumbing a real completion
  callback through QEMU's own (unhooked) resize path, cheap against the
  many seconds a boot already takes or the 30 s fallback this mechanism
  already tolerates if full screen is never reached at all. Confirmed
  fixed: 5 of 5 trials at the real crash resolution through the exact,
  unmodified `--fullscreen` path, plus the real `MacRenkuOS.app` itself
  relaunched end to end on the real target Mac.

  Both are upstream/this-port's-own-tooling issues respectively (the
  virtio_gpu one is upstream Haiku kernel code and worth upstreaming;
  the qemu-notch one is this port's own macOS-side tooling, not Haiku
  code at all) -- neither is related to the packagefs fix above, despite
  surfacing in the same investigation.

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
  https://pkgman.rainygirl.com/arm64, after boot -- not baked into the ISO,
  with two exceptions: R AppMarket itself ships on the image (0020), so a
  fresh guest always has a GUI to install the rest with, and R Retina's
  underlying app_server/Tracker/Screen/virtio_gpu patches (0037-0040) are
  folded directly into the image rather than left to R Retina's own
  `rretina` package, so the correct HiDPI scale is already picked during
  the very first live boot -- before Installer ever opens -- not only
  after a later package install.
- **Retina/HiDPI detection** (0037-0040): confirmed on the real target Mac
  (a 13" MacBook Air) through the actual `MacRenkuOS.app` launch path --
  `virtio_gpu_get_edid_info()` read the panel as 29x18 cm at 2560x1664
  (≈224 dpi), which `Desktop::_AutomaticUIScale()` turns into 2x, the
  right scale for that panel -- with no crash and a full boot to a
  working `sshd`, on the same real launch path (notch dylib, full
  screen, `-S`/QMP `cont`) that crashed before `0036`/the qemu-notch
  delay fix above. Also confirmed not regressing the virtio_gpu
  framebuffer-overflow fix: the same real `system_profiler`-reported
  6720x2836 that crashed app_server deterministically before boots
  clean with `0040`'s 8K-first allocation cascade.
