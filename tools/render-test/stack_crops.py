"""stack_crops.py out.png x y w h a.png b.png ...  -- crop the same region from several PNGs and stack vertically."""
import sys
from crop_png import read_png, write_png
x, y, cw, ch = map(int, sys.argv[2:6])
rows_out = []
for f in sys.argv[6:]:
    w, h, bpp, rows = read_png(f)
    for r in rows[y:y + ch]:
        line = bytearray(r[x * bpp:(x + cw) * bpp])
        need = cw * bpp - len(line)
        if need > 0:
            line += bytes([0, 0, 0, 255][:bpp]) * (need // bpp)
        rows_out.append(line)
write_png(sys.argv[1], cw, len(rows_out), 4, rows_out)
