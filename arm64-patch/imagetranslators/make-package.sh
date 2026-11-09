#!/bin/sh
# Package the arm64 image translators built in generated.arm64 into an hpkg.
set -e
G=/root/renku-arm64-work/generated.arm64
T=$G/objects/haiku/arm64/release/add-ons/translators
W=/tmp/imgtr; rm -rf $W; mkdir -p $W/s/add-ons/Translators
for t in avif/AVIF bmp/BMP gif/GIF hvif/HVIF ico/ICO jpeg/JPEG pcx/PCX png/PNG ppm/PPM psd/PSD raw/RAW rtf/RTF sgi/SGI tga/TGA webp/WebP wonderbrush/WonderBrush; do
	cp $T/${t}Translator $W/s/add-ons/Translators/
done
cat > $W/s/.PackageInfo <<INFO
name			renku_imagetranslators
version			1.0.0-1
architecture	arm64
summary			"Image translators for RenkuOS arm64"
description		"The Haiku data translators for images -- JPEG, PNG, WebP, AVIF, GIF, BMP, ICO, PPM, TGA, SGI, PCX, PSD, HVIF, RAW, WonderBrush -- plus RTF, which the minimum image leaves out. Without them ShowImage opens nothing and a picture set as the desktop background is not drawn. Built from RenkuOS/Source 8484655099 against the libjpeg-turbo, libpng, libwebp (haikuwebkit) and libavif (libavif1.0) the image already ships."
packager		"RENKU <renku@localhost>"
vendor			"Haiku Project"
licenses		{ "MIT" }
copyrights		{ "2001-2026 Haiku, Inc. et al" }
provides		{ renku_imagetranslators = 1.0.0 }
requires		{
	haiku
	haikuwebkit
	libavif1.0
}
INFO
export LD_LIBRARY_PATH=/root/rretina-lib
STORE=/root/rretina-attrs/00000000000000000000000000000
rm -rf $STORE; mkdir -p $STORE
$G/objects/linux/arm64/release/tools/package/package create -z zlib -C $W/s $W/renku_imagetranslators-1.0.0-1-arm64.hpkg >&2
$G/objects/linux/arm64/release/tools/package/package list $W/renku_imagetranslators-1.0.0-1-arm64.hpkg | tail -20 >&2
rm -rf $STORE
