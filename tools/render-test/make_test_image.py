import zlib, struct, math, sys

W = int(sys.argv[2]) if len(sys.argv) > 2 else 320
H = int(sys.argv[3]) if len(sys.argv) > 3 else 240
VARIANT = int(sys.argv[4]) if len(sys.argv) > 4 else 0
rows = []
for y in range(H):
    row = bytearray([0])
    for x in range(W):
        r = int(80 + 120 * x / W)
        g = int(120 + 80 * y / H)
        b = int(200 - 100 * x / W)
        dx, dy = x - W / 2, y - H / 2
        d = math.hypot(dx, dy * 1.1)
        if d < H * 0.29:
            r, g, b = 235, 190, 160
        if (abs(dx + 25) < 6 and abs(dy + 15) < 8) or (abs(dx - 25) < 6 and abs(dy + 15) < 8):
            r, g, b = 30, 30, 30
        if abs(dy - 28) < 3 and abs(dx) < 22:
            r, g, b = 150, 40, 40
        if x % (W // 8) == 0 or y % (H // 6) == 0:
            r, g, b = 255, 255, 255
        if x < 3 or y < 3 or x >= W - 3 or y >= H - 3:
            r, g, b = 255, 0, 255
        if VARIANT == 1:
            r, g, b = 255 - b, r // 2 + 40, 255 - g
        row += bytes((r, g, b))
    rows.append(bytes(row))
raw = b"".join(rows)


def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
