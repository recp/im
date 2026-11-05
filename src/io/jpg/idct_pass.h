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
 * The IDCT's 1-D pass for 32-bit lanes of any SIMD (idct_x86.h,
 * idct_wasm.h): T the vector, ADD SUB MUL (by a constant) SHL its
 * operations; idct.c's sums in its order, x[0–7] in, o[0–7] out.
 */

#ifndef src_io_jpg_idct_pass_h
#define src_io_jpg_idct_pass_h

#define JX_PASS(T, ADD, SUB, MUL, SHL, x, o)                                    \
  do {                                                                         \
    T z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;                  \
                                                                               \
    z1  = MUL(ADD(x[2], x[6]), 4433);                                          \
    t2  = SUB(z1, MUL(x[6], 15137));                                           \
    t3  = ADD(z1, MUL(x[2], 6270));                                            \
    t0  = SHL(ADD(x[0], x[4]), 13);                                            \
    t1  = SHL(SUB(x[0], x[4]), 13);                                            \
    t10 = ADD(t0, t3);                                                         \
    t13 = SUB(t0, t3);                                                         \
    t11 = ADD(t1, t2);                                                         \
    t12 = SUB(t1, t2);                                                         \
                                                                               \
    z1 = ADD(x[7], x[1]);                                                      \
    z2 = ADD(x[5], x[3]);                                                      \
    z3 = ADD(x[7], x[3]);                                                      \
    z4 = ADD(x[5], x[1]);                                                      \
    z5 = MUL(ADD(z3, z4), 9633);                                               \
    z3 = SUB(z5, MUL(z3, 16069));                                              \
    z4 = SUB(z5, MUL(z4, 3196));                                               \
    z1 = MUL(z1, -7373);                                                       \
    z2 = MUL(z2, -20995);                                                      \
    t0 = ADD(ADD(z1, MUL(x[7], 2446)), z3);                                    \
    t1 = ADD(ADD(z2, MUL(x[5], 16819)), z4);                                   \
    t2 = ADD(ADD(z2, MUL(x[3], 25172)), z3);                                   \
    t3 = ADD(ADD(z1, MUL(x[1], 12299)), z4);                                   \
                                                                               \
    o[0] = ADD(t10, t3);                                                       \
    o[7] = SUB(t10, t3);                                                       \
    o[1] = ADD(t11, t2);                                                       \
    o[6] = SUB(t11, t2);                                                       \
    o[2] = ADD(t12, t1);                                                       \
    o[5] = SUB(t12, t1);                                                       \
    o[3] = ADD(t13, t0);                                                       \
    o[4] = SUB(t13, t0);                                                       \
  } while (0)

/* JX_PASS where x[4–7] are zero: the same sums without their terms (as
   idct_neon.h's JN_HALF) */
#define JX_HALF(T, ADD, SUB, MUL, SHL, x, o)                                    \
  do {                                                                         \
    T z1, z2, z3, z4, z5, t0, t2, t3, t10, t11, t12, t13;                      \
                                                                               \
    z1  = MUL(x[2], 4433);                                                     \
    t2  = z1;                                                                  \
    t3  = ADD(z1, MUL(x[2], 6270));                                            \
    t0  = SHL(x[0], 13);                                                       \
    t10 = ADD(t0, t3);                                                         \
    t13 = SUB(t0, t3);                                                         \
    t11 = ADD(t0, t2);                                                         \
    t12 = SUB(t0, t2);                                                         \
                                                                               \
    z5 = MUL(ADD(x[3], x[1]), 9633);                                           \
    z3 = SUB(z5, MUL(x[3], 16069));                                            \
    z4 = SUB(z5, MUL(x[1], 3196));                                             \
    z1 = MUL(x[1], -7373);                                                     \
    z2 = MUL(x[3], -20995);                                                    \
    t0 = ADD(z1, z3);                                                          \
    z5 = ADD(z2, z4);                                                          \
    t2 = ADD(ADD(z2, MUL(x[3], 25172)), z3);                                   \
    t3 = ADD(ADD(z1, MUL(x[1], 12299)), z4);                                   \
                                                                               \
    o[0] = ADD(t10, t3);                                                       \
    o[7] = SUB(t10, t3);                                                       \
    o[1] = ADD(t11, t2);                                                       \
    o[6] = SUB(t11, t2);                                                       \
    o[2] = ADD(t12, z5);                                                       \
    o[5] = SUB(t12, z5);                                                       \
    o[3] = ADD(t13, t0);                                                       \
    o[4] = SUB(t13, t0);                                                       \
  } while (0)

#endif /* src_io_jpg_idct_pass_h */
