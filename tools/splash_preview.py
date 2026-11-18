#!/usr/bin/env python3
"""splash_preview.py <glcdfont.c> <outdir>: the 128x64 boot splash as the OLED would draw it, white on black, from the
MeshContinuum mark's geometry (rasterised here with 8x8 supersampling and a 50% coverage threshold) and the Adafruit GFX
6x8 font the display drivers use. Writes splash_<variant>_128x64.png and splash_<variant>_8x.png. No dependencies."""
import math, os, re, struct, sys, zlib

W, H = 128, 64

def load_font(path):
    src = open(path).read()
    body = src[src.index("{") + 1:src.index("};")]
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]

def text(px, font, x, y, s):
    for ch in s:
        c = ord(ch)
        for col in range(5):
            bits = font[c * 5 + col]
            for row in range(8):
                if bits >> row & 1 and 0 <= x + col < W and 0 <= y + row < H:
                    px[y + row][x + col] = 1
        x += 6

def bezier(p0, p1, p2, p3, n=200):
    out = []
    for i in range(n + 1):
        t = i / n
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out

def seg_dist(px_, py_, a, b):
    ax, ay = a; bx, by = b
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((px_ - ax) * dx + (py_ - ay) * dy) / L))
    return math.hypot(px_ - (ax + t * dx), py_ - (ay + t * dy))

def mark(px, ox, oy, size, loop, stroke, nodes, r, scale=1.0):
    """The mark's 64-unit geometry: a loop of four cubic segments, its stroke, four nodes of radius r. scale around 32,32."""
    tf = lambda p: (32 + (p[0] - 32) * scale, 32 + (p[1] - 32) * scale)
    pts = []
    for seg in loop:
        pts += bezier(*[tf(p) for p in seg])
    nodes = [tf(c) for c in nodes]
    hw, rr = stroke * scale / 2, r * scale
    SS = 8
    for j in range(size):
        for i in range(size):
            hit = 0
            for sj in range(SS):
                for si in range(SS):
                    ux = (i + (si + 0.5) / SS) * 64 / size
                    uy = (j + (sj + 0.5) / SS) * 64 / size
                    on = any(math.hypot(ux - cx, uy - cy) <= rr for cx, cy in nodes)
                    if not on:
                        on = min(seg_dist(ux, uy, pts[k], pts[k + 1]) for k in range(0, len(pts) - 1)) <= hw
                    hit += on
            if hit * 2 >= SS * SS:
                px[oy + j][ox + i] = 1

def png(path, px, scale):
    w, h = W * scale, H * scale
    raw = b"".join(b"\x00" + bytes(255 if px[y // scale][x // scale] else 0 for x in range(w)) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

# mark-mono-white.svg: M32 32 C 40 19, 55 19, 55 32 C 55 45, 40 45, 32 32 C 24 19, 9 19, 9 32 C 9 45, 24 45, 32 32; stroke 3.5;
# nodes r 5.25 at (17.5, 22.25) (46.5, 22.25) (46.5, 41.75) (17.5, 41.75)
MONO = dict(loop=[((32, 32), (40, 19), (55, 19), (55, 32)), ((55, 32), (55, 45), (40, 45), (32, 32)),
                  ((32, 32), (24, 19), (9, 19), (9, 32)), ((9, 32), (9, 45), (24, 45), (32, 32))],
            stroke=3.5, nodes=[(17.5, 22.25), (46.5, 22.25), (46.5, 41.75), (17.5, 41.75)], r=5.25)
# mark-small.svg, in one colour: the loop through 3 and 61, stroke 5.5, nodes r 8, all scaled 0.86 about the centre
SMALL = dict(loop=[((32, 32), (42, 15), (61, 15), (61, 32)), ((61, 32), (61, 49), (42, 49), (32, 32)),
                   ((32, 32), (22, 15), (3, 15), (3, 32)), ((3, 32), (3, 49), (22, 49), (32, 32))],
             stroke=5.5, nodes=[(13.75, 19.25), (50.25, 19.25), (50.25, 44.75), (13.75, 44.75)], r=8, scale=0.86)

def splash(font, geom):
    px = [[0] * W for _ in range(H)]
    M = int(os.environ.get("MARK", "24"))
    gap = 11 if M <= 24 else 10
    mark(px, 0, 0, M, **geom)
    text(px, font, M + 4, (M - 8) // 2, "MeshContinuum")
    for x in range(W):
        px[M + (2 if M <= 24 else 0)][x] = 1
    lines = ["https://mecon.cloud/", "MECON 0.3.7", "MeshCore 1.18.0"]   # versions: placeholders for the real ones
    for k, s in enumerate(lines):
        text(px, font, (W - 6 * len(s)) // 2, M + (6 if M <= 24 else 3) + k * gap, s)
    return px

if __name__ == "__main__":
    font, out = load_font(sys.argv[1]), sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for name, geom in (("mono", MONO), ("small", SMALL)):
        px = splash(font, geom)
        png(os.path.join(out, "splash_%s_128x64.png" % name), px, 1)
        png(os.path.join(out, "splash_%s_8x.png" % name), px, 8)
        print(name, "lit pixels:", sum(map(sum, px)))


def mark_c_array(size=32):
    """The mark alone, size x size, as the C initialiser for MeconSplash.h: rows MSB first, as drawBitmap takes them."""
    global W, H
    W0, H0 = W, H
    W, H = size, size
    px = [[0] * W for _ in range(H)]
    mark(px, 0, 0, size, **MONO)
    W, H = W0, H0
    out = []
    for row in px:
        for b in range(0, size, 8):
            v = 0
            for k in range(8):
                v = v << 1 | row[b + k]
            out.append("0x%02x" % v)
    return ",".join(out)
