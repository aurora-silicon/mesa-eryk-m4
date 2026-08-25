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
agx3_vary_interp(enum agx_isa_encoding enc, uint16_t dest, uint16_t coef_index,
                 bool perspective, uint8_t out[AGX3_VARY_MAX_LENGTH],
                 uint8_t *len)
{
   if (dest >= 256)
      return false;
   const struct agx3_field_kv base_b[] = {KV(9, 2), KV(10, 2), KV(25, 6),
                                          KV(26, 2), KV(40, 2)};
   const struct agx3_field_kv base_c[] = {KV(9, 2),  KV(10, 2), KV(25, 6),
                                          KV(26, 2), KV(40, 2), KV(44, 3),
                                          KV(45, 58)};
   /* Center location: f37=66, f39=7, f41=0 (f32 selector 0). */
   const struct agx3_field_kv ov[] = {KV(2, dest), KV(22, coef_index),
                                      KV(11, perspective ? 1 : 0), KV(37, 66),
                                      KV(39, 7), KV(41, 0), KV(32, 0)};
   return agx3_bind_compose(enc, "f32interp", 0, is_b(enc) ? base_b : base_c,
                            is_b(enc) ? 5 : 7, ov, 7, out, len);
}

bool
agx3_vary_ldcf(enum agx_isa_encoding enc, uint16_t dest, uint16_t coef_index,
               uint8_t slot, uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   if (dest >= 256 || slot < 1 ||
       slot > (is_b(enc) ? 6 : 8))
      return false;
   const struct agx3_field_kv base_b[] = {KV(9, 2), KV(10, 2), KV(23, 1),
                                          KV(24, 1), KV(38, 7)};
   const struct agx3_field_kv base_c[] = {KV(9, 2),  KV(10, 2), KV(23, 1),
                                          KV(24, 1), KV(38, 7), KV(29, 103)};
   const struct agx3_field_kv ov[] = {KV(2, dest), KV(15, coef_index),
                                      KV(12, slot)};
   return agx3_bind_compose(enc, "ldcfr", 0, is_b(enc) ? base_b : base_c,
                            is_b(enc) ? 5 : 6, ov, 3, out, len);
}

bool
agx3_vary_uvsw(enum agx_isa_encoding enc, uint16_t value, uint16_t uvs_index,
               uint8_t slot, bool value_kill, uint16_t wait_mask,
               uint8_t out[AGX3_VARY_MAX_LENGTH], uint8_t *len)
{
   if (value >= 256)
      return false;
   const struct agx3_field_kv base_b[] = {KV(9, 5), KV(23, 1), KV(24, 1)};
   const struct agx3_field_kv base_c[] = {KV(9, 5),  KV(10, 2), KV(23, 1),
                                          KV(24, 1), KV(28, 2), KV(29, 196)};
   struct agx3_field_kv ov[16];
   unsigned n = 0;
   ov[n++] = (struct agx3_field_kv)KV(5, value);
   ov[n++] = (struct agx3_field_kv)KV(15, uvs_index);
   if (slot)
      ov[n++] = (struct agx3_field_kv)KV(12, slot);
   ov[n++] = (struct agx3_field_kv)KV(13, value_kill ? 1 : 0);
   /* Wait bits f30.. (6 on agx3b, 8 on agx3c). */
   const unsigned nwait = is_b(enc) ? 6 : 8;
   for (unsigned i = 0; i < nwait; ++i)
      if (wait_mask & (1u << i))
         ov[n++] = (struct agx3_field_kv)KV(30 + i, 1);

   return agx3_bind_compose(enc, "uvsw", 0, is_b(enc) ? base_b : base_c,
                            is_b(enc) ? 3 : 6, ov, n, out, len);
}
