/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_ppp_hal300_depth_stencil_core.h"

#include "asahi/lib/agx_ppp_hal300_depth_stencil_core.h"

#include <string.h>

_Static_assert(D12_AGX3_PPP_HAL300_DEPTH_STENCIL_LENGTH ==
                  AGX_PPP_HAL300_DEPTH_STENCIL_CORE_LENGTH,
               "D3D and shared HAL300 depth/stencil lengths must agree");

static void
d12_agx3_ppp_hal300_depth_stencil_to_shared(
   const struct d12_agx3_ppp_hal300_depth_stencil_fields *source,
   struct agx_ppp_hal300_depth_stencil_fields *destination)
{
   destination->fragment_control_raw = source->fragment_control_raw;
   destination->fragment_front_face_raw = source->fragment_front_face_raw;
   destination->fragment_back_face_raw = source->fragment_back_face_raw;
   destination->fragment_front_stencil_raw =
      source->fragment_front_stencil_raw;
   destination->fragment_back_stencil_raw =
      source->fragment_back_stencil_raw;
}

static void
d12_agx3_ppp_hal300_depth_stencil_from_shared(
   const struct agx_ppp_hal300_depth_stencil_fields *source,
   struct d12_agx3_ppp_hal300_depth_stencil_fields *destination)
{
   destination->fragment_control_raw = source->fragment_control_raw;
   destination->fragment_front_face_raw = source->fragment_front_face_raw;
   destination->fragment_back_face_raw = source->fragment_back_face_raw;
   destination->fragment_front_stencil_raw =
      source->fragment_front_stencil_raw;
   destination->fragment_back_stencil_raw =
      source->fragment_back_stencil_raw;
}

bool
d12_agx3_encode_ppp_hal300_depth_stencil(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_depth_stencil_fields *fields,
   struct d12_agx3_ppp_hal300_depth_stencil *out)
{
   struct agx_ppp_hal300_depth_stencil_fields shared_fields;
   struct agx_ppp_hal300_depth_stencil_core shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !fields)
      return false;

   d12_agx3_ppp_hal300_depth_stencil_to_shared(fields, &shared_fields);
   if (!agx_ppp_hal300_depth_stencil_core_pack(chip, &shared_fields, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared HAL300 depth/stencil storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_ppp_hal300_depth_stencil(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_depth_stencil_fields *fields)
{
   struct agx_ppp_hal300_depth_stencil_fields shared_fields;

   if (fields)
      memset(fields, 0, sizeof(*fields));

   if (!fields || !agx_ppp_hal300_depth_stencil_core_unpack(
                     chip, bytes, size, &shared_fields))
      return false;

   d12_agx3_ppp_hal300_depth_stencil_from_shared(&shared_fields, fields);
   return true;
}
