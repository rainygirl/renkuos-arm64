#!/bin/bash
#
# Build a bootable-and-installable RenkuOS arm64 ISO, from scratch, on
# macOS.
#
# Clones https://github.com/RenkuOS/Source (the `nightly` tag), applies the
# patches in arm64-patch/patches/ -- the media stack, the arm64 clock and
# power-off, the R Chromium kernel fixes, curl/wget/grep/tar/gzip, and
# WebPositive -- and builds `@minimum-anyboot`: a hybrid ISO that boots
# directly (attached as a CD-ROM) and that the Installer can also install
# from onto a separate disk. See arm64-patch/README.md for what each patch
# does and why.
#
# Runs entirely inside a native-architecture Linux container -- the Haiku
# build needs a case-sensitive filesystem and a Linux host toolchain, and
# building a container for the Mac's own CPU (arm64 on Apple Silicon) means
# it runs at full speed instead of under Rosetta or QEMU translation. The
# latter was tried first and made the whole build unreliable: under both,
# gcc occasionally corrupted an emulated compile of some ordinary
# translation unit (a different one each run), which looks exactly like a
# flaky compiler bug until the same source builds clean natively.
#
# Usage:
#   ./build-renku-arm64-iso.sh [output-iso-path]
#
# Environment variables:
#   HAIKU_TREE_GIT_URL   Default https://github.com/RenkuOS/Source.git
#   HAIKU_TREE_GIT_REF   Branch, tag or commit to build. Default "nightly"
#                        (a moving tag -- resolves to whatever it currently
#                        points at).
#   SKIP_CROSS_TOOLS=1   Reuse an already-built cross-toolchain instead of
#                        rebuilding it (that step alone is roughly an hour).
#   JOBS                 Parallelism inside the container. Default: nproc.
#
# Requirements on the Mac:
#   - Docker Desktop, with Settings > General > "Use Virtualization
#     Framework" ON.
#   - Roughly 40 GB free for the container's own filesystem.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_ISO="${1:-$SCRIPT_DIR/renku-arm64.iso}"

CONTAINER_NAME="${RENKU_CONTAINER:-haiku-builder}"
IMAGE_NAME="ubuntu:22.04"
WORK_IN_CONTAINER="/root/renku-arm64-work"
ISO_IN_CONTAINER="/root/renku-arm64.iso"
HAIKU_TREE_GIT_URL="${HAIKU_TREE_GIT_URL:-https://github.com/RenkuOS/Source.git}"
HAIKU_TREE_GIT_REF="${HAIKU_TREE_GIT_REF:-nightly}"
SKIP_CROSS_TOOLS="${SKIP_CROSS_TOOLS:-0}"
JOBS="${JOBS:-}"

log() { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\n\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "this script is for macOS"
[ -d "$SCRIPT_DIR/arm64-patch/patches" ] || die "arm64-patch/patches is missing -- that is the arm64 port"

DOCKER=docker
command -v docker >/dev/null 2>&1 || DOCKER=/Applications/Docker.app/Contents/Resources/bin/docker
command -v "$DOCKER" >/dev/null 2>&1 || die "docker not found -- install Docker Desktop first"
"$DOCKER" info >/dev/null 2>&1 || die "Docker daemon not reachable -- is Docker Desktop running? (open -a Docker)"

# Nothing about the build needs an amd64 build machine, only a Linux one --
# matching the container to the Mac's own CPU is what makes it run natively.
case "$(uname -m)" in
	arm64)  PLATFORM=linux/arm64 ;;
	x86_64) PLATFORM=linux/amd64 ;;
	*)      die "unrecognized host architecture: $(uname -m)" ;;
esac

# ---------------------------------------------------------------------------
log "Setting up the $CONTAINER_NAME container"
# ---------------------------------------------------------------------------
if "$DOCKER" container inspect "$CONTAINER_NAME" >/dev/null 2>&1; then
	# Start it first -- a stopped container can't be execed into, and that
	# must not be mistaken for an arch mismatch: this container's own
	# filesystem is the only copy of whatever the build had gotten
	# through, so recreating it when a restart would have done throws
	# that progress away for nothing.
	if [ "$("$DOCKER" container inspect -f '{{.State.Status}}' "$CONTAINER_NAME")" != "running" ]; then
		log "Starting existing container $CONTAINER_NAME"
		"$DOCKER" start "$CONTAINER_NAME" >/dev/null
	fi
	EXISTING_ARCH="$("$DOCKER" exec "$CONTAINER_NAME" uname -m 2>/dev/null || echo unknown)"
	# Linux's own uname -m spells these differently from the Mac's
	# ("aarch64", "x86_64"), so PLATFORM can't be compared to it directly.
	case "$PLATFORM" in
		linux/arm64) EXPECTED_ARCH=aarch64 ;;
		linux/amd64) EXPECTED_ARCH=x86_64 ;;
	esac
	if [ "$EXISTING_ARCH" != "$EXPECTED_ARCH" ]; then
		log "$CONTAINER_NAME is $EXISTING_ARCH, not $EXPECTED_ARCH -- recreating it"
		"$DOCKER" rm -f "$CONTAINER_NAME" >/dev/null
	fi
fi
if ! "$DOCKER" container inspect "$CONTAINER_NAME" >/dev/null 2>&1; then
	log "Creating $CONTAINER_NAME ($IMAGE_NAME, $PLATFORM, native)"
	"$DOCKER" run -d --name "$CONTAINER_NAME" --platform "$PLATFORM" "$IMAGE_NAME" sleep infinity
fi

# ---------------------------------------------------------------------------
log "Copying arm64-patch/ into the container"
# ---------------------------------------------------------------------------
"$DOCKER" exec "$CONTAINER_NAME" rm -rf /root/arm64-patch
"$DOCKER" cp "$SCRIPT_DIR/arm64-patch" "$CONTAINER_NAME:/root/arm64-patch"

# ---------------------------------------------------------------------------
log "Running the build inside the container (this is the slow part)"
# ---------------------------------------------------------------------------
"$DOCKER" exec -i \
	-e HAIKU_TREE_GIT_URL="$HAIKU_TREE_GIT_URL" \
	-e HAIKU_TREE_GIT_REF="$HAIKU_TREE_GIT_REF" \
	-e SKIP_CROSS_TOOLS="$SKIP_CROSS_TOOLS" \
	-e JOBS="$JOBS" \
	-e WORK_DIR="$WORK_IN_CONTAINER" \
	-e OUTPUT_ISO="$ISO_IN_CONTAINER" \
	"$CONTAINER_NAME" bash -s <<'INSIDE_CONTAINER'
set -euo pipefail

log() { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\n\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

JOBS="${JOBS:-$(nproc)}"

# ---------------------------------------------------------------------------
log "Checking build dependencies"
# ---------------------------------------------------------------------------
REQUIRED=(git wget curl gcc g++ make bison flex gawk nasm autoconf automake
	libtool zip unzip python3 xorriso)
MISSING=()
for c in "${REQUIRED[@]}"; do command -v "$c" >/dev/null 2>&1 || MISSING+=("$c"); done
if [ "${#MISSING[@]}" -gt 0 ]; then
	log "Installing missing packages via apt: ${MISSING[*]}"
	apt-get update -qq
	apt-get install -y -qq build-essential bison flex gawk texinfo nasm \
		git wget curl autoconf automake libtool python3 zip unzip xorriso \
		zlib1g-dev libzstd-dev liblzma-dev libncurses-dev
fi

mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# ---------------------------------------------------------------------------
log "Fetching RenkuOS/Source"
# ---------------------------------------------------------------------------
[ -d buildtools/.git ] || git clone https://github.com/haiku/buildtools.git
[ -d haiku/.git ] || git clone "$HAIKU_TREE_GIT_URL" haiku

# fetch + checkout FETCH_HEAD works whether HAIKU_TREE_GIT_REF names a
# branch, a tag, or a raw commit -- unlike origin/$REF, which only exists
# for branches. A moving tag (nightly) resolves to whatever it currently
# points at.
log "Checking out $HAIKU_TREE_GIT_REF"
git -C haiku fetch --quiet origin "$HAIKU_TREE_GIT_REF"
git -C haiku checkout --quiet --detach FETCH_HEAD
git -C haiku reset --quiet --hard FETCH_HEAD
git -C haiku clean -qfdx -e generated.arm64
git -C haiku log -1 --oneline

# jam builds itself via a plain Makefile into buildtools/jam/bin.<platform>,
# named by `uname` at build time -- not worth hardcoding.
JAM="$(find buildtools/jam -maxdepth 2 -type f -name jam -perm -u+x 2>/dev/null | head -1)"
if [ -z "$JAM" ]; then
	log "Building jam"
	make -C buildtools/jam >/dev/null
	JAM="$(find buildtools/jam -maxdepth 2 -type f -name jam -perm -u+x | head -1)"
	[ -n "$JAM" ] || die "jam built but no binary found under buildtools/jam/bin.*"
fi
JAM="$WORK_DIR/$JAM"

# ---------------------------------------------------------------------------
log "Applying arm64-patch/patches"
# ---------------------------------------------------------------------------
for p in /root/arm64-patch/patches/*.patch; do
	name="$(basename "$p")"
	if git -C haiku apply --whitespace=nowarn --check "$p" 2>/dev/null; then
		git -C haiku apply --whitespace=nowarn "$p"
		echo "applied $name"
	else
		die "$name did not apply -- the tree has moved on, re-derive the patch (see arm64-patch/README.md)"
	fi
done

GEN="$WORK_DIR/generated.arm64"
mkdir -p "$GEN/download"
cp -f /root/arm64-patch/packages/*.hpkg "$GEN/download/"
log "Staged $(ls /root/arm64-patch/packages/*.hpkg | wc -l | tr -d ' ') packages into download/"

# ---------------------------------------------------------------------------
log "Building the arm64 cross-toolchain"
# ---------------------------------------------------------------------------
if [ "$SKIP_CROSS_TOOLS" = "1" ] && [ -d "$GEN/cross-tools-arm64/bin" ]; then
	log "Cross-tools present, skipping (SKIP_CROSS_TOOLS=1)"
else
	# configure's output IS the current directory -- there is no -o flag.
	mkdir -p "$GEN"
	( cd "$GEN" && ../haiku/configure --build-cross-tools arm64 \
		--cross-tools-source ../buildtools \
		--distro-compatibility official --use-gcc-pipe -j"$JOBS" )
fi

# ---------------------------------------------------------------------------
log "Building the ISO (jam @minimum-anyboot) -- this is the slow part"
# ---------------------------------------------------------------------------
# The patches changed the arm64 repository list, which changes its checksum;
# without this, jam asks Haiku's CDN for an index filed under that checksum
# and 404s. HAIKU_NO_DOWNLOADS=1 makes it build the index from download/
# instead, which is exactly what the patches staged.
#
# haiku-boot-cd is built explicitly first: AnybootImage declares
# haiku-boot-cd.iso a temporary of the anyboot target and deletes it after a
# successful build, so a second run finds it missing and anyboot fails with
# "failed to open ISO file".
export HAIKU_NO_DOWNLOADS=1
( cd "$GEN" && "$JAM" -q -j"$JOBS" haiku-boot-cd )
( cd "$GEN" && "$JAM" -q -j"$JOBS" '@minimum-anyboot' )

SRC_ISO="$GEN/haiku-minimum-anyboot.iso"
[ -f "$SRC_ISO" ] || die "expected ISO not found at $SRC_ISO"
mkdir -p "$(dirname "$OUTPUT_ISO")"
cp "$SRC_ISO" "$OUTPUT_ISO"
log "Built: $OUTPUT_ISO ($(du -h "$OUTPUT_ISO" | cut -f1))"
INSIDE_CONTAINER

# ---------------------------------------------------------------------------
log "Copying the finished ISO out to the Mac"
# ---------------------------------------------------------------------------
mkdir -p "$(dirname "$OUTPUT_ISO")"
"$DOCKER" cp "$CONTAINER_NAME:$ISO_IN_CONTAINER" "$OUTPUT_ISO"
"$DOCKER" exec "$CONTAINER_NAME" rm -f "$ISO_IN_CONTAINER"

log "Built: $OUTPUT_ISO ($(du -h "$OUTPUT_ISO" | cut -f1))"
echo "Boot it with:      ./run-renku-arm64.sh --install"
echo "Or write to a USB stick:  sudo dd if=$OUTPUT_ISO of=/dev/rdiskN bs=4m"
