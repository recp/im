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

#include "common.h"
#include <stdio.h>

#if DEBUG

IM_HIDE
void
print_byte(uint8_t byte) {
  static const char *bit_rep[16] = {
      [ 0] = "0000", [ 1] = "0001", [ 2] = "0010", [ 3] = "0011",
      [ 4] = "0100", [ 5] = "0101", [ 6] = "0110", [ 7] = "0111",
      [ 8] = "1000", [ 9] = "1001", [10] = "1010", [11] = "1011",
      [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
  };
  
  printf("%s%s\n", bit_rep[byte >> 4], bit_rep[byte & 0x0F]);
}

#endif
