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
- [ ] JPEG (In Progress)
- [ ] PNG (In Progress)
  - [x] palette
  - [x] color types
  - [x] 1,2,4,8,16bpp
  - [x] options
  - [x] adam7 interlaced PNGs
  - [ ] alpha - in progress -
  - [ ] additional chunks
  - [ ] custom unzip/deflate - in progress -
  - [ ] convert to user selected format
- [ ] JPEG 2000
  - [x] Apple platform
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
