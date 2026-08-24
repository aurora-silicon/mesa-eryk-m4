/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_ppp_viewport_payload_core.h"

#include "asahi/lib/agx_ppp_viewport_payload_core.h"

#include <string.h>

static void
d12_agx3_ppp_viewport_to_shared(
   const struct d12_agx3_ppp_viewport_fields *source,
   struct agx_ppp_viewport_payload_fields *destination)
{
   memset(destination, 0, sizeof(*destination));
   destination->viewport_count = source->viewport_count;
   destination->viewport_control_raw = source->viewport_control_raw;

   for (unsigned i = 0; i < D12_AGX3_PPP_VIEWPORT_MAX_VIEWPORTS; ++i) {
      destination->region_clip_raw[i] = source->region_clip_raw[i];
      destination->viewports[i].translate_x = source->viewports[i].translate_x;
      destination->viewports[i].scale_x = source->viewports[i].scale_x;
      destination->viewports[i].translate_y = source->viewports[i].translate_y;
      destination->viewports[i].scale_y = source->viewports[i].scale_y;
      destination->viewports[i].translate_z = source->viewports[i].translate_z;
      destination->viewports[i].scale_z = source->viewports[i].scale_z;
   }
}

static void
d12_agx3_ppp_viewport_from_shared(
   const struct agx_ppp_viewport_payload_fields *source,
   struct d12_agx3_ppp_viewport_fields *destination)
{
   memset(destination, 0, sizeof(*destination));
   destination->viewport_count = source->viewport_count;
   destination->viewport_control_raw = source->viewport_control_raw;

   for (unsigned i = 0; i < D12_AGX3_PPP_VIEWPORT_MAX_VIEWPORTS; ++i) {
      destination->region_clip_raw[i] = source->region_clip_raw[i];
      destination->viewports[i].translate_x = source->viewports[i].translate_x;
      destination->viewports[i].scale_x = source->viewports[i].scale_x;
      destination->viewports[i].translate_y = source->viewports[i].translate_y;
      destination->viewports[i].scale_y = source->viewports[i].scale_y;
      destination->viewports[i].translate_z = source->viewports[i].translate_z;
      destination->viewports[i].scale_z = source->viewports[i].scale_z;
   }
}

bool
d12_agx3_encode_ppp_viewport_payload(
   enum agx_chip chip, const struct d12_agx3_ppp_viewport_fields *fields,
   struct d12_agx3_ppp_viewport_payload *out)
{
   struct agx_ppp_viewport_payload_fields shared_fields;
   struct agx_ppp_viewport_payload_core shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !fields)
      return false;

   d12_agx3_ppp_viewport_to_shared(fields, &shared_fields);
   if (!agx_ppp_viewport_payload_core_pack(chip, &shared_fields, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared PPP viewport storage must agree");
   memcpy(out->bytes, shared.bytes, shared.length);
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_ppp_viewport_payload(enum agx_chip chip, const void *bytes,
                                     uint32_t size,
                                     struct d12_agx3_ppp_viewport_fields *fields)
{
   struct agx_ppp_viewport_payload_fields shared_fields;

   if (fields)
      memset(fields, 0, sizeof(*fields));

   if (!fields ||
       !agx_ppp_viewport_payload_core_unpack(chip, bytes, size, &shared_fields))
      return false;

   d12_agx3_ppp_viewport_from_shared(&shared_fields, fields);
   return true;
}
