#!/bin/sh
# Makes the JPEG XL fixtures with libjxl's cjxl (0.14's made the committed
# ones), lossless, from two synthetic 37 × 23 pictures the test computes
# too: rgb.jxl a bare codestream, rgba.jxl in the container (ISO BMFF) with
# an opacity channel.
set -eu
cd "$(dirname "$0")"
CJXL=${CJXL:-cjxl}
python3 - <<'PY'
w, h = 37, 23
rgb = bytearray()
rgba = bytearray()
for y in range(h):
    for x in range(w):
        r, g, b = (x * 7) & 255, (y * 11) & 255, ((x + y) * 3) & 255
        a = 255 if x < 20 else (x * 9 + y) & 255
        rgb += bytes([r, g, b])
        rgba += bytes([r, g, b, a])
open('rgb.ppm', 'wb').write(b'P6\n%d %d\n255\n' % (w, h) + rgb)
open('rgba.pam', 'wb').write(b'P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n' % (w, h) + rgba)
PY
"$CJXL" -d 0 -e 7 --container=0 rgb.ppm rgb.jxl
"$CJXL" -d 0 -e 7 --container=1 rgba.pam rgba.jxl
rm -f rgb.ppm rgba.pam
