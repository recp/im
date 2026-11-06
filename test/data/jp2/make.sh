#!/bin/sh
# Makes the JPEG 2000 fixtures (ffmpeg 9's encoder made the committed ones)
# from one synthetic 37 × 23 picture: gradients, a ring, stripes. Each is
# OpenJPEG's pixels to the byte as jp2.c reads it.
# Not made here: ppt.j2k, ppm.j2k and parts.j2k are ict-tiles.j2k with its
# packet headers moved into each tile-part's PPT, into the main header's
# PPM, and with each tile in two tile-parts (the first ones before any
# second), and poc.j2k is gray-layers.jp2's packets reordered under a POC
# (RLCP for two resolutions and two layers, then LRCP), written after COD.
# A scratch transcoder did that from the packets' boundaries; each
# decodes, in OpenJPEG too, to its source's pixels.
set -eu
cd "$(dirname "$0")"
python3 - <<'PY'
import math
w, h = 37, 23
px = bytearray()
for y in range(h):
    for x in range(w):
        r = int(255 * x / (w - 1)); g = int(255 * y / (h - 1)); b = 128
        d = math.hypot(x - 18, y - 11)
        if 6 < d < 9: r, g, b = 250, 240, 30
        if (x // 3) % 2 and y > 17: r, g, b = 20, 20, 20
        if x > 30 and y < 5: r, g, b = 255, 255, 255
        px += bytes([r, g, b])
open('src.ppm', 'wb').write(b'P6\n%d %d\n255\n' % (w, h) + px)
PY
enc() {
  name=$1 fmt=$2
  shift 2
  ffmpeg -hide_banner -loglevel error -y -i src.ppm -pix_fmt "$fmt" -c:v jpeg2000 "$@" -frames:v 1 -update 1 -f image2 "$name"
}
enc rct.jp2 rgb24 -pred dwt53
enc ict-tiles.j2k rgb24 -pred dwt97int -format j2k -tile_width 16 -tile_height 16 -prog rpcl -sop 1 -eph 1
enc gray-layers.jp2 gray -pred dwt97int -layer_rates 40,10,2 -prog pcrl
enc sycc420.jp2 yuv420p -pred dwt53
enc palette.jp2 pal8 -pred dwt53
enc alpha.jp2 rgba -pred dwt53 -prog cprl
enc gray16.jp2 gray16le -pred dwt53
# im's own: four components bare (CMYK), gray with opacity, 10 bits
enc cmyk.j2k rgba -pred dwt53 -format j2k
enc gray-alpha.jp2 ya8 -pred dwt97int
enc rgb10.jp2 gbrp10le -pred dwt53
# rct.jp2's bits read with every code-block style
python3 - <<'PY'
d = bytearray(open('rct.jp2', 'rb').read())
d[d.find(b'\xff\x52') + 12] = 0x3F
open('styles.jp2', 'wb').write(d)
PY
# alpha.jp2 saying its opacity is premultiplied (a cdef box in its jp2h)
python3 - <<'PY'
import struct
d, out, pos = open('alpha.jp2', 'rb').read(), bytearray(), 0
while pos + 8 <= len(d):
    n, t = struct.unpack('>I4s', d[pos:pos + 8])
    n = n or len(d) - pos
    box = d[pos:pos + n]
    if t == b'jp2h':
        cdef = struct.pack('>H', 4) + b''.join(struct.pack('>HHH', c, 0, c + 1) for c in range(3)) + struct.pack('>HHH', 3, 2, 0)
        box += struct.pack('>I4s', 8 + len(cdef), b'cdef') + cdef
        box = struct.pack('>I4s', len(box), b'jp2h') + box[8:]
    out += box
    pos += n
open('premult.jp2', 'wb').write(out)
PY
# HT code-blocks (T.814), OpenJPH's encoder (ojph_compress; it writes the
# cleanup passes alone): reversible, wrapped here in a JPH file (its
# ftyp's brand "jph "); irreversible in 16 x 16 tiles, LRCP, 8 x 8 blocks;
# 16-bit gray. Each OpenJPEG's samples, at every reduction too
ojph_compress -i src.ppm -o ht-rev.j2c -reversible true -num_decomps 3 -block_size '{16,16}' >/dev/null
ojph_compress -i src.ppm -o ht-irv-tiles.j2c -qstep 0.01 -num_decomps 2 -tile_size '{16,16}' -block_size '{8,8}' -prog_order LRCP >/dev/null
python3 - <<'PY'
import struct
d = open('src.ppm', 'rb').read()
w, h = 37, 23
px = d[len(d) - w * h * 3:]
g = bytearray()
for i in range(w * h):
    v = (px[3 * i] * 7 + px[3 * i + 1] * 20 + px[3 * i + 2] * 3) * 65535 // (255 * 30)
    g += bytes([v >> 8, v & 255])
open('g16.pgm', 'wb').write(b'P5\n%d %d\n65535\n' % (w, h) + g)
cs = open('ht-rev.j2c', 'rb').read()
def box(t, b): return struct.pack('>I4s', 8 + len(b), t) + b
ihdr = struct.pack('>IIHBBBB', h, w, 3, 7, 7, 0, 0)
colr = struct.pack('>BBBI', 1, 0, 0, 16)
open('ht-rev.jph', 'wb').write(box(b'jP  ', b'\r\n\x87\n') + box(b'ftyp', b'jph ' + struct.pack('>I', 0) + b'jph ')
                               + box(b'jp2h', box(b'ihdr', ihdr) + box(b'colr', colr)) + box(b'jp2c', cs))
PY
ojph_compress -i g16.pgm -o ht-gray16.j2c -reversible true -num_decomps 2 >/dev/null
rm -f src.ppm g16.pgm ht-rev.j2c
# imageio.jp2: ImageIO's encoder (sips; the committed one macOS 27's) on a
# 640 × 480 picture, large enough for the decoder's threads, with an sRGB
# profile in its colr box
python3 - <<'PY'
import math
w, h = 640, 480
px = bytearray()
for y in range(h):
    for x in range(w):
        r = int(255 * x / (w - 1)); g = int(255 * y / (h - 1)); b = int(128 + 100 * math.sin(x / 40.0) * math.cos(y / 30.0))
        d = math.hypot(x - 320, y - 240)
        if 100 < d < 150: r, g, b = 250, 240, 30
        if (x // 7) % 2 and y > 380: r, g, b = 20, 20, 20
        if x > 520 and y < 90: r, g, b = 255, 255, 255
        px += bytes([r, g, b])
open('big.ppm', 'wb').write(b'P6\n%d %d\n255\n' % (w, h) + px)
PY
ffmpeg -hide_banner -loglevel error -y -i big.ppm big.png
sips -s format jp2 -s formatOptions 20 big.png --out imageio.jp2 >/dev/null
rm big.ppm big.png
