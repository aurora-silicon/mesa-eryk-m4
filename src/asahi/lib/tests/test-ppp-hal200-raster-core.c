/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ppp_hal200_raster_core.h"

#include <assert.h>
#include <string.h>

int
main(void)
{
   const struct agx_ppp_hal200_raster_fields fields = {
      .scissor_enable = true,
      .max_x = 32,
      .max_y = 32,
      .max_z_raw = UINT32_C(0x3f800000),
   };
   static const uint8_t scissor[] = {
      0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
   };
   static const uint8_t no_bias[AGX_PPP_HAL200_DEPTH_BIAS_RECORD_LENGTH] = {0};

   struct agx_ppp_hal200_raster_state static_state, dynamic_state;
   assert(agx_ppp_hal200_raster_state_build(AGX_CHIP_G17P, &fields,
                                             &static_state));
   assert(static_state.fragment_control == UINT32_C(0x00050200));
   assert(static_state.draw_state_scissor_flag == 1);
   assert(memcmp(static_state.scissor, scissor, sizeof(scissor)) == 0);
   assert(memcmp(static_state.depth_bias, no_bias, sizeof(no_bias)) == 0);

   /* Static pipeline state and vkCmdSetViewport/Scissor both end in this same
    * semantic record; their origin must not perturb any hardware byte. */
   assert(agx_ppp_hal200_raster_state_build(AGX_CHIP_G17P, &fields,
                                             &dynamic_state));
   assert(memcmp(&static_state, &dynamic_state, sizeof(static_state)) == 0);

   struct agx_ppp_hal200_raster_fields disabled = fields;
   disabled.scissor_enable = false;
   assert(agx_ppp_hal200_raster_state_build(AGX_CHIP_G17P, &disabled,
                                             &dynamic_state));
   assert(dynamic_state.fragment_control == UINT32_C(0x00040200));
   assert(dynamic_state.draw_state_scissor_flag == 0);

   struct agx_ppp_hal200_raster_fields biased = fields;
   biased.depth_bias_enable = true;
   biased.depth_bias_raw = UINT32_C(0x3dcccccd);
   biased.slope_scale_raw = UINT32_C(0x40000000);
   biased.clamp_raw = UINT32_C(0xbf000000);
   static const uint8_t bias[] = {
      0xcd, 0xcc, 0xcc, 0x3d, 0x00, 0x00,
      0x00, 0x40, 0x00, 0x00, 0x00, 0xbf,
   };
   assert(agx_ppp_hal200_raster_state_build(AGX_CHIP_G17P, &biased,
                                             &dynamic_state));
   assert(dynamic_state.fragment_control == UINT32_C(0x00070200));
   assert(memcmp(dynamic_state.depth_bias, bias, sizeof(bias)) == 0);

   memset(&dynamic_state, 0xa5, sizeof(dynamic_state));
   assert(!agx_ppp_hal200_raster_state_build(AGX_CHIP_G17G, &fields,
                                              &dynamic_state));
   const struct agx_ppp_hal200_raster_state zero = {0};
   assert(memcmp(&dynamic_state, &zero, sizeof(zero)) == 0);
   return 0;
}
