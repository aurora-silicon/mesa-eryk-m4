/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_state_update_core.h"

#include "agx_vdm_state_core.h"

#include <string.h>

static void
agx_vdm_state_update_store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static uint32_t
agx_vdm_state_update_load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

bool
agx_vdm_state_update_supported(enum agx_chip chip)
{
   return agx_vdm_state_vertex_pipeline_pair_supported(chip);
}

bool
agx_vdm_state_update_pack(enum agx_chip chip,
                          const struct agx_vdm_state_update_info *info,
                          struct agx_vdm_state_update *out)
{
   struct agx_vdm_state_vertex_pipeline_pair pair;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info || !agx_vdm_state_vertex_pipeline_pair_pack(
                           chip, info->pipeline, info->raw_pipeline_template,
                           &pair))
      return false;

   _Static_assert(sizeof(pair.bytes) == 8,
                  "AGX3 VDM pipeline pair must occupy two words");
   agx_vdm_state_update_store_le32(&out->bytes[0],
                                   AGX_VDM_STATE_UPDATE_HEADER);
   agx_vdm_state_update_store_le32(&out->bytes[4],
                                   info->raw_vertex_shader_word0);
   memcpy(&out->bytes[8], pair.bytes, sizeof(pair.bytes));
   agx_vdm_state_update_store_le32(&out->bytes[16],
                                   info->raw_vertex_outputs[0]);
   agx_vdm_state_update_store_le32(&out->bytes[20],
                                   info->raw_vertex_outputs[1]);
   agx_vdm_state_update_store_le32(&out->bytes[24],
                                   info->raw_vertex_unknown);
   out->length = AGX_VDM_STATE_UPDATE_LENGTH;
   return true;
}

bool
agx_vdm_state_update_unpack(enum agx_chip chip, const void *bytes, size_t size,
                            struct agx_vdm_state_update_info *info)
{
   if (info)
      memset(info, 0, sizeof(*info));

   if (!bytes || !info || size != AGX_VDM_STATE_UPDATE_LENGTH ||
       !agx_vdm_state_update_supported(chip))
      return false;

   const uint8_t *raw = bytes;
   if (agx_vdm_state_update_load_le32(&raw[0]) !=
       AGX_VDM_STATE_UPDATE_HEADER)
      return false;

   if (!agx_vdm_state_vertex_pipeline_pair_unpack(
          chip, &raw[8], AGX_VDM_STATE_VERTEX_PIPELINE_PAIR_LENGTH,
          &info->pipeline, &info->raw_pipeline_template)) {
      memset(info, 0, sizeof(*info));
      return false;
   }

   info->raw_vertex_shader_word0 = agx_vdm_state_update_load_le32(&raw[4]);
   info->raw_vertex_outputs[0] = agx_vdm_state_update_load_le32(&raw[16]);
   info->raw_vertex_outputs[1] = agx_vdm_state_update_load_le32(&raw[20]);
   info->raw_vertex_unknown = agx_vdm_state_update_load_le32(&raw[24]);
   return true;
}
