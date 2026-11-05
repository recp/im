#!/bin/sh
# Makes the JPEG 2000 fixtures (ffmpeg 9's encoder made the committed ones)
# from one synthetic 37 × 23 picture: gradients, a ring, stripes. Each is
# PDFium's (OpenJPEG's) pixels to the byte as the decoder reads it (the
# PDF engine's, whose test data these are, and jp2.c's).
# Not made here: ppt.j2k, ppm.j2k and parts.j2k are ict-tiles.j2k with its
# packet headers moved into each tile-part's PPT, into the main header's
# PPM, and with each tile in two tile-parts (the first ones before any
# second), and poc.j2k is gray-layers.jp2's packets reordered under a POC
# (RLCP for two resolutions and two layers, then LRCP), written after COD.
# A scratch transcoder did that from the engine's packet boundaries; each
# decodes, in PDFium too, to its source's pixels.
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
# rct.jp2's bits read with every code-block style
python3 - <<'PY'
d = bytearray(open('rct.jp2', 'rb').read())
d[d.find(b'\xff\x52') + 12] = 0x3F
open('styles.jp2', 'wb').write(d)
PY
rm src.ppm
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
