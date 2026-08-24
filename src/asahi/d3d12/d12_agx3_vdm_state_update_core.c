/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_state_update_core.h"

#include "asahi/lib/agx_vdm_state_update_core.h"

#include <string.h>

bool
d12_agx3_encode_vdm_state_update(
   enum agx_chip chip, const struct d12_agx3_vdm_state_update_info *info,
   struct d12_agx3_vdm_state_update *out)
{
   struct agx_vdm_state_update_info shared_info;
   struct agx_vdm_state_update shared;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info)
      return false;

   shared_info.raw_vertex_shader_word0 = info->raw_vertex_shader_word0;
   shared_info.pipeline = info->pipeline;
   shared_info.raw_pipeline_template = info->raw_pipeline_template;
   shared_info.raw_vertex_outputs[0] = info->raw_vertex_outputs[0];
   shared_info.raw_vertex_outputs[1] = info->raw_vertex_outputs[1];
   shared_info.raw_vertex_unknown = info->raw_vertex_unknown;

   if (!agx_vdm_state_update_pack(chip, &shared_info, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared VDM State Update storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_vdm_state_update(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_vdm_state_update_info *info)
{
   struct agx_vdm_state_update_info shared_info;

   if (info)
      memset(info, 0, sizeof(*info));

   if (!info || !agx_vdm_state_update_unpack(chip, bytes, size, &shared_info))
      return false;

   info->raw_vertex_shader_word0 = shared_info.raw_vertex_shader_word0;
   info->pipeline = shared_info.pipeline;
   info->raw_pipeline_template = shared_info.raw_pipeline_template;
   info->raw_vertex_outputs[0] = shared_info.raw_vertex_outputs[0];
   info->raw_vertex_outputs[1] = shared_info.raw_vertex_outputs[1];
   info->raw_vertex_unknown = shared_info.raw_vertex_unknown;
   return true;
}
