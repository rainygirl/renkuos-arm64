#!/usr/bin/env python3
# Hand-rolled HVIF encoder for a simple BeOS/Haiku-style tilted box icon:
# three flat-shaded parallelogram faces (top/left/right) forming an
# isometric cube, the classic "generic application" icon motif.
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

# Isometric box, canvas ~0-64. Front vertical edge shared by left/right
# faces runs from (32,40) to (32,58); apex at (32,8).
P_TOP   = (32, 8)
P_LEFT  = (8, 24)
P_RIGHT = (56, 24)
P_FRONT_TOP = (32, 40)
P_FRONT_BOT = (32, 58)
P_LEFT_BOT  = (8, 42)
P_RIGHT_BOT = (56, 42)

top_face   = [P_TOP, P_RIGHT, P_FRONT_TOP, P_LEFT]
left_face  = [P_LEFT, P_FRONT_TOP, P_FRONT_BOT, P_LEFT_BOT]
right_face = [P_RIGHT, P_RIGHT_BOT, P_FRONT_BOT, P_FRONT_TOP]

styles = [
    style_solid(0xD8, 0xE4, 0xF2),  # top: light blue-gray
    style_solid(0x8F, 0xB0, 0xD9),  # left: medium blue
    style_solid(0x4C, 0x6F, 0xA5),  # right: dark blue
]

paths = [
    path_closed_straight(top_face),
    path_closed_straight(left_face),
    path_closed_straight(right_face),
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
