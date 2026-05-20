#!/usr/bin/env python3
"""Trace the cookie reference. Reference is a JPEG with checkerboard
transparency-substitute background (gray squares of two shades). The cookie
illustration is pure black line art with black-filled chocolate chunks and
small dots, on a white interior.

Strategy: classify each pixel as BLACK (cookie ink) or NON-BLACK. The
checkerboard background and the cookie interior are both non-black; we
ignore distinction. We find the bounding box of BLACK pixels and crop +
resample to 120x120 with majority-vote downsampling.
"""
import struct, sys

with open('/tmp/cookie_ref.bmp', 'rb') as f:
    data = f.read()
assert data[0:2] == b'BM'
pixel_off = struct.unpack('<I', data[10:14])[0]
width = struct.unpack('<i', data[18:22])[0]
height = struct.unpack('<i', data[22:26])[0]
bpp = struct.unpack('<H', data[28:30])[0]
abs_h = abs(height); top_down = (height < 0)
row_bytes = ((bpp * width + 31) // 32) * 4
pixels = data[pixel_off:]
print(f'BMP {width}x{abs_h} bpp={bpp}', file=sys.stderr)

def get_lum(x, y):
    ry = y if top_down else (abs_h - 1 - y)
    off = ry * row_bytes + x * (bpp // 8)
    b = pixels[off + 0]; g = pixels[off + 1]; r = pixels[off + 2]
    return (r + g + b) // 3  # luminance approx

# Threshold: pixels darker than 80 are "black" (cookie ink).
# The checkerboard background uses two shades of gray (around 220 and 240),
# so 80 is well below both.
BLACK_THR = 80

is_black = [[False] * width for _ in range(abs_h)]
for y in range(abs_h):
    for x in range(width):
        if get_lum(x, y) < BLACK_THR:
            is_black[y][x] = True

# Bounding box of black pixels
min_x = width; max_x = -1; min_y = abs_h; max_y = -1
black_count = 0
for y in range(abs_h):
    for x in range(width):
        if is_black[y][x]:
            black_count += 1
            if x < min_x: min_x = x
            if x > max_x: max_x = x
            if y < min_y: min_y = y
            if y > max_y: max_y = y
print(f'black pixels: {black_count}  bbox x[{min_x}..{max_x}] y[{min_y}..{max_y}] -> {max_x-min_x+1}x{max_y-min_y+1}', file=sys.stderr)

logo_w = max_x - min_x + 1
logo_h = max_y - min_y + 1

# Render into 120x120 with small margin
TARGET = 120
MARGIN = 2
avail = TARGET - 2 * MARGIN
scale = avail / max(logo_w, logo_h)
out_w = int(round(logo_w * scale))
out_h = int(round(logo_h * scale))
off_x = (TARGET - out_w) // 2
off_y = (TARGET - out_h) // 2
print(f'scale={scale:.4f} out={out_w}x{out_h} off=({off_x},{off_y})', file=sys.stderr)

# Resample: majority vote per output pixel
out = [[False] * TARGET for _ in range(TARGET)]
for oy in range(out_h):
    for ox in range(out_w):
        sx0 = ox * logo_w / out_w
        sx1 = (ox + 1) * logo_w / out_w
        sy0 = oy * logo_h / out_h
        sy1 = (oy + 1) * logo_h / out_h
        ix0 = int(sx0) + min_x; ix1 = max(int(sx1) + min_x, ix0 + 1)
        iy0 = int(sy0) + min_y; iy1 = max(int(sy1) + min_y, iy0 + 1)
        total = 0; hits = 0
        for yy in range(iy0, min(iy1, abs_h)):
            for xx in range(ix0, min(ix1, width)):
                total += 1
                if is_black[yy][xx]: hits += 1
        # Lower threshold than 50% — keeps thin lines from disappearing
        if total > 0 and hits * 3 >= total:  # ≥ 1/3 = black
            out[off_y + oy][off_x + ox] = True

# ASCII preview
print('Preview (B=black, .=white):', file=sys.stderr)
for y in range(0, TARGET, 2):
    row = ''.join('B' if out[y][x] else '.' for x in range(0, TARGET, 2))
    print(row, file=sys.stderr)

# Pack to 1-bit MSB-first. bit=1 white, bit=0 black.
packed = []
for y in range(TARGET):
    for xb in range(TARGET // 8):
        byte = 0
        for b in range(8):
            x = xb * 8 + b
            if not out[y][x]:
                byte |= (1 << (7 - b))
        packed.append(byte)
assert len(packed) == 1800

with open('/tmp/Logo120.h', 'w') as f:
    f.write('#pragma once\n#include <cstdint>\n\n')
    f.write('// CrumBle boot/sleep logo — chocolate-chip cookie with a bite\n')
    f.write('// taken out. 1-bit silhouette traced from the brand reference\n')
    f.write('// image. 120x120, MSB-first packed, 1800 bytes total.\n')
    f.write('// bit=1 white, bit=0 black.\n')
    f.write('static const uint8_t Logo120[] = {\n    ')
    for i, b in enumerate(packed):
        f.write(f'0x{b:02x}, ')
        if (i + 1) % 19 == 0:
            f.write('\n    ')
    f.write('};\n')
    f.write('static_assert(sizeof(Logo120) == 1800, "Logo120 must be exactly 120x120 / 8 bytes");\n')
print('wrote /tmp/Logo120.h', file=sys.stderr)
