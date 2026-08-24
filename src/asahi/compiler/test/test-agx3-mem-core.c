/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * The AGX3 memory encoders must reproduce real Apple lddev/stdev instructions.
 * Each vector is a device load/store decoded from the G16G/G17S corpus; the
 * encoder rebuilds its bytes from the normalized descriptor.
 */

#include "agx3_mem_core.h"
#include "agx3_mem_core_vectors.c"

#include <stdio.h>
#include <string.h>

int
main(void)
{
   unsigned pass = 0, fail = 0;

   for (size_t i = 0; i < mem_vec_count; ++i) {
      const struct mem_vec *v = &mem_vecs[i];
      const enum agx_isa_encoding e =
         v->enc == 0 ? AGX_ISA_ENCODING_AGX3B : AGX_ISA_ENCODING_AGX3C;

      struct agx3_mem_desc d = {
         .data = (uint16_t)v->data,
         .width = (enum agx3_mem_width)v->width,
         .mask = (uint8_t)v->mask,
         .shift = (uint8_t)v->shift,
         .base_is_uniform = v->base_uni != 0,
         .base = (uint16_t)v->base,
         .base_kill = v->base_kill != 0,
         .idx_kind = v->idx_reg ? AGX3_MEM_IDX_REG : AGX3_MEM_IDX_IMM,
         .idx_scaled = v->scaled != 0,
         .byte_off = v->byte_off,
         .elem_idx = v->elem_idx,
         .idx_reg = (uint16_t)v->idx_reg_v,
         .idx_signed = v->idx_signed != 0,
         .idx_kill = v->idx_kill != 0,
         .slot = (uint8_t)v->slot,
         .wait_mask = (uint16_t)v->wait,
      };

      uint8_t out[AGX3_MEM_MAX_LENGTH];
      uint8_t len = 0;
      bool ok = v->is_load ? agx3_mem_encode_load(e, &d, out, &len)
                           : agx3_mem_encode_store(e, &d, out, &len);

      if (!ok || len != v->nbytes || memcmp(out, v->bytes, v->nbytes) != 0) {
         if (fail < 10)
            fprintf(stderr, "FAIL %s i=%zu enc=%d\n",
                    v->is_load ? "lddev" : "stdev", i, v->enc);
         fail++;
      } else {
         pass++;
      }
   }

   if (fail) {
      fprintf(stderr, "agx3_mem_core: %u of %zu failed\n", fail, mem_vec_count);
      return 1;
   }
   printf("agx3_mem_core: all %u corpus memory ops reproduced\n", pass);
   return 0;
}
