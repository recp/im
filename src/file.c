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

#include "file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

ImFileResult
im_readfile(const char * __restrict file, bool readonly) {
  FILE        *infile;
  size_t       blksize;
  size_t       fsize;
  size_t       fcontents_size;
  size_t       total_read;
  size_t       nread;
  struct stat  infile_st;
  int          infile_no;
  ImFileResult res;

  memset(&res, 0, sizeof(res));
  
  infile = fopen(file, "rb");
  if (!infile) {
    fprintf(stderr, "errno: %d: %s", errno, strerror(errno));
    goto err;
  }
  
  infile_no = fileno(infile);
  
  if (fstat(infile_no, &infile_st) != 0)
    goto err;
  
  fsize          = infile_st.st_size;
  fcontents_size = sizeof(char) * fsize;
  res.size       = fcontents_size;
  
  /* TODO: use madvise() ? */
  if (readonly
      && fcontents_size > 1024 * 16 /* > 16K */
      && (res.raw = im_mmap_rdonly(infile_no, fsize))) {
    res.mmap = true;
    res.ret  = IM_OK;
    return res;
  }

//#ifndef IM_WINAPI
//  blksize = infile_st.st_blksize;
//#else
//  blksize = 512;
//#endif

  blksize = fcontents_size;
  res.raw = malloc(fcontents_size + 1);

#ifdef DEBUG
  assert(res.raw && "malloc failed");
#endif
  
  memset((char *)res.raw + fcontents_size, '\0', 1);
  
  total_read = 0;
  
  do {
    if ((fcontents_size - total_read) < blksize)
      blksize = fcontents_size - total_read;
    
    nread = fread((char *)res.raw + total_read,
                  sizeof(char),
                  blksize,
                  infile);
    
    total_read += nread;
  } while (nread > 0 && total_read < fsize);
  
  fclose(infile);
  
  res.ret = IM_OK;

  return res;
err:
  res.ret  = IM_ERR;
  return res;
}
