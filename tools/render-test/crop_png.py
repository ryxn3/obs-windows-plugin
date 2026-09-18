"""crop_png.py in.png out.png x y w h   -- crops an 8-bit RGBA/RGB PNG (stdlib only)."""
import zlib, struct, sys


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        ln, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + ln]
        if t == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
        elif t == b"IDAT":
            idat += body
        pos += 12 + ln
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def write_png(path, w, h, bpp, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    ct = 6 if bpp == 4 else 2
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ct, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


if __name__ == '__main__':
    w, h, bpp, rows = read_png(sys.argv[1])
    x, y, cw, ch = map(int, sys.argv[3:7])
    out = [r[x * bpp:(x + cw) * bpp] for r in rows[y:y + ch]]
    write_png(sys.argv[2], len(out[0]) // bpp, len(out), bpp, out)
