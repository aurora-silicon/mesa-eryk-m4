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
   struct d12_native_descriptor_layout layout = {99, 99};
   size_t value = 0;

   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN7, &layout));
   CHECK(layout.texture_stride_B == 24 && layout.pbe_stride_B == 24);
   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN8, &layout));
   CHECK(layout.texture_stride_B == 24 && layout.pbe_stride_B == 24);
   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN9, &layout));
   CHECK(layout.texture_stride_B == 32 && layout.pbe_stride_B == 32);
   CHECK(d12_native_descriptor_layout(D12_NATIVE_DESCRIPTOR_GEN10, &layout));
   CHECK(layout.texture_stride_B == 32 && layout.pbe_stride_B == 32);

   CHECK(!d12_native_descriptor_layout(0, &layout));
   CHECK(layout.texture_stride_B == 0 && layout.pbe_stride_B == 0);
   CHECK(!d12_native_descriptor_layout(11, &layout));
   CHECK(!d12_native_descriptor_layout(9, NULL));

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
