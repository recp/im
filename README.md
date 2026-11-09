# 🎞 Digital Image Processing ( In-Progress )

Image loader library to fast load images, it will be optimized for render engines, image processing and more...

It will provide lot of options, wait until first release. 

It will try to support **vector**, **raster** and **raw** images as possible it can be, iternally or with extension library/libraries...

Companion Libaries:

- [filt](https://github.com/recp/filt) - Image filter
- [vision](https://github.com/recp/vision) - Computer vision

TODO
-----
- [ ] Streaming Decode + Progresive Support 
- [ ] GPU support: what follows the entropy decoding, the same work for every block or sample (JPEG's IDCT,
      upsampling and colour; JPEG 2000's wavelet and colour), on the GPU for big images; the Huffman and MQ
      decoding stays on the CPU's threads
- [x] Cocoa and Win32 helpers
- [x] From memory (`im_load_mem`, `im_info_mem`: JPEG, JPEG 2000, JBIG2; raw CCITT fax), with options: threads,
      a JPEG's colour transform said by its container, CMYK as stored, RGB padded to RGB0, a palette's
      indexes, 8 bits by OpenJPEG's rounding, the opacity apart (or not decoded), decoded ½, ¼ or ⅛ the
      size, a region alone (`im_option_region`), a memory limit (`im_option_memory`: `IM_ENOMEM` before
      allocating what wouldn't fit), a page of a file of them (`IM_OPTION_PAGE`; `ImInfo.pages`), the bytes
      the decode read (`im_option_bytes_read`: where an image inline in other data ends)
- [x] Formats picked at build time: `-DIM_FORMATS="jpg;jp2"` (default `all`); only png needs the
      defl and huff submodules
- [x] WebAssembly (WASI): wasi-sdk's toolchain file, `-DIM_STATIC=ON` (`-msimd128` for SIMD128);
      one thread, files read (no mmap)
- [x] The codecs alone (`-DIM_CODECS_ONLY=ON`): JPEG, JPEG 2000, CCITT fax and JBIG2 from memory (`im_load_mem`,
      `im_info_mem`, `im_free`), no file layer, other formats, submodules or frameworks; `-DIM_NO_THREADS=ON`
      (WASI without `-pthread` on its own) leaves the thread layer out, decoders on the caller's thread.
      Built by hand: `src/load.c`, `src/io/jpg/{jpg,arith,huff,idct,sample}.c`, `src/io/jp2/jp2.c`,
      `src/io/ccitt/ccitt.c`, `src/io/jbig2/jbig2.c`, `src/thread/pool.c`, and `src/posix/thread.c` or `src/win/thread.c` (none without threads), with
      `IM_CODECS_ONLY=1`, `IM_STATIC`, `include/` and `src/` on the include path
- [x] `-DIM_PREFIX=app_`: a codec-only build's every symbol under that prefix (callers' names unchanged,
      `im.h` renames them too; the same define for im's sources and the caller's), so it links beside
      another copy of im; `test/check-prefix.sh` checks none is left bare
- [x] The caller's threads (`im_option_runner`): a decode's parallel work on them, none of im's started
- [x] Into the caller's memory (`im_option_dest`): rows at its stride, no allocation for the pixels
- [x] JPEG (libjpeg-turbo's pixels, to the byte)
  - [x] baseline, extended (8 and 12 bits), progressive; Huffman and arithmetic coding
  - [x] lossless, 2 to 16 bits; Huffman and arithmetic coding
  - [x] every sampling, restarts, Adobe/JFIF, CMYK and YCCK, ICC profile; up to 10 components (2 and 5 or
        more as they are, libjpeg's JCS_UNKNOWN)
  - [x] cut streams and damage as libjpeg reads them, block smoothing
  - [x] NEON, SSE4.1/AVX2 and WebAssembly SIMD128 IDCT, upsampling, colour
  - [x] on threads: restart intervals, speculative Huffman, progressive scans row by row
  - [x] DNL (the height after the first scan; libjpeg refuses it)
  - [x] hierarchical: DHP's frames one after another, differential frames (DCT, progressive, lossless;
        either coding) on the references grown as EXP says (J.1.1.2's filter)
  - [x] decoded smaller (`IM_OPTION_SCALE`): ½, ¼, ⅛ by a smaller IDCT, libjpeg's scaled decode to the byte
- [x] PNG (In Progress)
  - [x] palette
  - [x] color types
  - [x] 1,2,4,8,16bpp
  - [x] options
  - [x] adam7 interlaced PNGs
  - [x] alpha
  - [x] additional chunks ( more ? )
  - [x] custom unzip/deflate - in progress -
  - [ ] Deflate & IDAT optimization - in progress -
  - [ ] convert to user selected format
- [x] JPEG 2000 (JP2, J2K)
  - [x] Part 1: tiles, precincts, layers, the five progressions, POC, PPM/PPT, SOP/EPH, ROI
  - [x] every code-block style, 5/3 and 9/7, RCT and ICT, subsampling
  - [x] gray, RGB, RGBA, CMYK; 8 and 16 bits; palette, sYCC, ICC profile
  - [x] threads
  - [x] HT (Part 15): HT code-blocks (a tile-component's all; one HT set), the cleanup, SigProp and MagRef
        passes, JPH files; OpenJPEG's samples
  - [ ] Part 2 (Apple's decoder reads it on Apple platforms)
  - [x] decoded smaller (`IM_OPTION_SCALE`): the finest levels left out, OpenJPEG's `reduce` to the sample
  - [ ] SIMD
- [x] CCITT fax, raw (`IM_OPTION_FORMAT` `IM_FILEFORMATTYPE_CCITT`, its parameters by `im_option_ccitt`)
  - [x] T.4 Group 3, one- and two-dimensional (K), T.6 Group 4; EOLs, byte alignment, RTC and EOFB
  - [x] rows given or as many as the data has; damaged rows (the row before again, to the next EOL)
  - [x] a bit a pixel (1 white unless `black_is_1`) or a byte a pixel (`IM_OPTION_EIGHT_BITS`); a region
- [x] JBIG2: a file (its first page), or a stream's embedded segments (`IM_OPTION_FORMAT`
      `IM_FILEFORMATTYPE_JBIG2`, the globals' by `im_option_jbig2_globals`)
  - [x] generic regions: the four templates and the extended one (12 adaptive pixels), adaptive pixels,
        typical prediction, MMR, unknown length
  - [x] symbol dictionaries and text regions: arithmetic and Huffman (custom tables too), refinement and
        aggregation, contexts kept from one dictionary for the next
  - [x] pattern dictionaries and halftones; refinement of the page or of an intermediate region; striped pages
  - [x] a bit a pixel (0 black) or a byte a pixel; a region (regions off it not decoded); a memory limit
- [ ] JXL
  - [x] Apple platform
- [ ] TIFF
- [ ] GIF
- [x] BMP
  - [x] 1bpp, 2bpp, 3bpp, 4bpp, 5bpp, 6bpp, 7bpp, 8bpp, 16bpp, 24bpp, 32bpp (2,3,5,6,7 may not be official)
  - [ ] 64bpp?
  - [x] BITFIELDS, ALPHABITFIELDS. 
  - [x] Promote BITFIELDS to ALPHABITFIELDS if alpha mask is not zero
  - [x] RGB
  - [x] Monochrome
  - [x] RLE8 
  - [x] RLE4
  - [x] CMYK
  - [x] CMYKRLE8
  - [x] CMYKRLE4
  - [ ] JPEG (wait to finish JPEG codec)
  - [ ] PNG (wait to finish PNG codec)
  - [ ] ICC Color profile
  - [ ] HUFFMAN1D
  - [ ] Halftoning
  - [x] RLE24
  - [ ] Option to specify behavior of skipped pixels 
  - [x] DIB file
- [ ] PSD
- [ ] TGA
  - [x] palette
  - [x] BGR to RGB
  - [ ] handle all bits-per-component and bits-per pixel cases
  - [ ] alpha
  - [ ] RLE
- [ ] HDR
- [ ] EXR
- [ ] WebP
- [ ] AVIF
- [ ] HEIF, HEIC, HEVC
  - [x] Apple platform
  - [ ] Windows
  - [ ] Linux or other platform that has the CODEC?
- [x] Netpbm (pgm, ppm, pbm, pam, pfm)
  - [x] Plain pbm
  - [x] Binary pbm
  - [x] Plain pgm
  - [x] Binary pgm
  - [x] Plain ppm
  - [x] Binary ppm
  - [x] pfm (portable floatmap)
  - [x] pfm alpha (pf4) / augmented pfm
  - [x] pam
- [x] QOI
- [ ] KTX
- [ ] DDS
- [ ] DPX?
- [ ] PCX?
- [ ] ECW?
- [ ] PDF
- [ ] SVG with library extension (**im.svg** library)
- [ ] ...
