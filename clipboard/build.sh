#!/bin/bash
#
# Rebuild clipboard-cli for RENKU arm64. See ../tools/fetch-devel-sdk.sh
# for where the SDK this links against comes from.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$DIR/../tools/fetch-devel-sdk.sh" "$TMP/sdk"

CLANG="${RENKU_CLANG:-/opt/homebrew/opt/llvm/bin/clang++}"
LLD="${RENKU_LLD:-/opt/homebrew/bin/ld.lld}"
command -v "$CLANG" >/dev/null 2>&1 || { echo "$CLANG not found -- brew install llvm" >&2; exit 1; }
command -v "$LLD" >/dev/null 2>&1 || { echo "$LLD not found -- brew install lld" >&2; exit 1; }

H="$TMP/sdk/devel/headers"
LIB="$TMP/sdk/devel/lib"
CXXINC="$TMP/sdk/cxx-headers"
CRT="$TMP/sdk/gcc-crt"

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
