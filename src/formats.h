/*
 * Copyright (C) 2026 Recep Aslantas
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

/*
 * The formats built in: IM_WITH_<FORMAT> for each. A build that picks
 * some (CMake's IM_FORMATS, which defines IM_SELECTED_FORMATS and theirs)
 * gets those alone; any other build every one (HEIC and JXL, Apple's
 * decoders, on Apple platforms).
 */

#ifndef src_formats_h
#define src_formats_h

/* codecs alone: JPEG, JPEG 2000 and CCITT fax */
#if IM_CODECS_ONLY && !defined(IM_SELECTED_FORMATS)
#  define IM_SELECTED_FORMATS 1
#  define IM_WITH_JPG 1
#  define IM_WITH_JP2 1
#  define IM_WITH_CCITT 1
#endif

#ifndef IM_SELECTED_FORMATS
#  define IM_WITH_PNG 1
#  define IM_WITH_BMP 1
#  define IM_WITH_PPM 1
#  define IM_WITH_QOI 1
#  define IM_WITH_TGA 1
#  define IM_WITH_JPG 1
#  define IM_WITH_JP2 1
#  define IM_WITH_CCITT 1
#  ifdef __APPLE__
#    define IM_WITH_JXL  1
#    define IM_WITH_HEIC 1
#  endif
#endif

#endif /* src_formats_h */
