/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ppp_hal200_graphics_core.h"
#include "agx_vdm_state_core.h"

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
bundle_layout(enum agx_ppp_hal200_graphics_bundle bundle, uint32_t *header,
              unsigned *payload_words)
{
   switch (bundle) {
   case AGX_PPP_HAL200_GRAPHICS_FS:
      *header = AGX_PPP_HAL200_FS_HEADER;
      *payload_words = 6;
      return true;
   case AGX_PPP_HAL200_GRAPHICS_OUTPUT:
      *header = AGX_PPP_HAL200_OUTPUT_HEADER;
      *payload_words = 6;
      return true;
   case AGX_PPP_HAL200_GRAPHICS_PRIMARY_FACE:
      *header = AGX_PPP_HAL200_PRIMARY_FACE_HEADER;
      *payload_words = 6;
      return true;
   case AGX_PPP_HAL200_GRAPHICS_SECONDARY_FACE:
      *header = AGX_PPP_HAL200_SECONDARY_FACE_HEADER;
      *payload_words = 4;
      return true;
   case AGX_PPP_HAL200_GRAPHICS_CULL:
      *header = AGX_PPP_HAL200_CULL_HEADER;
      *payload_words = 1;
      return true;
   default:
      *header = 0;
      *payload_words = 0;
      return false;
   }
}

bool
agx_ppp_hal200_graphics_core_supported(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17P;
}

bool
agx_ppp_hal200_graphics_core_pack(
   enum agx_chip chip, enum agx_ppp_hal200_graphics_bundle bundle,
   const struct agx_ppp_hal200_graphics_fields *fields,
   struct agx_ppp_hal200_graphics_core *out)
{
   uint32_t header = 0;
   unsigned payload_words = 0;

   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !fields || !agx_ppp_hal200_graphics_core_supported(chip) ||
       !bundle_layout(bundle, &header, &payload_words))
      return false;

   store_le32(out->bytes, header);
   for (unsigned i = 0; i < payload_words; ++i)
      store_le32(&out->bytes[4 + (4 * i)], fields->words[i]);

   out->length = (uint8_t)(4 + (4 * payload_words));
   return true;
}

bool
agx_ppp_hal200_graphics_core_unpack(
   enum agx_chip chip, enum agx_ppp_hal200_graphics_bundle bundle,
   const void *bytes, size_t size,
   struct agx_ppp_hal200_graphics_fields *fields)
{
   uint32_t header = 0;
   unsigned payload_words = 0;

   if (fields)
      memset(fields, 0, sizeof(*fields));

   if (!bytes || !fields ||
       !agx_ppp_hal200_graphics_core_supported(chip) ||
       !bundle_layout(bundle, &header, &payload_words) ||
       size != 4 + (4 * payload_words))
      return false;

   const uint8_t *raw = bytes;
   if (load_le32(raw) != header)
      return false;

   for (unsigned i = 0; i < payload_words; ++i)
      fields->words[i] = load_le32(&raw[4 + (4 * i)]);

   return true;
}

bool
agx_ppp_hal200_fragment_core_pack(
   enum agx_chip chip, const struct agx_ppp_hal200_fragment_fields *fields,
   struct agx_ppp_hal200_graphics_core *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!fields || !out || (fields->varying_table & UINT32_C(0x3f)) != 0)
      return false;

   struct agx_vdm_state_vertex_pipeline_pair stage_program;
   if (!agx_vdm_state_vertex_pipeline_pair_pack(
          chip, fields->stage_program, fields->stage_program_template,
          &stage_program))
      return false;

   const struct agx_ppp_hal200_graphics_fields raw = {
      .words = {
         fields->resource_counts,
         fields->varying_table,
         fields->unknown_0c,
         load_le32(&stage_program.bytes[0]),
         load_le32(&stage_program.bytes[4]),
         fields->flags,
      },
   };
   return agx_ppp_hal200_graphics_core_pack(
      chip, AGX_PPP_HAL200_GRAPHICS_FS, &raw, out);
}
