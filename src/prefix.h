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

/*
 * IM_PREFIX for the symbols of a codec-only build (IM_CODECS_ONLY) that
 * aren't im's public functions (include/im/prefix.h has those): every one
 * of them, `nm` says, prefixed too.
 */

#ifndef src_prefix_h
#define src_prefix_h

#ifdef IM_PREFIX
#  define im_config             IM_PFX(im_config)
#  define im_pool_free          IM_PFX(im_pool_free)
#  define im_pool_new           IM_PFX(im_pool_new)
#  define im_pool_run           IM_PFX(im_pool_run)
#  define im_pool_size          IM_PFX(im_pool_size)
#  define im_pool_wrap          IM_PFX(im_pool_wrap)
#  define jp2_dec_mem           IM_PFX(jp2_dec_mem)
#  define jp2_info              IM_PFX(jp2_info)
#  define jpg_dc8               IM_PFX(jpg_dc8)
#  define jpg_dec_mem           IM_PFX(jpg_dec_mem)
#  define jpg_decode_mem        IM_PFX(jpg_decode_mem)
#  define jpg_flow_row          IM_PFX(jpg_flow_row)
#  define jpg_huff_build        IM_PFX(jpg_huff_build)
#  define jpg_huff_std          IM_PFX(jpg_huff_std)
#  define jpg_idct12            IM_PFX(jpg_idct12)
#  define jpg_idct8             IM_PFX(jpg_idct8)
#  define jpg_idct8_best        IM_PFX(jpg_idct8_best)
#  define jpg_idct_diff         IM_PFX(jpg_idct_diff)
#  define jpg_idct_for          IM_PFX(jpg_idct_for)
#  define jpg_loss_end          IM_PFX(jpg_loss_end)
#  define jpg_loss_start        IM_PFX(jpg_loss_start)
#  define jpg_scan_arith_lossless IM_PFX(jpg_scan_arith_lossless)
#  define jpg_undiff            IM_PFX(jpg_undiff)
#  define jpg_info              IM_PFX(jpg_info)
#  define jpg_natural           IM_PFX(jpg_natural)
#  define jpg_out               IM_PFX(jpg_out)
#  define jpg_out_ok            IM_PFX(jpg_out_ok)
#  define jpg_out_begin         IM_PFX(jpg_out_begin)
#  define jpg_out_rows          IM_PFX(jpg_out_rows)
#  define jpg_out_end           IM_PFX(jpg_out_end)
#  define jpg_put               IM_PFX(jpg_put)
#  define jpg_resync            IM_PFX(jpg_resync)
#  define jpg_scan_arith        IM_PFX(jpg_scan_arith)
#  define jpg_scan_end          IM_PFX(jpg_scan_end)
#  define jpg_scan_huff         IM_PFX(jpg_scan_huff)
#  define jpg_scan_lossless     IM_PFX(jpg_scan_lossless)
#  define jpg_unstuff           IM_PFX(jpg_unstuff)
#  define thread_cond_broadcast IM_PFX(thread_cond_broadcast)
#  define thread_cond_destroy   IM_PFX(thread_cond_destroy)
#  define thread_cond_init      IM_PFX(thread_cond_init)
#  define thread_cond_signal    IM_PFX(thread_cond_signal)
#  define thread_cond_wait      IM_PFX(thread_cond_wait)
#  define thread_exit           IM_PFX(thread_exit)
#  define thread_join           IM_PFX(thread_join)
#  define thread_lock           IM_PFX(thread_lock)
#  define thread_mutex_destroy  IM_PFX(thread_mutex_destroy)
#  define thread_mutex_init     IM_PFX(thread_mutex_init)
#  define thread_ncpu           IM_PFX(thread_ncpu)
#  define thread_new            IM_PFX(thread_new)
#  define thread_rdlock         IM_PFX(thread_rdlock)
#  define thread_release        IM_PFX(thread_release)
#  define thread_rwlock_destroy IM_PFX(thread_rwlock_destroy)
#  define thread_rwlock_init    IM_PFX(thread_rwlock_init)
#  define thread_rwunlock       IM_PFX(thread_rwunlock)
#  define thread_unlock         IM_PFX(thread_unlock)
#  define thread_wrlock         IM_PFX(thread_wrlock)
#endif

#endif /* src_prefix_h */
