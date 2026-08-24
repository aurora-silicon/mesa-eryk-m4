/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "../d12_native_descriptor_core.h"

#include <stdio.h>

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

   for (uint32_t generation = 0; generation < 12; ++generation) {
      side.descriptor_generation = generation;
      side.kind = AGX_TEXTURE_SIDE_ENTRY_KIND_BUFFER;
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
