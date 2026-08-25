/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_state_core.h"

#include <string.h>

static void
hk_agx3_ioto_state_tail_publish(
   const struct agx_ioto_state_tail_output *packed,
   struct hk_agx3_ioto_state_tail_encoding *out)
{
   struct hk_agx3_ioto_state_tail_encoding staged = {
      .length = (uint8_t)packed->length,
   };
   memcpy(staged.bytes, packed->bytes, packed->length);
   *out = staged;
}

bool
hk_agx3_ioto_state_tail_generate(
   enum agx_chip chip, const struct agx_ioto_state_tail *tail,
   struct hk_agx3_ioto_state_tail_encoding *out)
{
   if (!out)
      return false;

   struct agx_ioto_state_tail_output packed;
   if (!agx_ioto_state_tail_pack(chip, tail, &packed))
      return false;

   hk_agx3_ioto_state_tail_publish(&packed, out);
   return true;
}

bool
hk_agx3_ioto_state_tail_generate_shader(
   enum agx_chip chip, const struct agx_ioto_state_tail *tail,
   uint64_t shader_va, struct hk_agx3_ioto_state_tail_encoding *out)
{
   if (!out)
      return false;

   struct agx_ioto_state_tail_output packed;
   if (!agx_ioto_state_tail_pack_shader(chip, tail, shader_va, &packed))
      return false;

   hk_agx3_ioto_state_tail_publish(&packed, out);
   return true;
}
