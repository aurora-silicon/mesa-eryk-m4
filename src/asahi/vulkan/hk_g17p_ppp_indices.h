/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* HAL200 PPP selector bit8 is followed by two array indices, not enable
 * flags. AGXMetalG17P handleScissorUpdates (447cf4..447cf8/447c8c) stores
 * (record - base) / 16 in the low half; handleDepthBiasUpdates (447ef0/
 * 447f28) supplies the high half. encodeAndEmitRenderState (454ac0..454adc)
 * copies the header and packed indices unchanged. The fragment-control
 * scissor/depth-bias enables are separate state.
 *
 * The bounded single-viewport path appends one record of each kind whenever
 * it emits native bind state, and later draws retain the latest records.
 * Reject malformed/empty arrays instead of selecting an unowned slot.
 */
static inline bool
hk_g17p_ppp_indices_pack(size_t scissor_bytes, size_t depth_bias_bytes,
                         uint8_t output[8])
{
   if (!output || scissor_bytes == 0 || depth_bias_bytes == 0 ||
       scissor_bytes % 16 != 0 || depth_bias_bytes % 12 != 0)
      return false;

   const size_t scissor_index = scissor_bytes / 16 - 1;
   const size_t depth_bias_index = depth_bias_bytes / 12 - 1;
   if (scissor_index > UINT16_MAX || depth_bias_index > UINT16_MAX)
      return false;

   const uint8_t record[8] = {
      0x00, 0x01, 0x00, 0x00,
      (uint8_t)scissor_index, (uint8_t)(scissor_index >> 8),
      (uint8_t)depth_bias_index, (uint8_t)(depth_bias_index >> 8),
   };
   memcpy(output, record, sizeof(record));
   return true;
}
