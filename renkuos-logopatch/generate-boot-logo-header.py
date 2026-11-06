#!/usr/bin/env python3
"""Update Haiku's generated boot image header with the RenkuOS splash logo."""
import re
import sys
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent
DEFAULT_SOURCE = Path(__file__).resolve().parents[2] / "renku" / "Source"
source = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SOURCE
header = source / "headers/private/kernel/boot/images.h"
text = header.read_text()

logo = Image.open(ROOT / "assets/renkuos-boot-logo.png").convert("RGB")
if logo.size != (372, 96):
    raise SystemExit(f"logo must be 372x96, got {logo.size}")

# The boot blitter consumes BGR rows. The existing shared 8-bit palette is
# retained so the icon image and its palette remain byte-for-byte unchanged.
rgb = logo.tobytes()
bgr = bytes(channel for i in range(0, len(rgb), 3) for channel in (rgb[i + 2], rgb[i + 1], rgb[i]))
palette_match = re.search(r"static const uint8 k8BitPalette\[\] = \{(.*?)\};", text, re.S)
if palette_match is None:
    raise SystemExit("could not find k8BitPalette")
values = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", palette_match.group(1))]
if len(values) != 768:
    raise SystemExit(f"expected 256 RGB palette entries, found {len(values) // 3}")
palette = [tuple(values[i:i + 3]) for i in range(0, len(values), 3)]

colors = set(zip(rgb[0::3], rgb[1::3], rgb[2::3]))
nearest = {}
for color in colors:
    nearest[color] = min(
        range(256),
        key=lambda i: sum((color[channel] - palette[i][channel]) ** 2 for channel in range(3)),
    )
indexed = bytes(nearest[tuple(rgb[i:i + 3])] for i in range(0, len(rgb), 3))

def format_array(name: str, data: bytes) -> str:
    rows = ["\t" + ", ".join(str(byte) for byte in data[i:i + 12]) + ","
        for i in range(0, len(data), 12)]
    return f"static uint8 {name}[] = {{\n" + "\n".join(rows) + "\n};"

def replace_array(name: str, data: bytes) -> None:
    global text
    pattern = rf"static uint8 {re.escape(name)}\[\] = \{{.*?\n\}};"
    replacement = format_array(name, zlib.compress(data, 9))
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"could not replace {name}")

replace_array("kSplashLogo24BitCompressedImage", bgr)
replace_array("kSplashLogo8BitCompressedImage", indexed)
header.write_text(text)
print(header)
