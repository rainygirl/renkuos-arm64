#!/bin/bash
#
# Pull a complete Haiku arm64 devel SDK (headers, crt objects, libbe.so/
# libroot.so, cross-g++'s own libstdc++ headers, libstdc++.so/libgcc_s.so)
# out of the haiku-builder container into the directory given as $1. Shared
# by every clipboard/, appmarket/, etc. build.sh so the extraction logic --
# and the three packagefs-merge-only symlinks it has to work around -- live
# in exactly one place.
#
# Needs the haiku-builder container that build-renku-arm64-iso.sh creates
# (run that script -- or at least its cross-tools step -- first).
#
# Haiku's public headers are deeply cross-included (Application.h alone
# pulls in Looper.h, Handler.h, Archivable.h, Message.h, ...), so building
# against anything less than the complete headers tree fails one missing
# file at a time instead of all at once -- this pulls the whole tree, not
# a hand-picked subset.
#
# Output layout in $1: devel/{headers,lib}, cxx-headers/, gcc-crt/.
set -euo pipefail
OUT="${1:?usage: fetch-devel-sdk.sh <output-dir>}"
CONTAINER="${RENKU_CONTAINER:-haiku-builder}"
WORK=/root/renku-arm64-work

docker exec "$CONTAINER" true 2>/dev/null \
	|| { echo "container '$CONTAINER' not running -- run build-renku-arm64-iso.sh (or SKIP_CROSS_TOOLS=1 to reuse an existing one) first" >&2; exit 1; }

mkdir -p "$OUT"

# The devel package's own contents (headers/ + lib/), not the sparse
# develop/headers some other local SDK extraction might have -- this is
# the real, complete arm64 devel SDK this exact build produced.
DEVEL_PKG="$(docker exec "$CONTAINER" bash -c \
	"find $WORK/generated.arm64/objects/haiku/arm64/packaging/packages_build -iname 'hpkg_-haiku_devel.hpkg' -print -quit")"
[ -n "$DEVEL_PKG" ] || { echo "haiku_devel package contents not found in $CONTAINER -- did the build finish?" >&2; exit 1; }
rm -rf "$OUT/devel"
docker cp "$CONTAINER:$DEVEL_PKG/contents/develop" "$OUT/devel"

# haiku_devel's own develop/lib/{libbe,libroot}.so are relative symlinks
# (../../lib/lib*.so) that only resolve inside a real merged packagefs
# tree -- the real files are in the base "haiku" package, right next to
# haiku_devel in the same packages_build output.
HAIKU_PKG="$(dirname "$DEVEL_PKG")/hpkg_-haiku.hpkg"
rm -f "$OUT/devel/lib/libbe.so" "$OUT/devel/lib/libroot.so"
docker cp "$CONTAINER:$HAIKU_PKG/contents/lib/libbe.so" "$OUT/devel/lib/libbe.so"
docker cp "$CONTAINER:$HAIKU_PKG/contents/lib/libroot.so" "$OUT/devel/lib/libroot.so"

# The cross-compiler's own C++ headers (libstdc++) and crt/libgcc objects.
CROSS="$WORK/generated.arm64/cross-tools-arm64"
rm -rf "$OUT/cxx-headers"
docker cp "$CONTAINER:$CROSS/aarch64-unknown-haiku/include/c++/13.3.0" "$OUT/cxx-headers"
mkdir -p "$OUT/gcc-crt"
docker cp "$CONTAINER:$CROSS/lib/gcc/aarch64-unknown-haiku/13.3.0/crtbeginS.o" "$OUT/gcc-crt/"
docker cp "$CONTAINER:$CROSS/lib/gcc/aarch64-unknown-haiku/13.3.0/crtendS.o" "$OUT/gcc-crt/"
docker cp "$CONTAINER:$CROSS/lib/gcc/aarch64-unknown-haiku/13.3.0/libgcc.a" "$OUT/gcc-crt/"

# libstdc++.so/libgcc_s.so* aren't in the devel package at all -- they're
# gcc_syslibs(_devel), a separate package pulled in only at image build
# time. gcc_syslibs_devel's own develop/lib/lib{stdc++,gcc_s}.so are
# symlinks (../../lib/lib*.so) with the same problem as libbe.so above, so
# take the non-devel gcc_syslibs package's lib/ directly instead -- its
# libstdc++.so symlink is relative within that same directory and survives
# being copied out standalone.
SYSLIBS_PKG="$(docker exec "$CONTAINER" bash -c \
	"find $WORK/generated.arm64/build_packages -maxdepth 1 -iname 'gcc_syslibs-*-arm64' -print -quit")"
[ -n "$SYSLIBS_PKG" ] || { echo "gcc_syslibs package not found in $CONTAINER" >&2; exit 1; }
docker cp "$CONTAINER:$SYSLIBS_PKG/lib/libstdc++.so" "$OUT/gcc-crt/"
docker cp "$CONTAINER:$SYSLIBS_PKG/lib/libstdc++.so.6.0.32" "$OUT/gcc-crt/"
docker cp "$CONTAINER:$SYSLIBS_PKG/lib/libgcc_s.so.1" "$OUT/gcc-crt/"
cat > "$OUT/gcc-crt/libgcc_s.so" <<'EOF'
/* GNU ld script
   Use the shared library, but some functions are only in
   the static library.  */
GROUP ( libgcc_s.so.1 -lgcc )
EOF

echo "SDK ready in $OUT (devel/, cxx-headers/, gcc-crt/)"
