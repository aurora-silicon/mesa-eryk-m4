/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_w_clamp_cull2.h"

bool
hk_agx3_decode_ppp_w_clamp_cull2(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_w_clamp_cull2_fields *fields)
{
   return agx_ppp_w_clamp_cull2_core_unpack(chip, payload, size, fields);
}
