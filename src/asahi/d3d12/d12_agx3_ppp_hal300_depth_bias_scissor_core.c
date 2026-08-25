/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_ppp_hal300_depth_bias_scissor_core.h"

#include "asahi/lib/agx_ppp_hal300_depth_bias_scissor_core.h"

#include <string.h>

_Static_assert(D12_AGX3_PPP_HAL300_DEPTH_BIAS_RECORD_LENGTH ==
                  AGX_PPP_HAL300_DEPTH_BIAS_RECORD_LENGTH,
               "D3D and shared depth-bias lengths must agree");
_Static_assert(D12_AGX3_PPP_HAL300_SCISSOR_RECORD_LENGTH ==
                  AGX_PPP_HAL300_SCISSOR_RECORD_LENGTH,
               "D3D and shared scissor lengths must agree");
_Static_assert(D12_AGX3_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH ==
                  AGX_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH,
               "D3D and shared base-block lengths must agree");

bool
d12_agx3_encode_ppp_hal300_depth_bias_record(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_depth_bias_record_fields *fields,
   struct d12_agx3_ppp_hal300_depth_bias_record *out)
{
   if (out)
      memset(out, 0, sizeof(*out));
   if (!out || !fields)
      return false;

   const struct agx_ppp_hal300_depth_bias_record_fields shared_fields = {
      .depth_bias_raw = fields->depth_bias_raw,
      .slope_scale_raw = fields->slope_scale_raw,
      .clamp_raw = fields->clamp_raw,
   };
   struct agx_ppp_hal300_depth_bias_record shared;
   if (!agx_ppp_hal300_depth_bias_record_pack(chip, &shared_fields, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared depth-bias storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_ppp_hal300_depth_bias_record(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_depth_bias_record_fields *fields)
{
   if (fields)
      memset(fields, 0, sizeof(*fields));
   if (!fields)
      return false;

   struct agx_ppp_hal300_depth_bias_record_fields shared;
   if (!agx_ppp_hal300_depth_bias_record_unpack(chip, bytes, size, &shared))
      return false;

   fields->depth_bias_raw = shared.depth_bias_raw;
   fields->slope_scale_raw = shared.slope_scale_raw;
   fields->clamp_raw = shared.clamp_raw;
   return true;
}

bool
d12_agx3_encode_ppp_hal300_scissor_record(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_scissor_record_fields *fields,
   struct d12_agx3_ppp_hal300_scissor_record *out)
{
   if (out)
      memset(out, 0, sizeof(*out));
   if (!out || !fields)
      return false;

   const struct agx_ppp_hal300_scissor_record_fields shared_fields = {
      .max_x = fields->max_x,
      .min_x = fields->min_x,
      .max_y = fields->max_y,
      .min_y = fields->min_y,
      .min_z_raw = fields->min_z_raw,
      .max_z_raw = fields->max_z_raw,
   };
   struct agx_ppp_hal300_scissor_record shared;
   if (!agx_ppp_hal300_scissor_record_pack(chip, &shared_fields, &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared scissor storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_ppp_hal300_scissor_record(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_scissor_record_fields *fields)
{
   if (fields)
      memset(fields, 0, sizeof(*fields));
   if (!fields)
      return false;

   struct agx_ppp_hal300_scissor_record_fields shared;
   if (!agx_ppp_hal300_scissor_record_unpack(chip, bytes, size, &shared))
      return false;

   fields->max_x = shared.max_x;
   fields->min_x = shared.min_x;
   fields->max_y = shared.max_y;
   fields->min_y = shared.min_y;
   fields->min_z_raw = shared.min_z_raw;
   fields->max_z_raw = shared.max_z_raw;
   return true;
}

bool
d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields *fields,
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases *out)
{
   if (out)
      memset(out, 0, sizeof(*out));
   if (!out || !fields)
      return false;

   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields shared_fields = {
      .depth_bias_base = fields->depth_bias_base,
      .scissor_base = fields->scissor_base,
   };
   struct agx_ppp_hal300_depth_bias_scissor_bases shared;
   if (!agx_ppp_hal300_depth_bias_scissor_bases_pack(chip, &shared_fields,
                                                     &shared))
      return false;

   _Static_assert(sizeof(out->bytes) == sizeof(shared.bytes),
                  "D3D and shared base-block storage must agree");
   memcpy(out->bytes, shared.bytes, sizeof(out->bytes));
   out->length = shared.length;
   return true;
}

bool
d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields *fields)
{
   if (fields)
      memset(fields, 0, sizeof(*fields));
   if (!fields)
      return false;

   struct agx_ppp_hal300_depth_bias_scissor_bases_fields shared;
   if (!agx_ppp_hal300_depth_bias_scissor_bases_unpack(chip, bytes, size,
                                                       &shared))
      return false;

   fields->depth_bias_base = shared.depth_bias_base;
   fields->scissor_base = shared.scissor_base;
   return true;
}
