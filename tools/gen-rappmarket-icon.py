#!/usr/bin/env python3
# Hand-rolled HVIF encoder for R AppMarket's icon: an isometric 3D basket
# (top/left/right faces, same construction Haiku's own R Memo icon uses
# for its book shape -- decoded directly from R Memo's own BEOS:ICON
# attribute to confirm the geometry) with a generic app tile nested in
# its open top, rather than a flat 2D diamond outline.
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

# Isometric cube (same geometry as R Memo's own icon): top face is the
# rhombus formed by the cube's top vertex, left and right corners, and
# the center point where front-left and front-right faces meet; the two
# front faces hang down from that same center point, giving it visible
# height instead of reading as a flat diamond.
top_face   = [(32, 3), (62, 21), (32, 39), (2, 21)]
left_face  = [(2, 21), (32, 39), (32, 53), (2, 35)]
right_face = [(62, 21), (62, 35), (32, 53), (32, 39)]
# A smaller rhombus nested in the open top, same center as top_face,
# scaled to about 0.4x -- the generic app sitting inside the basket.
app_tile   = [(32, 14), (44, 21), (32, 28), (20, 21)]

styles = [
    style_solid(0xD6, 0xAF, 0x82),  # top (rim/opening): light tan
    style_solid(0xA8, 0x7A, 0x4D),  # left face: medium brown
    style_solid(0x6E, 0x4B, 0x2D),  # right face: dark brown
    style_solid(0x42, 0x85, 0xC3),  # app tile: bright blue
]

paths = [
    path_closed_straight(left_face),
    path_closed_straight(right_face),
    path_closed_straight(top_face),
    path_closed_straight(app_tile),
]

# Drawn back to front: both side faces, then the top rim, then the app
# tile on top of it so it reads as sitting inside the open basket.
shapes = [
    shape_path_source(1, [0]),
    shape_path_source(2, [1]),
    shape_path_source(0, [2]),
    shape_path_source(3, [3]),
]

out = b"ncif"
out += bytes([len(styles)]) + b"".join(styles)
out += bytes([len(paths)]) + b"".join(paths)
out += bytes([len(shapes)]) + b"".join(shapes)

with open("/tmp/rappmarket-icon.hvif", "wb") as f:
    f.write(out)

print(len(out), "bytes written")
print(out.hex())
