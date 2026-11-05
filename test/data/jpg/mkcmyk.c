/* CMYK and YCCK JPEGs from a synthetic picture: TurboJPEG's encoder */
#include <turbojpeg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(int argc, char **argv) {
  const char *dir = argv[1];
  int sizes[][2] = { { 37, 23 }, { 255, 129 }, { 801, 601 } };
  for (int si = 0; si < 3; si++) {
    int w = sizes[si][0], h = sizes[si][1];
    unsigned char *px = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
      unsigned char *p = px + ((size_t)y * w + x) * 4;
      p[0] = (unsigned char)(255 * x / (w > 1 ? w - 1 : 1)); p[1] = (unsigned char)(255 * y / (h > 1 ? h - 1 : 1));
      p[2] = (unsigned char)(128 + 100 * sin(x / 5.0) * cos(y / 4.0)); p[3] = (unsigned char)((x * 7 + y * 3) % 256);
    }
    for (int cs = 0; cs < 2; cs++) for (int ss = 0; ss < 2; ss++) {
      tjhandle t = tj3Init(TJINIT_COMPRESS);
      unsigned char *out = NULL; size_t n = 0; char name[512];
      tj3Set(t, TJPARAM_QUALITY, 85);
      tj3Set(t, TJPARAM_COLORSPACE, cs ? TJCS_YCCK : TJCS_CMYK);
      tj3Set(t, TJPARAM_SUBSAMP, ss ? TJSAMP_420 : TJSAMP_444);
      if (tj3Compress8(t, px, w, 0, h, TJPF_CMYK, &out, &n) < 0) { fprintf(stderr, "%s\n", tj3GetErrorStr(t)); return 1; }
      snprintf(name, sizeof name, "%s/%s-%dx%d-%s.jpg", dir, cs ? "ycck" : "cmyk", w, h, ss ? "420" : "444");
      FILE *f = fopen(name, "wb"); fwrite(out, 1, n, f); fclose(f);
      /* the same without its Adobe marker (straight CMYK, not inverted) */
      if (!cs) {
        size_t i; snprintf(name, sizeof name, "%s/cmyk-noadobe-%dx%d-%s.jpg", dir, w, h, ss ? "420" : "444");
        f = fopen(name, "wb");
        for (i = 0; i < n; ) {
          if (out[i] == 0xFF && out[i + 1] == 0xEE) { size_t len = (size_t)out[i + 2] << 8 | out[i + 3]; i += 2 + len; continue; }
          if (out[i] == 0xFF && out[i + 1] == 0xDA) { fwrite(out + i, 1, n - i, f); break; }
          fputc(out[i], f); i++;
        }
        fclose(f);
      }
      tj3Free(out); tj3Destroy(t);
    }
    free(px);
  }
  return 0;
}
