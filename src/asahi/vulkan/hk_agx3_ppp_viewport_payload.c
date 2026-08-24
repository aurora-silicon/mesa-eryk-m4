/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_viewport_payload.h"

bool
hk_agx3_decode_ppp_viewport_payload(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_viewport_payload_fields *fields)
{
   return agx_ppp_viewport_payload_core_unpack(chip, payload, size, fields);
}
