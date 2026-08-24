/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_tessellate_core.h"

#include <string.h>

bool
d12_agx3_vdm_tessellate_direct_encode(
   enum agx_chip chip, const struct agx_vdm_tessellate_direct_info *info,
   struct d12_agx3_vdm_tessellate_direct *out)
{
   struct agx_vdm_tessellate_direct shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !agx_vdm_tessellate_direct_pack(chip, info, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared VDM Tessellate storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_vdm_tessellate_direct_decode(
   enum agx_chip chip, const void *command, uint32_t size,
   struct agx_vdm_tessellate_direct_info *info)
{
   return agx_vdm_tessellate_direct_unpack(chip, command, size, info);
}
