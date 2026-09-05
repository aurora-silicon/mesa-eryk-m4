/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_hal200_direct_draw_core.h"

#include <assert.h>
#include <string.h>

static void
check_indexed(void)
{
   /* Valid native194053 doorbell1: PPP draw-state offset0x48000, restart
    * disabled with retained 0xffff state, uint16 indices and a 48-bit VA. */
   struct agx_vdm_hal200_indexed_draw_fields f = {
      .draw_state_context_offset = 0x48000,
      .index_buffer = UINT64_C(0x10001bc0180),
      .index_buffer_range_B = 0x3fe00,
      .index_count = 6,
      .instance_count = 1,
   };
   const uint32_t native[] = {
      0x48000, 0x40000001, 0xffff, 0x61f20600, 0x01bc0180,
      6, 1, 0, 0xff7f, 1,
   };
   struct agx_vdm_hal200_indexed_draw packed;
   assert(agx_vdm_hal200_indexed_draw_pack(AGX_CHIP_G17P, &f, &packed));
   assert(packed.length == sizeof(native));
   assert(memcmp(packed.bytes, native, sizeof(native)) == 0);

   /* firstIndex is in uint16 elements and reduces the remaining byte range.
    * Relocating an equivalent bound buffer must preserve the complete packet. */
   f.index_buffer -= 6;
   f.index_buffer_range_B += 6;
   f.first_index = 3;
   assert(agx_vdm_hal200_indexed_draw_pack(AGX_CHIP_G17P, &f, &packed));
   assert(memcmp(packed.bytes, native, sizeof(native)) == 0);
   const int32_t biases[] = {INT32_MIN, -7, -1, 0, 1, 7, INT32_MAX};
   for (unsigned i = 0; i < sizeof(biases) / sizeof(biases[0]); ++i) {
      f.base_vertex = biases[i];
      assert(agx_vdm_hal200_indexed_draw_pack(AGX_CHIP_G17P, &f, &packed));
      uint32_t word;
      memcpy(&word, packed.bytes + 28, 4);
      assert(word == (uint32_t)biases[i]);
   }

   uint64_t relocated;
   uint32_t size_word;
   assert(agx_vdm_hal200_u16_index_range(0x1000, 12, 0, 6,
                                         &relocated, &size_word));
   assert(relocated == 0x1000 && size_word == 2);
   assert(agx_vdm_hal200_u16_index_range(0x1000, 14, 1, 6,
                                         &relocated, &size_word));
   assert(relocated == 0x1002 && size_word == 2);
   assert(agx_vdm_hal200_u16_index_range(0x1000, 2, 0, 1,
                                         &relocated, &size_word));
   assert(size_word == 0);
   assert(agx_vdm_hal200_u16_index_range(0x1000, UINT32_MAX, 0, 1,
                                         &relocated, &size_word));
   assert(size_word == 0x3fffffff);
   assert(!agx_vdm_hal200_u16_index_range(0x1000, 12, 1, 6,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, UINT32_MAX, UINT32_MAX, 1,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, UINT32_MAX, 0, UINT32_MAX,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, UINT64_C(1) << 32, 0, 1,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range((UINT64_C(1) << 48) - 2, 4, 0, 1,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1001, 12, 0, 6,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0, 12, 0, 6,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, 0, 0, 1,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, 12, 0, 0,
                                          &relocated, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, 12, 0, 6, NULL, &size_word));
   assert(!agx_vdm_hal200_u16_index_range(0x1000, 12, 0, 6, &relocated, NULL));
   f.instance_count = 2;
   assert(!agx_vdm_hal200_indexed_draw_pack(AGX_CHIP_G17P, &f, &packed));
   f.instance_count = 1;
   f.draw_state_context_offset |= 1;
   assert(!agx_vdm_hal200_indexed_draw_pack(AGX_CHIP_G17P, &f, &packed));
   f.draw_state_context_offset = 0x48000;
   assert(!agx_vdm_hal200_indexed_draw_pack(AGX_CHIP_G17G, &f, &packed));
}

int
main(void)
{
   check_indexed();
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
