/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef src_jpg_markers_h
#define src_jpg_markers_h

typedef uint16_t JPGMarker;

#define JPP_MARKER_SIZE 2

#define JPG_SOI  0xD8FF
#define JPG_EOI  0xD9FF

/* Start Of Frame markers, non-differential, Huffman coding                 */
#define JPG_SOF0 0xC0FF /* Baseline DCT                                     */
#define JPG_SOF1 0xC1FF /* Extended sequential DCT                          */
#define JPG_SOF2 0xC2FF /* Progressive DCT                                  */
#define JPG_SOF3 0xC3FF /* Lossless (sequential)                            */

/* Start Of Frame markers, differential, Huffman coding                     */
#define JPG_SOF5  0xC5FF /* Differential sequential DCT                     */
#define JPG_SOF6  0xC6FF /* Differential progressive DCT                    */
#define JPG_SOF7  0xC7FF /* Differential lossless (sequential)              */

/* Start Of Frame markers, non-differential, arithmetic coding              */
#define JPG_SOF8  0xC8FF /* Reserved for JPEG extensions                    */
#define JPG_SOF9  0xC9FF /* Extended sequential DCT                         */
#define JPG_SOF10 0xCAFF /* Progressive DCT                                 */
#define JPG_SOF11 0xCBFF /* Lossless (sequential)                           */

/* Start Of Frame markers, differential, arithmetic coding                  */
#define JPG_SOF13 0xCDFF /* Differential sequential DCT                     */
#define JPG_SOF14 0xCEFF /* Differential progressive DCT                    */
#define JPG_SOF15 0xCFFF /* Differential lossless (sequential)              */

#define JPG_APPn(n) 0xE ## n ## FF
#define JPG_SOFn(n) 0xC ## n ## FF

#define JPG_DQT 0xDBFF   /* Define quantization table(s)                    */

#endif /* src_jpg_markers_h */
