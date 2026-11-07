#!/bin/sh
# Makes the CCITT fax fixtures: synthetic bitmaps (a fixed generator, so
# the same each time) encoded as T.4 Group 3 (1-D, 2-D) and T.6 Group 4
# with the parameters test_load.c's ccitt test reads them by. `make.sh pbm`
# also writes each source as a PBM (1 black) beside it.
set -eu
cd "$(dirname "$0")"
python3 - "$@" <<'PY'
import sys

def table(text):
    return {int(v): bits for bits, v in (item.split(":") for item in text.split())}

# T.4 Tables 2, 3 (terminating 0-63, makeup 64-1728) and 3a (1792-2560)
WHITE = table("""00110101:0 000111:1 0111:2 1000:3 1011:4 1100:5 1110:6 1111:7 10011:8 10100:9 00111:10
01000:11 001000:12 000011:13 110100:14 110101:15 101010:16 101011:17 0100111:18 0001100:19 0001000:20
0010111:21 0000011:22 0000100:23 0101000:24 0101011:25 0010011:26 0100100:27 0011000:28 00000010:29
00000011:30 00011010:31 00011011:32 00010010:33 00010011:34 00010100:35 00010101:36 00010110:37
00010111:38 00101000:39 00101001:40 00101010:41 00101011:42 00101100:43 00101101:44 00000100:45
00000101:46 00001010:47 00001011:48 01010010:49 01010011:50 01010100:51 01010101:52 00100100:53
00100101:54 01011000:55 01011001:56 01011010:57 01011011:58 01001010:59 01001011:60 00110010:61
00110011:62 00110100:63 11011:64 10010:128 010111:192 0110111:256 00110110:320 00110111:384
01100100:448 01100101:512 01101000:576 01100111:640 011001100:704 011001101:768 011010010:832
011010011:896 011010100:960 011010101:1024 011010110:1088 011010111:1152 011011000:1216
011011001:1280 011011010:1344 011011011:1408 010011000:1472 010011001:1536 010011010:1600
011000:1664 010011011:1728""")
BLACK = table("""0000110111:0 010:1 11:2 10:3 011:4 0011:5 0010:6 00011:7 000101:8 000100:9 0000100:10
0000101:11 0000111:12 00000100:13 00000111:14 000011000:15 0000010111:16 0000011000:17 0000001000:18
00001100111:19 00001101000:20 00001101100:21 00000110111:22 00000101000:23 00000010111:24
00000011000:25 000011001010:26 000011001011:27 000011001100:28 000011001101:29 000001101000:30
000001101001:31 000001101010:32 000001101011:33 000011010010:34 000011010011:35 000011010100:36
000011010101:37 000011010110:38 000011010111:39 000001101100:40 000001101101:41 000011011010:42
000011011011:43 000001010100:44 000001010101:45 000001010110:46 000001010111:47 000001100100:48
000001100101:49 000001010010:50 000001010011:51 000000100100:52 000000110111:53 000000111000:54
000000100111:55 000000101000:56 000001011000:57 000001011001:58 000000101011:59 000000101100:60
000001011010:61 000001100110:62 000001100111:63 0000001111:64 000011001000:128 000011001001:192
000001011011:256 000000110011:320 000000110100:384 000000110101:448 0000001101100:512
0000001101101:576 0000001001010:640 0000001001011:704 0000001001100:768 0000001001101:832
0000001110010:896 0000001110011:960 0000001110100:1024 0000001110101:1088 0000001110110:1152
0000001110111:1216 0000001010010:1280 0000001010011:1344 0000001010100:1408 0000001010101:1472
0000001011010:1536 0000001011011:1600 0000001100100:1664 0000001100101:1728""")
LONG = table("""00000001000:1792 00000001100:1856 00000001101:1920 000000010010:1984 000000010011:2048
000000010100:2112 000000010101:2176 000000010110:2240 000000010111:2304 000000011100:2368
000000011101:2432 000000011110:2496 000000011111:2560""")
EOL = "000000000001"
MODES = {"P": "0001", "H": "001", 0: "1", 1: "011", 2: "000011", 3: "0000011",
         -1: "010", -2: "000010", -3: "0000010"}

def run_code(n, black):
    t, out = BLACK if black else WHITE, ""
    while n >= 2560 + 64:
        out += LONG[2560]
        n -= 2560
    if n >= 64:
        m = n // 64 * 64
        out += t[m] if m <= 1728 else LONG[m]
        n -= m
    return out + t[n]

def changes(row, w):
    c, prev = [], 0
    for x in range(w):
        if row[x] != prev:
            c.append(x)
            prev = row[x]
    return c + [w, w]

def enc_1d(row, w):
    out, x, black = "", 0, 0
    while x < w:
        n = 0
        while x + n < w and row[x + n] == black:
            n += 1
        out += run_code(n, black)
        x += n
        black ^= 1
    return out

def enc_2d(row, ref, w):
    cur, rc = changes(row, w), changes(ref, w)
    nxt = lambda c, after: next((v for v in c if v > after), w)
    def colour(c, x):  # the colour from x on: the count of changes at or before it
        return sum(1 for v in c if v <= x and v < w) & 1
    out, a0, black = "", -1, 0
    while a0 < w:
        a1 = nxt(cur, a0)
        a2 = nxt(cur, a1) if a1 < w else w
        b1 = next((v for v in rc if a0 < v < w and colour(rc, v) != black), w)
        b2 = nxt(rc, b1) if b1 < w else w
        if b2 < a1:
            out += MODES["P"]
            a0 = b2
        elif abs(a1 - b1) <= 3:
            out += MODES[a1 - b1]
            a0 = a1
            black ^= 1
        else:
            out += MODES["H"] + run_code(a1 - max(a0, 0), black) + run_code(a2 - a1, black ^ 1)
            a0 = a2
    return out

def encode(rows, w, k, eol, align, eob):
    s = ""
    pad = lambda s: s + "0" * (-len(s) % 8)
    ref = [0] * w
    for y, row in enumerate(rows):
        if align:
            s = pad(s)  # the row (its EOL first) on a byte
        if k >= 0 and eol:
            s += EOL
        if k > 0:
            two = y % k != 0
            s += ("0" + enc_2d(row, ref, w)) if two else ("1" + enc_1d(row, w))
        elif k == 0:
            s += enc_1d(row, w)
        else:
            s += enc_2d(row, ref, w)
        ref = row
    if eob:
        s += EOL * 2 if k < 0 else (EOL + ("1" if k > 0 else "")) * 6
    s = pad(s)
    return bytes(int(s[i:i + 8], 2) for i in range(0, len(s), 8))

class Lcg:
    def __init__(self, seed):
        self.s = seed
    def next(self, n):  # 0 .. n-1
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        return (self.s >> 33) % n

def bitmap(kind, w, h, seed):
    r = Lcg(seed)
    rows = [[0] * w for _ in range(h)]
    if kind == "noise":  # a pixel in four black
        rows = [[1 if r.next(4) == 0 else 0 for _ in range(w)] for _ in range(h)]
    elif kind == "text":  # blocks like letters
        for _ in range(w * h // 60 + 1):
            x0, y0 = r.next(w), r.next(h)
            for y in range(y0, min(h, y0 + 1 + r.next(6))):
                for x in range(x0, min(w, x0 + 1 + r.next(12))):
                    rows[y][x] = 1
    elif kind == "lines":  # stripes, rows the row before again
        for y in range(h):
            if y == 0 or r.next(3) == 0:
                p = 1 + r.next(9)
                rows[y] = [(x // p) & 1 for x in range(w)]
            else:
                rows[y] = list(rows[y - 1])
    elif kind == "long":  # long runs: makeup codes, extended ones past 1792
        for y in range(h):
            a, b = sorted((r.next(w + 1), r.next(w + 1)))
            rows[y] = [1 if a <= x < b else 0 for x in range(w)]
        rows[0] = [1] * w
    return rows

# name, k, columns, rows, EOLs, byte-aligned, an end of block, black is 1, picture, seed
CASES = [
    ("g4",          -1,  257,  11, 0, 0, 1, 0, "text",  1),
    ("g4-align",    -1,  100,  17, 0, 1, 1, 1, "noise", 2),
    ("g3-1d-eol",    0,   31,  17, 1, 0, 1, 0, "lines", 3),
    ("g3-1d",        0,   64,  17, 0, 0, 0, 0, "noise", 4),
    ("g3-2d",        4,  100,  17, 1, 1, 1, 0, "text",  5),
    ("g3-2d-align", 1000,   1, 116, 1, 1, 0, 0, "noise", 6),
    ("black1",       4, 1378,  17, 0, 0, 0, 1, "long",  7),
    ("fax",         -1, 1728,  17, 0, 0, 1, 0, "text",  8),
    ("wide",         2, 3000,   9, 1, 0, 1, 0, "long",  9),
]
for name, k, w, h, eol, align, eob, black1, kind, seed in CASES:
    rows = bitmap(kind, w, h, seed)
    open(name + ".fax", "wb").write(encode(rows, w, k, eol, align, eob))
    if "pbm" in sys.argv[1:]:
        bits = bytes(sum(row[x] << (7 - x % 8) for x in range(x0, min(w, x0 + 8)))
                     for row in rows for x0 in range(0, w, 8))
        open(name + ".pbm", "wb").write(b"P4\n%d %d\n" % (w, h) + bits)
PY
