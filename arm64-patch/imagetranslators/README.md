# renku_imagetranslators

How `../packages/renku_imagetranslators-1.0.0-1-arm64.hpkg` (patch 0022) was
built, in the `haiku-builder` container with the tree at 8484655099 and
patches 0001-0020 applied:

1. `UserBuildConfig` goes into `generated.arm64/`. It enables the jpeg,
   libpng, libwebp and libavif build features against headers and libraries
   staged from the HaikuWebKit arm64 build (`/root/wk/sysroot`, the same
   libjpeg-turbo 3.1.1, libpng 1.6.50, libwebp and libavif 1.1.1 the image
   installs), and forces `translator-fixes.h` into every translator.
2. `translator-fixes.h` replaces the broken arm64 `__swap_float`/
   `__swap_double` (fixed in libroot by 0021) and defines the
   `B_TRANSLATOR_EXT_DOCUMENT_COUNT/INDEX` strings, so the package also works
   on images built before 0021 and before 13a583a7f0.
3. `jam -q AVIFTranslator BMPTranslator GIFTranslator HVIFTranslator
   ICOTranslator JPEGTranslator PCXTranslator PNGTranslator PPMTranslator
   PSDTranslator RAWTranslator RTFTranslator SGITranslator TGATranslator
   WebPTranslator WonderBrushTranslator` (delete their `.o` files first if the
   flags changed: jam does not rebuild on a flag change).
4. `make-package.sh` packs them with the Linux-hosted `package` tool.

Remove `generated.arm64/UserBuildConfig` afterwards; it applies to every
build in that directory.

Verified on the RENKU arm64 VM: a 6144x3558 JPEG is drawn as the desktop
background and opens in ShowImage; `BTranslationUtils::GetBitmap()` returns
the right size.
