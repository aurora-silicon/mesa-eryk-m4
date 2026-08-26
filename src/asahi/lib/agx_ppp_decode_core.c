/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ppp_decode_core.h"

#include <string.h>

bool
agx_ppp_decode_one(enum agx_chip chip, const void *payload, size_t size,
                   struct agx_ppp_decoded_record *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!payload || !out || size == 0 ||
       size > AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH)
      return false;

   struct agx_ppp_decoded_record candidate = {0};
   unsigned matches = 0;

   struct agx_ppp_w_clamp_cull2_fields w_clamp_cull2;
   if (agx_ppp_w_clamp_cull2_core_unpack(chip, payload, size,
                                         &w_clamp_cull2)) {
      candidate = (struct agx_ppp_decoded_record){
         .kind = AGX_PPP_DECODED_RECORD_W_CLAMP_CULL2,
         .length = size,
         .data.w_clamp_cull2 = w_clamp_cull2,
      };
      matches++;
   }

   struct agx_ppp_viewport_payload_fields viewport;
   if (agx_ppp_viewport_payload_core_unpack(chip, payload, size, &viewport)) {
      candidate = (struct agx_ppp_decoded_record){
         .kind = AGX_PPP_DECODED_RECORD_VIEWPORT,
         .length = size,
         .data.viewport = viewport,
      };
      matches++;
   }

   struct agx_ppp_hal300_depth_stencil_fields depth_stencil;
   if (agx_ppp_hal300_depth_stencil_core_unpack(chip, payload, size,
                                                &depth_stencil)) {
      candidate = (struct agx_ppp_decoded_record){
         .kind = AGX_PPP_DECODED_RECORD_HAL300_DEPTH_STENCIL,
         .length = size,
         .data.hal300_depth_stencil = depth_stencil,
      };
      matches++;
   }

   struct agx_ppp_hal300_depth_bias_scissor_bases_fields bases;
   if (agx_ppp_hal300_depth_bias_scissor_bases_unpack(chip, payload, size,
                                                      &bases)) {
      candidate = (struct agx_ppp_decoded_record){
         .kind =
            AGX_PPP_DECODED_RECORD_HAL300_DEPTH_BIAS_SCISSOR_BASES,
         .length = size,
         .data.hal300_depth_bias_scissor_bases = bases,
      };
      matches++;
   }

   if (matches != 1)
      return false;

   *out = candidate;
   return true;
}
