/* SPDX-License-Identifier: MIT */
#include "hk_native_descriptor_pack.h"

#include <assert.h>
#include <string.h>

int
main(void)
{
   struct AGX_TEXTURE semantic = {
      AGX_TEXTURE_header,
      .dimension = AGX_TEXTURE_DIMENSION_2D,
      .layout = AGX_LAYOUT_LINEAR,
      .channels = AGX_CHANNELS_R8,
      .type = AGX_TEXTURE_TYPE_UINT,
      .width = 16384,
      .height = 2,
      .depth = 1,
      .stride = 16384 - 16,
      .address = 0x100004000ull,
      .buffer_size_sw = 16384,
   };
   struct hk_native_texture_descriptor packed;
   for (unsigned tail = 0; tail < 16; ++tail) {
      semantic.buffer_offset_sw = tail;
      assert(hk_pack_native_texture(9, &packed, &semantic));
      struct AGX_TEXTURE_GEN9 decoded;
      AGX_TEXTURE_GEN9_unpack(NULL, (const uint8_t *)&packed, &decoded);
      assert(decoded.address == semantic.address);
      assert(decoded.width == 16384 && decoded.height == 2);
      assert(decoded.buffer_size_sw == 16384);
      assert(decoded.buffer_offset_sw == tail);
      assert(!decoded.extended);
      /* Both descriptor crawls read identical software-owned offset bits. */
      struct AGX_TEXTURE crawl;
      AGX_TEXTURE_unpack(NULL, (const uint8_t *)&packed, &crawl);
      assert(crawl.buffer_offset_sw == tail);
      assert(crawl.buffer_size_sw == 16384);
      if (tail)
         assert(!hk_pack_native_texture(10, &packed, &semantic));
   }

   /* Word 2 must never overlap a native extension payload. */
   semantic.extended = true;
   assert(!hk_pack_native_texture(9, &packed, &semantic));
   semantic.extended = false;
   semantic.first_level = semantic.last_level = 1;
   assert(!hk_pack_native_texture(9, &packed, &semantic));
   semantic.first_level = semantic.last_level = 0;
   semantic.layout = AGX_LAYOUT_GPU;
   assert(!hk_pack_native_texture(9, &packed, &semantic));
   semantic.layout = AGX_LAYOUT_LINEAR;
   semantic.buffer_size_sw = 0;
   assert(!hk_pack_native_texture(9, &packed, &semantic));

   /* The software PBE retains the tail, but the hardware PBE deliberately
    * rejects it: buffer-view creation must separate the two representations.
    */
   struct AGX_PBE pbe = {
      AGX_PBE_header,
      .dimension = AGX_TEXTURE_DIMENSION_2D,
      .layout = AGX_LAYOUT_LINEAR,
      .channels = AGX_CHANNELS_R8,
      .type = AGX_TEXTURE_TYPE_UINT,
      .width = 16384,
      .height = 2,
      .stride = 16384 - 4,
      .layers = 1,
      .levels = 1,
      .buffer = 0x100004000ull,
      .buffer_offset_sw = 15,
   };
   struct hk_native_pbe_descriptor native;
   assert(!hk_pack_native_pbe(9, &native, &pbe));
   uint32_t software[AGX_PBE_LENGTH / 4];
   AGX_PBE_pack(software, &pbe);
   struct AGX_PBE decoded;
   AGX_PBE_unpack(NULL, (const uint8_t *)&software, &decoded);
   assert(decoded.buffer_offset_sw == 15 && decoded.buffer == pbe.buffer);
   pbe.buffer_offset_sw = 0;
   assert(hk_pack_native_pbe(9, &native, &pbe));
   return 0;
}
