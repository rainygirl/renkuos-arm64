#!/bin/bash
#
# Rebuild clipboard-cli for RENKU arm64.
#
# Needs the haiku-builder container that build-renku-arm64-iso.sh creates
# (run that script -- or at least its cross-tools step -- first). The
# container holds a *complete* devel SDK (haiku_devel.hpkg's own contents:
# full headers, crt objects, libbe.so/libroot.so) and a complete cross-g++
# with its own libstdc++ headers -- this script pulls both out fresh each
# time rather than trusting a stale local copy, since nothing here commits
# a multi-megabyte SDK to the repo.
#
# Haiku's public headers are deeply cross-included (Application.h alone
# pulls in Looper.h, Handler.h, Archivable.h, Message.h, ...), so building
# against anything less than the complete headers tree fails one missing
# file at a time instead of all at once -- pull the whole thing, don't try
# to hand-pick which headers a given .cpp needs.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER="${RENKU_CONTAINER:-haiku-builder}"
WORK=/root/renku-arm64-work

docker exec "$CONTAINER" true 2>/dev/null \
	|| { echo "container '$CONTAINER' not running -- run build-renku-arm64-iso.sh (or SKIP_CROSS_TOOLS=1 to reuse an existing one) first" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The devel package's own contents (headers/ + lib/), not the sparse
# develop/headers some other local SDK extraction might have -- this is
# the real, complete arm64 devel SDK this exact build produced.
DEVEL_PKG="$(docker exec "$CONTAINER" bash -c \
	"find $WORK/generated.arm64/objects/haiku/arm64/packaging/packages_build -iname 'hpkg_-haiku_devel.hpkg' -print -quit")"
[ -n "$DEVEL_PKG" ] || { echo "haiku_devel package contents not found in $CONTAINER -- did the build finish?" >&2; exit 1; }
docker cp "$CONTAINER:$DEVEL_PKG/contents/develop" "$TMP/devel"

# haiku_devel's own develop/lib/{libbe,libroot}.so are ALSO relative
# symlinks (../../lib/lib*.so, same packagefs-merge assumption as
# libgcc_s.so below) -- the real files are in the base "haiku" package,
# right next to haiku_devel in the same packages_build output.
HAIKU_PKG="$(dirname "$DEVEL_PKG")/hpkg_-haiku.hpkg"
rm -f "$TMP/devel/lib/libbe.so" "$TMP/devel/lib/libroot.so"
docker cp "$CONTAINER:$HAIKU_PKG/contents/lib/libbe.so" "$TMP/devel/lib/libbe.so"
docker cp "$CONTAINER:$HAIKU_PKG/contents/lib/libroot.so" "$TMP/devel/lib/libroot.so"

# The cross-compiler's own C++ headers (libstdc++) and crt/libgcc objects,
# separate from the devel package above.
CROSS="$WORK/generated.arm64/cross-tools-arm64"
docker cp "$CONTAINER:$CROSS/aarch64-unknown-haiku/include/c++/13.3.0" "$TMP/cxx-headers"
mkdir -p "$TMP/gcc-crt"
docker cp "$CONTAINER:$CROSS/lib/gcc/aarch64-unknown-haiku/13.3.0/crtbeginS.o" "$TMP/gcc-crt/"
docker cp "$CONTAINER:$CROSS/lib/gcc/aarch64-unknown-haiku/13.3.0/crtendS.o" "$TMP/gcc-crt/"
docker cp "$CONTAINER:$CROSS/lib/gcc/aarch64-unknown-haiku/13.3.0/libgcc.a" "$TMP/gcc-crt/"

# libstdc++.so/libgcc_s.so* aren't in the devel package at all -- they're
# gcc_syslibs(_devel), a separate package pulled in only at image build
# time. gcc_syslibs_devel's own develop/lib/lib{stdc++,gcc_s}.so are
# symlinks (../../lib/lib*.so) that resolve only once merged into a real
# installed packagefs tree, so take the non-devel gcc_syslibs package's
# lib/ directly instead -- its libstdc++.so symlink is relative within
# that same directory and survives being copied out standalone.
SYSLIBS_PKG="$(docker exec "$CONTAINER" bash -c \
	"find $WORK/generated.arm64/build_packages -maxdepth 1 -iname 'gcc_syslibs-*-arm64' -print -quit")"
[ -n "$SYSLIBS_PKG" ] || { echo "gcc_syslibs package not found in $CONTAINER" >&2; exit 1; }
docker cp "$CONTAINER:$SYSLIBS_PKG/lib/libstdc++.so" "$TMP/gcc-crt/"
docker cp "$CONTAINER:$SYSLIBS_PKG/lib/libstdc++.so.6.0.32" "$TMP/gcc-crt/"
docker cp "$CONTAINER:$SYSLIBS_PKG/lib/libgcc_s.so.1" "$TMP/gcc-crt/"
cat > "$TMP/gcc-crt/libgcc_s.so" <<'EOF'
/* GNU ld script
   Use the shared library, but some functions are only in
   the static library.  */
GROUP ( libgcc_s.so.1 -lgcc )
EOF

CLANG="${RENKU_CLANG:-/opt/homebrew/opt/llvm/bin/clang++}"
LLD="${RENKU_LLD:-/opt/homebrew/bin/ld.lld}"
command -v "$CLANG" >/dev/null 2>&1 || { echo "$CLANG not found -- brew install llvm" >&2; exit 1; }
command -v "$LLD" >/dev/null 2>&1 || { echo "$LLD not found -- brew install lld" >&2; exit 1; }

H="$TMP/devel/headers"
LIB="$TMP/devel/lib"
CXXINC="$TMP/cxx-headers"
CRT="$TMP/gcc-crt"

VFS="$TMP/vfs.json"
python3 - "$H" "$VFS" <<'PYEOF'
import json, pathlib, sys
h = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
roots = [{"type": "file", "name": "/__haiku/" + str(p.relative_to(h)), "external-contents": str(p)}
         for p in h.rglob("*") if p.is_file()]
out.write_text(json.dumps({"version": 0, "case-sensitive": True, "roots": roots}))
PYEOF

FLAGS=(--target=aarch64-unknown-haiku -O2 -fPIC -D_DEFAULT_SOURCE -ivfsoverlay "$VFS")
for p in "$H/config" "$H/bsd" "$H/posix" "$H/os" "$H/os"/*/ "$H"; do
	[ -d "$p" ] && FLAGS+=(-isystem "${p/$H//__haiku}")
done
CPPFLAGS=(-std=c++17 -nostdinc++ -isystem "$CXXINC" -isystem "$CXXINC/aarch64-unknown-haiku" "${FLAGS[@]}")

"$CLANG" "${CPPFLAGS[@]}" -c "$DIR/clipboard-cli.cpp" -o "$TMP/clipboard-cli.o"

"$LLD" -m aarch64elf --no-undefined --no-rosegment -z norelro --eh-frame-hdr --hash-style=both \
	-L"$LIB" -L"$CRT" \
	-e _start -soname _APP_ -o "$DIR/clipboard-cli" \
	"$LIB/crti.o" "$CRT/crtbeginS.o" "$LIB/start_dyn.o" "$LIB/init_term_dyn.o" \
	"$TMP/clipboard-cli.o" \
	-lbe -lstdc++ -lgcc_s -lroot \
	"$CRT/crtendS.o" "$LIB/crtn.o"

chmod +x "$DIR/clipboard-cli"
echo "built: $DIR/clipboard-cli"
file "$DIR/clipboard-cli"
