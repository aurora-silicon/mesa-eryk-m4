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
      0x00048000, 0x61c40600, 3, 1, 0,
   };

   struct agx_vdm_hal200_direct_draw packed;
   assert(agx_vdm_hal200_direct_draw_pack(AGX_CHIP_G17P, &fields, &packed));
   assert(packed.length == sizeof(expected));
   assert(memcmp(packed.bytes, expected, sizeof(expected)) == 0);

   struct agx_vdm_hal200_direct_draw_fields unpacked;
   assert(agx_vdm_hal200_direct_draw_unpack(
      AGX_CHIP_G17P, packed.bytes, packed.length, &unpacked));
   assert(memcmp(&unpacked, &fields, sizeof(fields)) == 0);

   /* The original single-draw fixture is unchanged after the caller appends
    * its separate END. Consecutive draw bodies must not manufacture ENDs.
    */
   static const uint32_t end = 0xc0000000;
   uint8_t stream[44];
   memcpy(stream, packed.bytes, packed.length);
   memcpy(stream + packed.length, &end, sizeof(end));
   static const uint32_t old_single[] = {
      0x00048000, 0x61c40600, 3, 1, 0, 0xc0000000,
   };
   assert(!memcmp(stream, old_single, sizeof(old_single)));
   memcpy(stream + packed.length, packed.bytes, packed.length);
   memcpy(stream + 2 * packed.length, &end, sizeof(end));
   for (unsigned i = 0; i < 2; ++i) {
      assert(agx_vdm_hal200_direct_draw_unpack(
         AGX_CHIP_G17P, stream + i * packed.length, packed.length, &unpacked));
      assert(unpacked.vertex_count == 3);
   }
   assert(!memcmp(stream + 40, &end, sizeof(end)));

   /* A payload may equal the terminal opcode. Finalization must not inspect
    * vertex_start and mistake it for an already emitted stream terminator.
    */
   struct agx_vdm_hal200_direct_draw_fields endword_start = fields;
   endword_start.vertex_start = end;
   assert(agx_vdm_hal200_direct_draw_pack(AGX_CHIP_G17P, &endword_start, &packed));
   assert(agx_vdm_hal200_direct_draw_unpack(
      AGX_CHIP_G17P, packed.bytes, packed.length, &unpacked));
   assert(unpacked.vertex_start == end);
   assert(!agx_vdm_hal200_direct_draw_unpack(
      AGX_CHIP_G17P, stream, sizeof(old_single), &unpacked));
   for (size_t size = 0; size < AGX_VDM_HAL200_DIRECT_DRAW_LENGTH; ++size)
      assert(!agx_vdm_hal200_direct_draw_unpack(
         AGX_CHIP_G17P, stream, size, &unpacked));
   assert(!agx_vdm_hal200_direct_draw_pack(AGX_CHIP_G17G, &fields, &packed));
   return 0;
}
