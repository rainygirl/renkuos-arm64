#!/usr/bin/env python3
# Hand-rolled HVIF encoder for R AppMarket's icon: a BeOS-style diamond
# (rhombus) basket -- the classic isometric-viewed-from-above container
# shape -- with a generic app tile nested inside it.
#
# Byte format reverse-engineered directly from Haiku's own parser
# (src/libs/icon/flat_icon/FlatIconImporter.cpp + FlatIconFormat.cpp) --
# not from the (undocumented-for-writing) spec. Only the subset actually
# used here (solid-color styles, straight-line closed paths, plain
# path-source shapes) is implemented.
import struct

def style_solid(r, g, b, a=255):
    return bytes([1, r, g, b, a])  # STYLE_TYPE_SOLID_COLOR

def write_coord(c):
    c = max(-128.0, min(192.0, float(c)))
    if float(int(c)) == c and -32 <= c <= 95:
        return bytes([int(c) + 32])
    v = int((c + 128.0) * 102.0) | 32768
    return bytes([(v >> 8) & 0xFF, v & 0xFF])

def path_closed_straight(points):
    # PATH_FLAG_CLOSED (1<<1) | PATH_FLAG_NO_CURVES (1<<3) = 10
    out = bytes([10, len(points)])
    for x, y in points:
        out += write_coord(x) + write_coord(y)
    return out

def shape_path_source(style_index, path_indices):
    out = bytes([10, style_index, len(path_indices)])  # SHAPE_TYPE_PATH_SOURCE
    out += bytes(path_indices)
    out += bytes([0])  # shape flags: none
    return out

def diamond(cx, cy, r):
    return [(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)]

# Canvas ~0-64, centered at (32, 32). Three concentric diamonds: the
# basket's outer rim, its darker inner well, and a bright app tile
# nested inside that well -- drawn back to front in that order.
outer_rim = diamond(32, 32, 26)
inner_well = diamond(32, 32, 16)
app_tile = diamond(32, 32, 9)

styles = [
    style_solid(0xB2, 0x80, 0x53),  # outer rim: medium brown (basket wicker)
    style_solid(0x6E, 0x4B, 0x2D),  # inner well: dark brown (basket interior)
    style_solid(0x42, 0x85, 0xC3),  # app tile: bright blue
]

paths = [
    path_closed_straight(outer_rim),
    path_closed_straight(inner_well),
    path_closed_straight(app_tile),
]

shapes = [
    shape_path_source(0, [0]),
    shape_path_source(1, [1]),
    shape_path_source(2, [2]),
]

out = b"ncif"
out += bytes([len(styles)]) + b"".join(styles)
out += bytes([len(paths)]) + b"".join(paths)
out += bytes([len(shapes)]) + b"".join(shapes)

with open("/tmp/rappmarket-icon.hvif", "wb") as f:
    f.write(out)

print(len(out), "bytes written")
print(out.hex())
