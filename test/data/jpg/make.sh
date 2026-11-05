#!/bin/zsh
# Makes the JPEG fixtures with libjpeg-turbo 3.2's cjpeg (the committed ones
# 3.2.1's) from a synthetic 37 × 23 picture (and a 7 × 5 one): gradients,
# a ring, stripes, a little noise; CMYK and YCCK with its TurboJPEG
# encoder (mkcmyk.c); threads.jpg from macOS's "iMac Blue" desktop picture
# at 777 × 777, a restart a row; two with a DNL for their height. Each
# decodes to libjpeg-turbo's pixels to the byte (its default decompression,
# the C IDCT for the cut ones; the DNL ones their twins', libjpeg refusing
# DNL).
set -eu
cd ${0:A:h}
python3 - <<'PY'
import math, random
random.seed(7)
def pic(name, w, h):
    px = bytearray()
    for y in range(h):
        for x in range(w):
            r = int(255 * x / max(1, w - 1)); g = int(255 * y / max(1, h - 1))
            b = int(128 + 100 * math.sin(x / 5.0) * math.cos(y / 4.0))
            d = math.hypot(x - w / 2, y - h / 2)
            if w / 6 < d < w / 4: r, g, b = 250, 240, 30
            if (x // 3) % 2 and y > h * 0.8: r, g, b = 20, 20, 20
            n = random.randint(-12, 12)
            px += bytes([min(255, max(0, v + n)) for v in (r, g, b)])
    open(name, 'wb').write(b'P6\n%d %d\n255\n' % (w, h) + px)
for (w, h) in [(1, 1), (2, 3), (7, 5), (17, 13), (37, 23)]:
    pic('s%dx%d.ppm' % (w, h), w, h)
PY
s=s37x23.ppm
cjpeg -sample 2x2 -quality 75 -outfile 420.jpg $s
cjpeg -sample 1x1 -quality 75 -outfile 444.jpg $s
cjpeg -sample 2x1 -quality 75 -outfile 422.jpg $s
cjpeg -sample 1x2 -quality 75 -outfile 440.jpg $s
cjpeg -sample 4x1 -quality 75 -outfile 411.jpg $s
cjpeg -sample 3x1 -quality 75 -outfile 311.jpg $s
cjpeg -sample 2x2,1x2,2x1 -quality 75 -outfile mixed.jpg $s
cjpeg -sample 2x2 -quality 75 -outfile tiny.jpg s7x5.ppm
cjpeg -progressive -outfile prog.jpg $s
cjpeg -progressive -restart 2 -outfile prog-rst.jpg $s
cjpeg -restart 1B -sample 1x1 -outfile rst.jpg $s
cjpeg -grayscale -outfile gray.jpg $s
cjpeg -rgb -outfile rgb.jpg $s
cjpeg -precision 12 -outfile p12.jpg $s
cjpeg -precision 12 -progressive -sample 2x1 -outfile p12-prog.jpg $s
cjpeg -optimize -outfile opt.jpg $s
cjpeg -arithmetic -sample 2x2 -outfile arith.jpg $s
cjpeg -arithmetic -progressive -outfile arith-prog.jpg $s
cjpeg -arithmetic -restart 1 -outfile arith-rst.jpg $s
cjpeg -arithmetic -precision 12 -outfile arith-p12.jpg $s
cjpeg -lossless 1 -outfile ll1.jpg $s
cjpeg -lossless 7,4 -outfile ll7-pt4.jpg $s
cjpeg -lossless 1 -precision 2 -outfile ll-p2.jpg $s
cjpeg -lossless 1 -precision 12 -outfile ll-p12.jpg $s
cjpeg -lossless 1 -precision 16 -outfile ll-p16.jpg $s
cjpeg -lossless 1 -sample 2x2 -outfile ll-420.jpg $s
cjpeg -lossless 1 -restart 1 -outfile ll-rst.jpg $s
# cut short: the rest of the baseline one grey, the progressive one smoothed
head -c 640 prog.jpg > prog-cut.jpg
head -c 700 420.jpg > 420-cut.jpg
# no height in the frame, a DNL after the first scan giving it (B.2.5):
# 420.jpg's and prog.jpg's pixels (libjpeg refuses DNL)
python3 - <<'PY'
def dnl(src, dst):
    d = bytearray(open(src, 'rb').read()); i = 2; sof = None
    while True:
        m = d[i + 1]; L = d[i + 2] << 8 | d[i + 3]
        if m in (0xC0, 0xC1, 0xC2): sof = i + 4
        if m == 0xDA: break
        i += 2 + L
    j = i + 2 + L
    while not (d[j] == 0xFF and d[j + 1] not in (0x00, 0xFF) and not 0xD0 <= d[j + 1] <= 0xD7): j += 1
    h = d[sof + 1] << 8 | d[sof + 2]
    d[sof + 1] = d[sof + 2] = 0
    d[j:j] = bytes([0xFF, 0xDC, 0, 4, h >> 8, h & 0xFF])
    open(dst, 'wb').write(d)
dnl('420.jpg', 'dnl.jpg')
dnl('prog.jpg', 'dnl-prog.jpg')
PY
# CMYK (Adobe's, inverted), YCCK, and CMYK without the Adobe marker
mkdir -p cmyk
cc -O2 -I/opt/homebrew/include mkcmyk.c -L/opt/homebrew/lib -lturbojpeg -lm -o mkcmyk
./mkcmyk cmyk
mv cmyk/cmyk-37x23-444.jpg cmyk.jpg
mv cmyk/ycck-37x23-420.jpg ycck.jpg
mv cmyk/cmyk-noadobe-37x23-444.jpg cmyk-plain.jpg
rm -rf cmyk mkcmyk
# a picture large enough for threads, a restart each MCU row
sips -s format png "/System/Library/Desktop Pictures/iMac Blue.heic" --out big.png >/dev/null
sips --resampleWidth 777 big.png --out big.png >/dev/null
cjpeg -restart 1 -outfile threads.jpg big.png
rm -f big.png s*.ppm
