/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_core.h"

#include "asahi/lib/agx_vdm_state_core.h"

#include <string.h>

bool
d12_agx3_vdm_encode_vertex_pipeline_pair(
   enum agx_chip chip, uint64_t pipeline, uint32_t raw_template,
   struct d12_agx3_vdm_vertex_pipeline_pair *out)
{
   struct agx_vdm_state_vertex_pipeline_pair shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !agx_vdm_state_vertex_pipeline_pair_pack(
                  chip, pipeline, raw_template, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared VDM pipeline storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_vdm_decode_vertex_pipeline_pair(
   enum agx_chip chip, const void *bytes, uint32_t size, uint64_t *pipeline,
   uint32_t *raw_template)
{
   return agx_vdm_state_vertex_pipeline_pair_unpack(
      chip, bytes, size, pipeline, raw_template);
}
