#!/bin/sh
# Builds libqemu-notch.dylib next to this script. See qemu-notch.m.
#
# The default SDK can be newer than the linker that ships with it (Command
# Line Tools 27.2's .tbd files name an arm64e.x1 architecture its ld does not
# know), so every installed SDK is tried, newest first, until one links.
set -e
cd "$(dirname "$0")"
build() {
	clang ${1:+-isysroot "$1"} -dynamiclib -fobjc-arc -O2 -framework Cocoa \
		-o libqemu-notch.dylib qemu-notch.m 2>/dev/null
}
if ! build ""; then
	ok=""
	for sdk in $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX*.*.sdk \
	                   /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -rV); do
		if build "$sdk"; then ok="$sdk"; break; fi
	done
	[ -n "$ok" ] || { echo "no SDK links qemu-notch.m" >&2; exit 1; }
fi
echo "built $(pwd)/libqemu-notch.dylib"
