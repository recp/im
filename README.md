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
- [ ] GPU support
- [x] Cocoa and Win32 helpers
- [x] From memory (`im_load_mem`: JPEG, JPEG 2000), with what a PDF asks: threads, a JPEG's colour
      transform (DCTDecode's /ColorTransform), CMYK as stored, RGB padded to RGB0, a palette's indexes
- [x] JPEG (libjpeg-turbo's pixels, to the byte)
  - [x] baseline, extended (8 and 12 bits), progressive; Huffman and arithmetic coding
  - [x] lossless, 2 to 16 bits
  - [x] every sampling, restarts, Adobe/JFIF, CMYK and YCCK, ICC profile
  - [x] cut streams and damage as libjpeg reads them, block smoothing
  - [x] NEON and SSE4.1/AVX2 IDCT, upsampling, colour
  - [x] on threads: restart intervals, speculative Huffman, progressive scans row by row
  - [ ] hierarchical, lossless arithmetic, DNL
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
  - [ ] HT (Part 15), Part 2 (Apple's decoder reads them on Apple platforms)
  - [ ] SIMD
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
