/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_hal200_direct_draw_core.h"

#include <string.h>

static void
store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static uint32_t
load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static bool
primitive_supported(uint8_t primitive)
{
   switch (primitive) {
   case 0:
   case 1:
   case 3:
   case 5:
   case 6:
   case 9:
   case 11:
   case 13:
   case 14:
   case 15:
      return true;
   default:
      return false;
   }
}

bool
agx_vdm_hal200_direct_draw_pack(
   enum agx_chip chip, const struct agx_vdm_hal200_direct_draw_fields *fields,
   struct agx_vdm_hal200_direct_draw *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !fields || chip != AGX_CHIP_G17P ||
       fields->draw_state_context_offset == 0 ||
       (fields->draw_state_context_offset & UINT32_C(3)) != 0 ||
       !primitive_supported(fields->primitive) || fields->vertex_count == 0 ||
       fields->instance_count == 0)
      return false;

   store_le32(&out->bytes[0], fields->draw_state_context_offset);
   store_le32(&out->bytes[4],
              (AGX_VDM_HAL200_DIRECT_DRAW_OPCODE << 16) |
                 ((uint32_t)fields->primitive << 8));
   store_le32(&out->bytes[8], fields->vertex_count);
   store_le32(&out->bytes[12], fields->instance_count);
   store_le32(&out->bytes[16], fields->vertex_start);
   store_le32(&out->bytes[20], AGX_VDM_HAL200_DIRECT_DRAW_TAIL);
   out->length = AGX_VDM_HAL200_DIRECT_DRAW_LENGTH;
   return true;
}

bool
agx_vdm_hal200_direct_draw_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_hal200_direct_draw_fields *fields)
{
   if (fields)
      memset(fields, 0, sizeof(*fields));

   if (!bytes || !fields || chip != AGX_CHIP_G17P ||
       size != AGX_VDM_HAL200_DIRECT_DRAW_LENGTH)
      return false;

   const uint8_t *raw = bytes;
   const uint32_t header = load_le32(&raw[4]);
   const uint32_t offset = load_le32(&raw[0]);
   const uint8_t primitive = (uint8_t)(header >> 8);
   if (offset == 0 || (offset & 3) != 0 ||
       (header & UINT32_C(0xffff00ff)) !=
          (AGX_VDM_HAL200_DIRECT_DRAW_OPCODE << 16) ||
       !primitive_supported(primitive) || load_le32(&raw[8]) == 0 ||
       load_le32(&raw[12]) == 0 ||
       load_le32(&raw[20]) != AGX_VDM_HAL200_DIRECT_DRAW_TAIL)
      return false;

   fields->draw_state_context_offset = offset;
   fields->primitive = primitive;
   fields->vertex_count = load_le32(&raw[8]);
   fields->instance_count = load_le32(&raw[12]);
   fields->vertex_start = load_le32(&raw[16]);
   return true;
}
