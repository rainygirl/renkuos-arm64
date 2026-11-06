# RenkuOS boot splash logo patch

RenkuOS has a branded logo for About and Deskbar, but its boot image header
still contains an empty logo for non-trademark builds. This patch replaces the
boot logo pixels in `headers/private/kernel/boot/images.h` with a high contrast
372x96 RenkuOS wordmark. The existing `video_display_splash()` path in current
upstream already draws this header in `default` builds.

The wordmark is adapted from the upstream
[`RenkuOS logo - about.png`](https://github.com/RenkuOS/Source/commit/328e19c8e399f43b617027bc226ddb718ee2fba4):
white lettering and the orange OS mark on black, sized for the boot splash.
`renkuos-boot-splash-logo.patch` changes only the generated logo data; it leaves
the boot icons and their palette unchanged.

The patch was generated against RenkuOS/Source HEAD
`8484655099aae479b59590d098dfcbf6fdc4d908` (2026-11-06). Apply it from a
clean checkout of that source:

```sh
git apply renkuos-boot-splash-logo.patch
```

To regenerate the image header, install Python Pillow and run
`generate-boot-logo-header.py <RenkuOS-Source-checkout>`. The checked-in PNG
is the boot-sized, high contrast artwork used by the generator.
