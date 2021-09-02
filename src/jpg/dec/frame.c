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

#include "frame.h"
#include "scan.h"
#include <stdio.h>

IM_HIDE
ImByte*
jpg_sof(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImFrm        *frm;
  ImComponent *icomp;
  uint8_t      tmp;
  uint32_t     len, i, Nf;

  len             = jpg_get_ui16(pRaw);
  frm             = &jpg->frm;
  frm->precision  = pRaw[2];
  frm->height     = jpg_get_ui16(&pRaw[3]);
  frm->width      = jpg_get_ui16(&pRaw[5]);
  frm->Nf         = Nf = pRaw[7];
  frm->hmax       = 0;
  frm->vmax       = 0;

  jpg->im->data   = malloc(Nf * frm->height * frm->width);
  jpg->im->width  = frm->width;
  jpg->im->height = frm->height;
  jpg->im->len    = Nf * frm->height * frm->width;

  pRaw += 8;

  /* if two IDs are same, last one will override first one */
  for (i = 0; i < Nf; i++) {
    icomp = &frm->compo[i];
    tmp   = pRaw[1];
    
    icomp->id = pRaw[0];;      /* Ci  */
    icomp->V  = tmp & 0x0F;    /* Vi  */
    icomp->H  = tmp >> 4;      /* Hi  */
    icomp->Tq = pRaw[2];       /* Tqi */

    frm->hmax = im_maxiu8(frm->hmax, icomp->H);
    frm->vmax = im_maxiu8(frm->vmax, icomp->V);
    
    pRaw += 3;
  }

  return pRaw;
}

IM_HIDE
ImByte*
jpg_sos(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImScan         *scan;
  ImByte         *pRawEnd;
  ImComponentSel *icomp;
  uint32_t        len, i, Ns;
  uint8_t         tmp;

  len               = jpg_get_ui16(pRaw);
  pRawEnd           = pRaw + len;
  scan              = calloc(1, sizeof(*scan));
  scan->Ns          = Ns = pRaw[2];
  scan->compo.ncomp = Ns;
  scan->jpg         = jpg;

  if (Ns > 4) {
    jpg->result = IM_JPEG_INVALID_COMPONENT_COUNT_IN_SCAN;
    thread_exit();
    return NULL;
  }

  pRaw += 3;

  for (i = 0; i < Ns; i++) {
    icomp     = &scan->compo.comp[i];
    tmp       = pRaw[1];
    icomp->id = pRaw[0];    /* Csj  */
    icomp->Ta = tmp & 0x0F; /* Taj  */
    icomp->Td = tmp >> 4;   /* Tdj  */
    
    pRaw += 2;
  }

  scan->startOfSpectral = pRaw[0];
  scan->endOfSpectral   = pRaw[1];
  tmp                   = pRaw[2];
  scan->apprxLo         = tmp & 0x0F;
  scan->apprxHi         = tmp >> 4;

  jpg->scan = scan;

  /* interleaved */
  if (Ns > 1) {
    pRawEnd = jpg_scan_intr(pRawEnd, jpg, scan);
  } else {

  }

  /* next sos or EOI */
  return pRawEnd;
}
