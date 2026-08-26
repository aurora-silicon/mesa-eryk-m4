/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "../d12_native_descriptor_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
         return 1;                                                             \
      }                                                                        \
   } while (0)

int
main(void)
{
   struct d12_native_descriptor_layout layout = {99, 99, 99};
   size_t value = 0;

   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN7, &layout));
   CHECK(layout.texture_stride_B == 24 && layout.pbe_stride_B == 24 &&
         layout.texture_side_stride_B == 0);
   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN8, &layout));
   CHECK(layout.texture_stride_B == 24 && layout.pbe_stride_B == 24 &&
         layout.texture_side_stride_B == 0);
   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN9, &layout));
   CHECK(layout.texture_stride_B == 32 && layout.pbe_stride_B == 32 &&
         layout.texture_side_stride_B == 8);
   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN10, &layout));
   CHECK(layout.texture_stride_B == 32 && layout.pbe_stride_B == 32 &&
         layout.texture_side_stride_B == 8);

   CHECK(!d12_native_descriptor_layout(0, &layout));
   CHECK(layout.texture_stride_B == 0 && layout.pbe_stride_B == 0 &&
         layout.texture_side_stride_B == 0);
   CHECK(!d12_native_descriptor_layout(11, &layout));
   CHECK(!d12_native_descriptor_layout(9, NULL));

   static const uint8_t channels[] = {
      0x00, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x21,
      0x23, 0x25, 0x26, 0x27, 0x28, 0x31, 0x32, 0x38,
   };
   static const uint8_t expected_gen9[] = {
      0x22, 0x0a, 0xe4, 0xff, 0xc0, 0x1f, 0x00, 0x00,
      0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x00, 0x10,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   static const uint8_t expected_gen10[] = {
      0x22, 0x0a, 0xe4, 0xff, 0x80, 0x3f, 0x00, 0x00,
      0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x00, 0x10,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   struct agx_pbe_agx3_2d_fields pbe = {
      .layout = 2,
      .channels = 0x28,
      .swizzle_r = 0,
      .swizzle_g = 1,
      .swizzle_b = 2,
      .swizzle_a = 3,
      .width = 256,
      .height = 128,
      .buffer = UINT64_C(0x0000123456789ab0),
      .page_aligned_layers = true,
   };
   struct agx_pbe_agx3_descriptor native_pbe;
   CHECK(d12_native_pbe_agx3_2d_pack(D12_NATIVE_DESCRIPTOR_GEN9, &pbe,
                                      &native_pbe));
   CHECK(native_pbe.length == sizeof(expected_gen9));
   CHECK(memcmp(native_pbe.bytes, expected_gen9, sizeof(expected_gen9)) == 0);
   CHECK(d12_native_pbe_agx3_2d_pack(D12_NATIVE_DESCRIPTOR_GEN10, &pbe,
                                      &native_pbe));
   CHECK(memcmp(native_pbe.bytes, expected_gen10, sizeof(expected_gen10)) ==
         0);

   const uint32_t pbe_generations[] = {
      D12_NATIVE_DESCRIPTOR_GEN9,
      D12_NATIVE_DESCRIPTOR_GEN10,
   };
   for (unsigned g = 0;
        g < sizeof(pbe_generations) / sizeof(pbe_generations[0]); ++g)
      for (uint8_t pbe_layout = 1; pbe_layout <= 2; ++pbe_layout)
         for (unsigned c = 0; c < sizeof(channels) / sizeof(channels[0]); ++c)
            for (uint8_t type = 0; type <= 5; ++type)
               for (unsigned swizzle = 0; swizzle < 256; ++swizzle) {
                  pbe.layout = pbe_layout;
                  pbe.channels = channels[c];
                  pbe.type = type;
                  pbe.swizzle_r = swizzle & 3;
                  pbe.swizzle_g = (swizzle >> 2) & 3;
                  pbe.swizzle_b = (swizzle >> 4) & 3;
                  pbe.swizzle_a = (swizzle >> 6) & 3;
                  pbe.width = pbe_generations[g] ==
                                    D12_NATIVE_DESCRIPTOR_GEN9
                                 ? 16384
                                 : 32768;
                  pbe.height = 1;
                  pbe.page_aligned_layers = (swizzle & 1) != 0;
                  CHECK(d12_native_pbe_agx3_2d_pack(
                     pbe_generations[g], &pbe, &native_pbe));
               }

   memset(&native_pbe, 0xa5, sizeof(native_pbe));
   CHECK(!d12_native_pbe_agx3_2d_pack(D12_NATIVE_DESCRIPTOR_GEN8, &pbe,
                                       &native_pbe));
   CHECK(memcmp(&native_pbe, &(struct agx_pbe_agx3_descriptor){0},
                sizeof(native_pbe)) == 0);
   pbe.layout = 0;
   CHECK(!d12_native_pbe_agx3_2d_pack(D12_NATIVE_DESCRIPTOR_GEN9, &pbe,
                                       &native_pbe));
   CHECK(!d12_native_pbe_agx3_2d_pack(D12_NATIVE_DESCRIPTOR_GEN9, NULL,
                                       &native_pbe));
   CHECK(!d12_native_pbe_agx3_2d_pack(D12_NATIVE_DESCRIPTOR_GEN9, &pbe,
                                       NULL));

   struct d12_native_texture_side_entry_info side = {
      .descriptor_generation = D12_NATIVE_DESCRIPTOR_GEN9,
      .form = AGX_TEXTURE_SIDE_ENTRY_FORM_NORMAL,
      .kind = AGX_TEXTURE_SIDE_ENTRY_KIND_BUFFER,
      .auxiliary_heap_index = 0x89abcdef,
      .value.buffer_width_texels = 0x12345678,
   };
   uint64_t side_word = UINT64_MAX;
   CHECK(d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == UINT64_C(0x89abcdef12345678));

   struct d12_native_texture_side_entry_decoded side_decoded = {0, 0};
   CHECK(d12_native_texture_side_entry_decode(side_word, &side_decoded));
   CHECK(side_decoded.payload == 0x12345678);
   CHECK(side_decoded.auxiliary_heap_index == 0x89abcdef);

   side.descriptor_generation = D12_NATIVE_DESCRIPTOR_GEN10;
   side.value.buffer_width_texels = UINT32_MAX;
   side.auxiliary_heap_index = UINT32_MAX;
   CHECK(d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == UINT64_MAX);

   side.kind = AGX_TEXTURE_SIDE_ENTRY_KIND_ORDINARY_IMAGE;
   side.auxiliary_heap_index = 7;
   CHECK(d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == (UINT64_C(7) << 32));

   side.descriptor_generation = D12_NATIVE_DESCRIPTOR_GEN9;
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);

   side.kind = AGX_TEXTURE_SIDE_ENTRY_KIND_GEN9_ATOMIC_IMAGE;
   side.auxiliary_heap_index = 3;
   side.value.gen9_atomic = (struct agx_texture_side_entry_gen9_atomic_info){
      .eligible = true,
      .layout_class = AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_2D,
      .level_offset_bytes = UINT64_C(0x123480),
   };
   CHECK(d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == (UINT64_C(3) << 32 | UINT64_C(0x2469)));

   side.form = AGX_TEXTURE_SIDE_ENTRY_FORM_DIRECT_UNRESOLVED;
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);
   side.form = AGX_TEXTURE_SIDE_ENTRY_FORM_NORMAL;

   side.descriptor_generation = D12_NATIVE_DESCRIPTOR_GEN10;
   side.form = AGX_TEXTURE_SIDE_ENTRY_FORM_DIRECT;
   side.kind = AGX_TEXTURE_SIDE_ENTRY_KIND_TIER1_SPARSE_RESIDENCY;
   side.auxiliary_heap_index = 0;
   side.value.sparse_residency.width_tiles = 3;
   side.value.sparse_residency.height_tiles = 5;
   CHECK(d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == UINT64_C(0x40a0000040400000));

   side.descriptor_generation = D12_NATIVE_DESCRIPTOR_GEN9;
   side.value.sparse_residency.width_tiles = UINT64_C(0x1000000);
   side.value.sparse_residency.height_tiles = 1;
   CHECK(d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == UINT64_C(0x3f8000004b800000));

   side.value.sparse_residency.width_tiles = UINT64_C(0x1000001);
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);
   side.value.sparse_residency.width_tiles = 1;
   side.auxiliary_heap_index = 1;
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);
   side.auxiliary_heap_index = 0;
   side.form = AGX_TEXTURE_SIDE_ENTRY_FORM_NORMAL;
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);
   side.form = AGX_TEXTURE_SIDE_ENTRY_FORM_DIRECT;

   for (uint32_t generation = 0; generation < 12; ++generation) {
      side.descriptor_generation = generation;
      side.kind = AGX_TEXTURE_SIDE_ENTRY_KIND_BUFFER;
      side.form = AGX_TEXTURE_SIDE_ENTRY_FORM_NORMAL;
      side.auxiliary_heap_index = 3;
      side.value.buffer_width_texels = 1;
      const bool supported = generation == D12_NATIVE_DESCRIPTOR_GEN9 ||
                             generation == D12_NATIVE_DESCRIPTOR_GEN10;
      CHECK(d12_native_texture_side_entry_pack(&side, &side_word) == supported);
      CHECK(side_word == (supported ? (UINT64_C(3) << 32 | 1) : 0));
   }

   side.descriptor_generation = D12_NATIVE_DESCRIPTOR_GEN9;
   side.value.buffer_width_texels = 0;
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);
   side.value.buffer_width_texels = UINT64_C(0x100000000);
   CHECK(!d12_native_texture_side_entry_pack(&side, &side_word));
   CHECK(side_word == 0);
   CHECK(!d12_native_texture_side_entry_pack(NULL, &side_word));
   CHECK(side_word == 0);
   CHECK(!d12_native_texture_side_entry_pack(&side, NULL));
   CHECK(!d12_native_texture_side_entry_decode(side_word, NULL));

   uint64_t sample_field = UINT64_MAX;
   CHECK(d12_native_texture_gen9_sample_field(false, 1, 0, &sample_field));
   CHECK(sample_field == 0);
   CHECK(d12_native_texture_gen9_sample_field(true, 2, 0, &sample_field));
   CHECK(sample_field == UINT64_C(0x0) << 56);
   CHECK(d12_native_texture_gen9_sample_field(true, 4, 1, &sample_field));
   CHECK(sample_field == UINT64_C(0x1) << 56);
   CHECK(d12_native_texture_gen9_sample_field(true, 8, 2, &sample_field));
   CHECK(sample_field == UINT64_C(0x2) << 56);

   /* There is no one-sample encoding. Non-MS selectors, selector 3 and
    * non-canonical counts must remain fail-closed. */
   CHECK(!d12_native_texture_gen9_sample_field(true, 1, 0, &sample_field));
   CHECK(sample_field == 0);
   CHECK(!d12_native_texture_gen9_sample_field(false, 2, 0, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(false, 1, 1, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(false, 1, 2, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, 2, 3, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, 4, 0, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, 8, 1, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, 16, 3, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, 3, 0, &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, UINT32_MAX, 0,
                                                &sample_field));
   CHECK(!d12_native_texture_gen9_sample_field(true, 2, 0, NULL));

   CHECK(d12_native_pbe_software_addressing_supported(
      D12_NATIVE_DESCRIPTOR_GEN7));
   CHECK(d12_native_pbe_software_addressing_supported(
      D12_NATIVE_DESCRIPTOR_GEN8));
   CHECK(!d12_native_pbe_software_addressing_supported(
      D12_NATIVE_DESCRIPTOR_GEN9));
   CHECK(!d12_native_pbe_software_addressing_supported(
      D12_NATIVE_DESCRIPTOR_GEN10));
   CHECK(!d12_native_pbe_software_addressing_supported(0));
   CHECK(!d12_native_pbe_software_addressing_supported(11));

   CHECK(d12_native_descriptor_span(24, 7, 0, &value) && value == 168);
   CHECK(d12_native_descriptor_span(32, 7, 1, &value) && value == 256);
   CHECK(d12_native_descriptor_offset(24, 17, &value) && value == 408);
   CHECK(d12_native_descriptor_offset(32, 17, &value) && value == 544);
   CHECK(!d12_native_descriptor_span(0, 1, 0, &value));
   CHECK(!d12_native_descriptor_span(32, SIZE_MAX, 1, &value));
   CHECK(!d12_native_descriptor_span(32, SIZE_MAX / 32 + 1, 0, &value));
   CHECK(!d12_native_descriptor_span(32, 1, 0, NULL));

   CHECK(!d12_native_pbe_has_levels(false, 0, 1));
   CHECK(d12_native_pbe_has_levels(false, 1, 1));
   CHECK(d12_native_pbe_has_levels(false, 0, 2));
   CHECK(!d12_native_pbe_has_levels(true, 1, 8));

   printf("d12 native descriptor core: %u checks passed\n", checks);
   return 0;
}
