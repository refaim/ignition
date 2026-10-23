#!/usr/bin/env python3
"""Writes the Vita package's LiveArea images into OUT/:

    icon0.png                      128 x 128
    livearea/contents/bg.png       840 x 500
    livearea/contents/startup.png  280 x 158

Drawn here, a road running to the horizon on a dusk sky, so no art from
the game is kept in the repository. The PNGs are 8 bit paletted, as the
LiveArea needs.

    tools/vita_sce_sys.py OUT
"""

import os
import struct
import sys
import zlib

PALETTE = [
    (16, 12, 32),       # 0 night
    (60, 28, 72),       # 1 dusk
    (196, 72, 52),      # 2 horizon
    (244, 160, 56),     # 3 sun
    (40, 40, 44),       # 4 road
    (236, 228, 200),    # 5 lines
    (28, 64, 36),       # 6 grass
]


def pixel(x, y, w, h):
    horizon = h * 0.55
    if y < horizon:
        t = y / horizon
        sun_x, sun_y, sun_r = w * 0.5, horizon, h * 0.22
        if (x - sun_x) ** 2 + (y - sun_y) ** 2 < sun_r ** 2 and int(y) % 6 < 4:
            return 3
        return 0 if t < 0.35 else 1 if t < 0.8 else 2
    # The road narrows to a point on the horizon.
    d = (y - horizon) / (h - horizon)
    half = d * w * 0.42
    off = x - w * 0.5
    if abs(off) > half:
        return 6
    if abs(off) < max(1.0, half * 0.03) and int(d * 24 + 0.5) % 2 == 0:
        return 5
    if abs(abs(off) - half * 0.92) < max(1.0, half * 0.03):
        return 5
    return 4


def png(path, w, h):
    rows = b"".join(
        b"\0" + bytes(pixel(x, y, w, h) for x in range(w)) for y in range(h))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    data = (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", b"".join(bytes(c) for c in PALETTE))
            + chunk(b"IDAT", zlib.compress(rows, 9))
            + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    png(os.path.join(out, "icon0.png"), 128, 128)
    png(os.path.join(out, "livearea", "contents", "bg.png"), 840, 500)
    png(os.path.join(out, "livearea", "contents", "startup.png"), 280, 158)


if __name__ == "__main__":
    main()
