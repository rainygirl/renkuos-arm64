#!/usr/bin/env python3
# Hand-rolled HVIF encoder for R AppMarket's icon: a basket (the "market"
# part) with a generic app-icon tile sitting inside it, peeking out above
# the rim.
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

# Canvas ~0-64. Drawn back-to-front: the "app" tile goes down first so the
# basket's rim/body, painted after, cover its lower half -- only the top
# peeks out above the rim, like an item sitting inside a basket.
app_tile  = [(22, 8), (42, 8), (42, 30), (22, 30)]
rim       = [(10, 26), (54, 26), (54, 36), (10, 36)]
body      = [(14, 36), (50, 36), (44, 58), (20, 58)]

styles = [
    style_solid(0x42, 0x85, 0xC3),  # app tile: bright blue
    style_solid(0xA8, 0x7A, 0x4D),  # rim: medium brown
    style_solid(0xD6, 0xAF, 0x82),  # body: light tan
]

paths = [
    path_closed_straight(app_tile),
    path_closed_straight(rim),
    path_closed_straight(body),
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
