#!/bin/sh
# Cross-compiles R AppMarket for Haiku and leaves dist/<arch>/RAppMarket,
# resources attached (BEOS:TYPE/BEOS:ICON via rc+xres).
#
#   tools/cross-build.sh arm64     reuses ../build.sh's own clang/lld build
#   tools/cross-build.sh x86_64    haiku/cross-compiler:x86_64-r1beta4
#   tools/cross-build.sh x86       haiku/cross-compiler:x86_gcc2h-r1beta4,
#                                   its secondary (modern-gcc) x86 compiler
#
# x86_64 and x86 need only a local Docker, nothing else: both cross-compiler
# images already bundle Haiku's headers and libbe/libtracker, so the
# compile+link step is fully self-contained. What they do NOT have is a
# *runnable* rc/xres -- /system/bin/rc inside either image is a Haiku target
# binary, not a Linux one, and segfaults under the container's own kernel.
# Resources are attached afterwards with the arm64 tree's host-native
# rc/xres instead (built as ordinary Linux tools by the same Jam run that
# produces everything else in generated.arm64/objects/linux/arm64) -- they
# handle any target ELF, independent of its own architecture, which is
# exactly how RInstallWizard's own tools/cross-build.sh does this too.
#
# main.cpp never includes <ObjectList.h> (see its own PtrList note) because
# RENKU arm64 is built against a Haiku nightly where BObjectList takes
# ownership as a second template argument, while these R1 Beta 4 images
# predate that change and only take a runtime constructor bool -- two
# incompatible signatures under one name. Hitting another header mismatch
# like that here means the fix belongs in main.cpp (avoid the API), not in
# this script.
#
#   ARM64_CONTAINER   default haiku-builder
#   ARM64_GENERATED   default /root/renku-arm64-work/generated.arm64
set -e

ARCH="$1"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/dist/$ARCH"
mkdir -p "$OUT"

ARM64_CONTAINER="${ARM64_CONTAINER:-haiku-builder}"
ARM64_GENERATED="${ARM64_GENERATED:-/root/renku-arm64-work/generated.arm64}"

# Attaches BEOS:TYPE/BEOS:ICON to $OUT/RAppMarket using the arm64 build
# tree's own host (Linux) rc/xres -- shared by every non-arm64 target below
# since none of them can run a Haiku-native rc/xres themselves.
attach_resources() {
	docker exec "$ARM64_CONTAINER" true 2>/dev/null \
		|| { echo "container '$ARM64_CONTAINER' not running -- run build-renku-arm64-iso.sh (or SKIP_CROSS_TOOLS=1) first; its host rc/xres are what attach resources to every architecture, not just arm64" >&2; exit 1; }

	local tag="$1"
	docker cp "$OUT/RAppMarket" "$ARM64_CONTAINER:/tmp/RAppMarket-$tag.bin"
	docker cp "$HERE/RAppMarket.rdef" "$ARM64_CONTAINER:/tmp/RAppMarket-$tag.rdef"
	local rc="$ARM64_GENERATED/objects/linux/arm64/release/tools/rc/rc"
	local xres="$ARM64_GENERATED/objects/linux/arm64/release/tools/xres"
	docker exec "$ARM64_CONTAINER" bash -c \
		"$rc -o /tmp/RAppMarket-$tag.rsrc /tmp/RAppMarket-$tag.rdef && $xres -o /tmp/RAppMarket-$tag.bin /tmp/RAppMarket-$tag.rsrc"
	docker cp "$ARM64_CONTAINER:/tmp/RAppMarket-$tag.bin" "$OUT/RAppMarket"
	chmod +x "$OUT/RAppMarket"
}

case "$ARCH" in
arm64)
	"$HERE/build.sh"
	cp "$HERE/RAppMarket" "$OUT/RAppMarket"
	;;
x86_64)
	( cd "$HERE" && tar cf - main.cpp Strings.h ) \
		| docker run --rm -i --platform linux/amd64 haiku/cross-compiler:x86_64-r1beta4 sh -c "
			set -e
			mkdir -p /work && cd /work && tar xf -
			x86_64-unknown-haiku-g++ -c main.cpp -O2 -std=c++17 -Wno-multichar -o main.o
			x86_64-unknown-haiku-g++ -o RAppMarket main.o -Xlinker -soname=_APP_ -lbe -lstdc++
			tar cf - RAppMarket" \
		| tar xf - -C "$OUT"
	attach_resources x86_64
	;;
x86)
	( cd "$HERE" && tar cf - main.cpp Strings.h ) \
		| docker run --rm -i --platform linux/amd64 haiku/cross-compiler:x86_gcc2h-r1beta4 sh -c "
			set -e
			mkdir -p /work && cd /work && tar xf -
			i586-pc-haiku-g++-x86 -c main.cpp -O2 -std=c++17 -Wno-multichar -o main.o
			i586-pc-haiku-g++-x86 -o RAppMarket main.o -Xlinker -soname=_APP_ -lbe -lstdc++
			tar cf - RAppMarket" \
		| tar xf - -C "$OUT"
	attach_resources x86
	;;
*)
	echo "usage: $0 arm64|x86_64|x86" >&2
	exit 1
	;;
esac

file "$OUT/RAppMarket"
