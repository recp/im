/*
 * Copyright (C) 2020 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef src_jpg_markers_h
#define src_jpg_markers_h

typedef uint16_t JPGMarker;

#define JPP_MARKER_SIZE 2

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

/* Huffman table specification */
#define JPG_DHT 0xC4FF   /* Define Huffman table(s)                         */

/* Arithmetic coding conditioning specification */
#define JPG_DAC 0xCCFF   /* Define arithmetic coding conditioning(s)        */

/* Restart interval termination */
#define JPG_RST(m) 0xD ## m ## FF       /* Restart with modulo 8 count “m”  */

/* Other markers */
#define JPG_SOI  0xD8FF  /* Start of image                                  */
#define JPG_EOI  0xD9FF  /* End of image                                    */
#define JPG_SOS  0xDAFF  /* Start of scan                                   */
#define JPG_DQT  0xDBFF  /* Define quantization table(s)                    */
#define JPG_DNL  0xDCFF  /* Define number of lines                          */
#define JPG_DRI  0xDDFF  /* Define restart interval                         */
#define JPG_DHP  0xDDFF  /* Define hierarchical progression                 */
#define JPG_EXP  0xDDFF  /* Expand reference component(s)                   */
#define JPG_COM  0xFEFF  /* COM: Comment segment                            */

#define JPG_APPn(n) 0xE ## n ## FF  /* Reserved for application segments    */
#define JPG_JPGn(n) 0xF ## n ## FF  /* Reserved for JPEG extensions         */
#define JPG_SOFn(n) 0xC ## n ## FF  /* Start Of Frame                       */

/* Reserved markers */
#define JPG_TEM  0x01FF
#define JPG_RES  0x02FF

/*  */

#define JPG_ZRL  0x0FFF

#endif /* src_jpg_markers_h */
