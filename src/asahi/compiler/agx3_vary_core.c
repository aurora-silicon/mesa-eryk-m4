/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_vary_core.h"

#include "agx3_binder.h"

#define KV(f, v) {(uint16_t)(f), (uint64_t)(v)}

static bool
is_b(enum agx_isa_encoding e)
{
   return e == AGX_ISA_ENCODING_AGX3B;
}

bool
agx3_vary_interp_coords(enum agx_isa_encoding enc, uint16_t dest,
                    uint16_t coef_index, bool perspective,
                    enum agx3_vary_location location, bool first, bool projective_rhw,
                    uint16_t centroid_coord,
                    uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   if ((enc != AGX_ISA_ENCODING_AGX3B &&
        enc != AGX_ISA_ENCODING_AGX3C) ||
       (first && enc != AGX_ISA_ENCODING_AGX3B) ||
       (projective_rhw && (enc != AGX_ISA_ENCODING_AGX3B ||
                           !perspective ||
                           coef_index > 0x7f)) ||
       (dest & 1) != 0 || dest >= 256 ||
       (centroid_coord & 1) || centroid_coord >= 256 ||
       (location != AGX3_VARY_CENTER && location != AGX3_VARY_CENTROID))
      return false;
   const struct agx3_field_kv base_b[] = {KV(9, 2), KV(10, 2), KV(25, 6),
                                          KV(26, 2), KV(40, 2)};
   const struct agx3_field_kv base_c[] = {KV(9, 2),  KV(10, 2), KV(25, 6),
                                          KV(26, 2), KV(40, 2), KV(44, 3),
                                          KV(45, 58)};
   struct agx3_field_kv ov[9];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(2, dest);
   ov[n++] = (struct agx3_field_kv)KV(22, coef_index);
   ov[n++] = (struct agx3_field_kv)KV(11, perspective ? 1 : 0);
   if (location == AGX3_VARY_CENTER) {
      ov[n++] = (struct agx3_field_kv)KV(37, 66);
      ov[n++] = (struct agx3_field_kv)KV(39, 7);
      ov[n++] = (struct agx3_field_kv)KV(41, 0);
      ov[n++] = (struct agx3_field_kv)KV(32, 0);
   } else {
      /* Centroid and ordinary per-sample interpolation use the same leaf.
       * Explicit at-sample setup remains outside this codec. */
      ov[n++] = (struct agx3_field_kv)KV(39, 2);
      ov[n++] = (struct agx3_field_kv)KV(41, 1);
      ov[n++] =
         (struct agx3_field_kv)KV(32, enc == AGX_ISA_ENCODING_AGX3B ? centroid_coord : (perspective ? 0 : 6));
   }
   if (projective_rhw)
      ov[n++] = (struct agx3_field_kv)KV(54, 1);
   if (first)
      ov[n++] = (struct agx3_field_kv)KV(63, 1);
   return agx3_bind_compose(enc, "f32interp", 0, is_b(enc) ? base_b : base_c,
                            is_b(enc) ? 5 : 7, ov, n, out, len);
}

bool
agx3_vary_interp_at_start(enum agx_isa_encoding enc, uint16_t dest,
                         uint16_t coef_index, bool perspective,
                         enum agx3_vary_location location, bool first,
                         uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   return agx3_vary_interp_coords(enc, dest, coef_index, perspective, location,
                               first, false, 6, out, len);
}

bool
agx3_vary_projective_rhw_at(enum agx_isa_encoding enc, uint16_t dest,
                            uint16_t coef_index, enum agx3_vary_location location,
                            bool first, uint8_t out[AGX3_VARY_MAX_LENGTH],
                            uint8_t *len)
{
   return agx3_vary_interp_coords(enc, dest, coef_index, true, location,
                               first, true, 6, out, len);
}

bool
agx3_vary_projective_rhw(enum agx_isa_encoding enc, uint16_t dest,
                         uint16_t coef_index, bool first,
                         uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   return agx3_vary_interp_coords(enc, dest, coef_index, true, AGX3_VARY_CENTER,
                               first, true, 6, out, len);
}

bool
agx3_vary_interp_at(enum agx_isa_encoding enc, uint16_t dest,
                    uint16_t coef_index, bool perspective,
                    enum agx3_vary_location location,
                    uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   return agx3_vary_interp_at_start(enc, dest, coef_index, perspective,
                                    location, false, out, len);
}

bool
agx3_vary_interp(enum agx_isa_encoding enc, uint16_t dest,
                 uint16_t coef_index, bool perspective,
                 uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   return agx3_vary_interp_at(enc, dest, coef_index, perspective,
                              AGX3_VARY_CENTER, out, len);
}

bool
agx3_vary_ldcf_at_start(enum agx_isa_encoding enc, uint16_t dest,
                        uint16_t coef_index, uint8_t slot, bool first,
                        uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   if ((enc != AGX_ISA_ENCODING_AGX3B &&
        enc != AGX_ISA_ENCODING_AGX3C) ||
       (first && !is_b(enc)) ||
       (dest & 1) != 0 || dest >= 256 || slot < 1 ||
       slot > (is_b(enc) ? 6 : 8))
      return false;
   const struct agx3_field_kv base_b[] = {KV(9, 2), KV(10, 2), KV(23, 1),
                                          KV(24, 1), KV(38, 7)};
   const struct agx3_field_kv base_c[] = {KV(9, 2),  KV(10, 2), KV(23, 1),
                                          KV(24, 1), KV(38, 7), KV(29, 103)};
   const struct agx3_field_kv ov[] = {KV(2, dest), KV(15, coef_index),
                                      KV(12, slot), KV(48, first)};
   return agx3_bind_compose(enc, "ldcfr", 0, is_b(enc) ? base_b : base_c,
                            is_b(enc) ? 5 : 6, ov, first ? 4 : 3, out, len);
}

bool
agx3_vary_ldcf(enum agx_isa_encoding enc, uint16_t dest, uint16_t coef_index,
               uint8_t slot, uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   return agx3_vary_ldcf_at_start(enc, dest, coef_index, slot, false, out, len);
}

bool
agx3_vary_uvsw(enum agx_isa_encoding enc, uint16_t value, uint16_t uvs_index,
               uint8_t slot, bool value_kill, uint16_t wait_mask,
               uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   const unsigned nwait = is_b(enc) ? 6 : 8;
   if ((enc != AGX_ISA_ENCODING_AGX3B &&
        enc != AGX_ISA_ENCODING_AGX3C) ||
       (value & 1) != 0 || value >= 256 || slot < 1 || slot > nwait ||
       (wait_mask & ~((1u << nwait) - 1)) != 0)
      return false;
   const struct agx3_field_kv base_b[] = {KV(9, 5), KV(23, 1), KV(24, 1)};
   const struct agx3_field_kv base_c[] = {KV(9, 5),  KV(10, 2), KV(23, 1),
                                          KV(24, 1), KV(28, 2), KV(29, 196)};
   struct agx3_field_kv ov[16];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(5, value);
   ov[n++] = (struct agx3_field_kv)KV(15, uvs_index);
   ov[n++] = (struct agx3_field_kv)KV(12, slot);
   ov[n++] = (struct agx3_field_kv)KV(13, value_kill ? 1 : 0);
   /* Wait bits f30.. (6 on agx3b, 8 on agx3c). */
   for (unsigned i = 0; i < nwait; ++i)
      if (wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(30 + i, 1);

   return agx3_bind_compose(enc, "uvsw", 0, is_b(enc) ? base_b : base_c,
                            is_b(enc) ? 3 : 6, ov, n, out, len);
}

/* Destination operand kinds 2/3 select f16/f17 in the native UVSW table;
 * f24=1 specifies a half-width UVS index, independent of its 32-bit value. */
bool
agx3_vary_uvsw_indexed(enum agx_isa_encoding enc, uint16_t value,
                       uint16_t index, bool uniform, bool index_live,
                       uint8_t slot, bool value_kill, uint16_t wait_mask,
                       uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   if (enc != AGX_ISA_ENCODING_AGX3B || (value & 1) || value >= 256 ||
       index >= (uniform ? 512 : 192) || slot < 1 || slot > 6 ||
       (wait_mask & ~0x3f))
      return false;
   const struct agx3_field_kv base[] = {KV(9,5), KV(23,uniform ? 3 : 2), KV(24,1)};
   struct agx3_field_kv ov[12];
   unsigned n=0;
   ov[n++] = (struct agx3_field_kv)KV(5,value);
   ov[n++] = (struct agx3_field_kv)KV(uniform ? 17 : 16,index);
   ov[n++] = (struct agx3_field_kv)KV(12,slot);
   ov[n++] = (struct agx3_field_kv)KV(13,value_kill);
   if (!uniform)
      ov[n++] = (struct agx3_field_kv)KV(index_live ? 25 : 27,1);
   for (unsigned i=0;i<6;i++)
      if (wait_mask & (1u<<i))
         ov[n++] = (struct agx3_field_kv)KV(30+i,1);
   return agx3_bind_compose(enc,"uvsw",0,base,3,ov,n,out,len);
}

/* Native G16G centroid float4: coverage half0 -> coordinate r3,
 * af14540603000a01, after the SR39 load on slot1. */
bool
agx3_vary_centroid_coords(enum agx_isa_encoding enc, uint16_t dest,
                          uint16_t coverage, bool discard, uint16_t wait_mask,
                          uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   if (enc != AGX_ISA_ENCODING_AGX3B || (dest & 1) || dest >= 256 ||
       coverage >= 256 || (wait_mask & ~0x3f))
      return false;
   const struct agx3_field_kv base[] = {
      KV(9,2), KV(10,2), KV(11,1), KV(2,dest),
      KV(23,2), KV(24,1), KV(16,coverage), KV(27,discard), KV(38,1),
   };
   struct agx3_field_kv waits[6];
   unsigned n = 0;
   for (unsigned i=0;i<6;++i)
      if (wait_mask & (1u << i))
         waits[n++] = (struct agx3_field_kv)KV(30+i,1);
   return agx3_bind_compose(enc, "itrcoords", 0, base, 9, waits, n, out, len);
}
