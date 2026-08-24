/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_ppp_w_clamp_cull2_core.h"

#include "asahi/lib/agx_ppp_w_clamp_cull2_core.h"

#include <string.h>

_Static_assert(D12_AGX3_PPP_W_CLAMP_CULL2_LENGTH ==
                  AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH,
               "D3D and shared PPP W-clamp/Cull-2 lengths must agree");

bool
d12_agx3_encode_ppp_w_clamp_cull2(
   enum agx_chip chip,
   const struct d12_agx3_ppp_w_clamp_cull2_fields *fields,
   struct d12_agx3_ppp_w_clamp_cull2 *out)
{
   struct agx_ppp_w_clamp_cull2_fields shared_fields;
   struct agx_ppp_w_clamp_cull2_core shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !fields)
      return false;

   shared_fields.w_clamp_raw = fields->w_clamp_raw;
   shared_fields.cull2_raw = fields->cull2_raw;
   if (!agx_ppp_w_clamp_cull2_core_pack(chip, &shared_fields, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared PPP W-clamp/Cull-2 storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_ppp_w_clamp_cull2(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_w_clamp_cull2_fields *fields)
{
   struct agx_ppp_w_clamp_cull2_fields shared_fields;

   if (fields)
      memset(fields, 0, sizeof(*fields));

   if (!fields || !agx_ppp_w_clamp_cull2_core_unpack(
                     chip, bytes, size, &shared_fields))
      return false;

   fields->w_clamp_raw = shared_fields.w_clamp_raw;
   fields->cull2_raw = shared_fields.cull2_raw;
   return true;
}
