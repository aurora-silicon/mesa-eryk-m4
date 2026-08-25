/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_hal300_depth_bias_scissor.h"

bool
hk_agx3_encode_ppp_hal300_depth_bias_record(
   enum agx_chip chip,
   const struct agx_ppp_hal300_depth_bias_record_fields *fields,
   struct agx_ppp_hal300_depth_bias_record *out)
{
   return agx_ppp_hal300_depth_bias_record_pack(chip, fields, out);
}

bool
hk_agx3_decode_ppp_hal300_depth_bias_record(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_depth_bias_record_fields *fields)
{
   return agx_ppp_hal300_depth_bias_record_unpack(chip, payload, size, fields);
}

bool
hk_agx3_encode_ppp_hal300_scissor_record(
   enum agx_chip chip,
   const struct agx_ppp_hal300_scissor_record_fields *fields,
   struct agx_ppp_hal300_scissor_record *out)
{
   return agx_ppp_hal300_scissor_record_pack(chip, fields, out);
}

bool
hk_agx3_decode_ppp_hal300_scissor_record(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_scissor_record_fields *fields)
{
   return agx_ppp_hal300_scissor_record_unpack(chip, payload, size, fields);
}

bool
hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip,
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields *fields,
   struct agx_ppp_hal300_depth_bias_scissor_bases *out)
{
   return agx_ppp_hal300_depth_bias_scissor_bases_pack(chip, fields, out);
}

bool
hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_depth_bias_scissor_bases_fields *fields)
{
   return agx_ppp_hal300_depth_bias_scissor_bases_unpack(chip, payload, size,
                                                         fields);
}
