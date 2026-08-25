/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_hal300_depth_stencil.h"

bool
hk_agx3_encode_ppp_hal300_depth_stencil(
   enum agx_chip chip,
   const struct agx_ppp_hal300_depth_stencil_fields *fields,
   struct agx_ppp_hal300_depth_stencil_core *out)
{
   return agx_ppp_hal300_depth_stencil_core_pack(chip, fields, out);
}

bool
hk_agx3_decode_ppp_hal300_depth_stencil(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_depth_stencil_fields *fields)
{
   return agx_ppp_hal300_depth_stencil_core_unpack(chip, payload, size,
                                                   fields);
}
