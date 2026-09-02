/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_hal200_direct_draw_core.h"

#include <assert.h>
#include <string.h>

int
main(void)
{
   const struct agx_vdm_hal200_direct_draw_fields fields = {
      .draw_state_context_offset = 0x48000,
      .primitive = 6,
      .vertex_count = 3,
      .instance_count = 1,
      .vertex_start = 0,
   };
   static const uint32_t expected[] = {
      0x00048000, 0x61c40600, 3, 1, 0, 0xc0000000,
   };

   struct agx_vdm_hal200_direct_draw packed;
   assert(agx_vdm_hal200_direct_draw_pack(AGX_CHIP_G17P, &fields, &packed));
   assert(packed.length == sizeof(expected));
   assert(memcmp(packed.bytes, expected, sizeof(expected)) == 0);

   struct agx_vdm_hal200_direct_draw_fields unpacked;
   assert(agx_vdm_hal200_direct_draw_unpack(
      AGX_CHIP_G17P, packed.bytes, packed.length, &unpacked));
   assert(memcmp(&unpacked, &fields, sizeof(fields)) == 0);
   assert(!agx_vdm_hal200_direct_draw_pack(AGX_CHIP_G17G, &fields, &packed));
   return 0;
}
