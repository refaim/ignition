#!/usr/bin/env python3
"""make_icon.py - draws the port's icon (a lightning bolt on a red disc),
not the game's art, so it can ship with every package.

    python3 tools/make_icon.py platforms/linux/ignition.png 256
"""

import struct
import sys
import zlib

BOLT = [(0.58, 0.10), (0.30, 0.55), (0.48, 0.55), (0.40, 0.90), (0.72, 0.42), (0.53, 0.42),
        (0.64, 0.10)]


def inside(poly, x, y):
    c = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c


def pixel(x, y):
    dx, dy = x - 0.5, y - 0.5
    d = (dx * dx + dy * dy) ** 0.5
    if d > 0.48:
        return (0, 0, 0, 0)
    if d > 0.43:
        return (240, 240, 240, 255)
    if inside(BOLT, x, y):
        return (255, 214, 0, 255)
    return (200, 16, 46, 255)


def main():
    out, size = sys.argv[1], int(sys.argv[2])
    ss = 4
    rows = []
    for py in range(size):
        row = bytearray([0])
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    p = pixel((px + (sx + 0.5) / ss) / size, (py + (sy + 0.5) / ss) / size)
                    for k in range(4):
                        acc[k] += p[k]
            row += bytes(v // (ss * ss) for v in acc)
        rows.append(bytes(row))

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows), 9))
    png += chunk(b'IEND', b'')
    with open(out, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    main()
