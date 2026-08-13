/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_present_format_core.h"
#include "../../libagx/present_blt.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                     \
   do {                                                                      \
      if (!(condition)) {                                                    \
         fprintf(stderr, "d12_present_format_core_tests: failed at line "   \
                         "%d\n",                                            \
                 __LINE__);                                                  \
         exit(1);                                                            \
      }                                                                      \
   } while (0)

static void
check_format(uint32_t format, uint32_t shader_format, uint32_t pixel_size_B,
             bool srgb)
{
   struct d12_present_format_info info;

   CHECK(d12_present_format_core_get(format, &info));
   CHECK(info.shader_format == shader_format);
   CHECK(info.pixel_size_B == pixel_size_B);
   CHECK(info.srgb == srgb);
   CHECK(info.srgb == LIBAGX_PRESENT_FORMAT_IS_SRGB(info.shader_format));
   CHECK(d12_present_format_core_supported(format));
}

int
main(void)
{
   uint32_t supported_count = 0;
   struct d12_present_format_info untouched = {
      .shader_format = UINT32_C(0xfeed),
      .pixel_size_B = UINT32_C(0xbeef),
      .srgb = true,
   };

   check_format(D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM,
                LIBAGX_PRESENT_FORMAT_RGBA8, 4, false);
   check_format(D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM_SRGB,
                LIBAGX_PRESENT_FORMAT_RGBA8_SRGB, 4, true);
   check_format(D12_PRESENT_CORE_FORMAT_R10G10B10A2_UNORM,
                LIBAGX_PRESENT_FORMAT_RGB10A2, 4, false);
   check_format(D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM_SRGB,
                LIBAGX_PRESENT_FORMAT_BGRA8_SRGB, 4, true);
   check_format(D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM,
                LIBAGX_PRESENT_FORMAT_BGRA8, 4, false);
   check_format(D12_PRESENT_CORE_FORMAT_B8G8R8X8_UNORM,
                LIBAGX_PRESENT_FORMAT_BGRX8, 4, false);
   check_format(D12_PRESENT_CORE_FORMAT_R16G16B16A16_FLOAT,
                LIBAGX_PRESENT_FORMAT_RGBA16_FLOAT, 8, false);
   check_format(D12_PRESENT_CORE_FORMAT_B5G6R5_UNORM,
                LIBAGX_PRESENT_FORMAT_BGR565, 2, false);
   check_format(D12_PRESENT_CORE_FORMAT_B5G5R5A1_UNORM,
                LIBAGX_PRESENT_FORMAT_BGRA5551, 2, false);

   CHECK(!d12_present_format_core_get(90, &untouched));
   CHECK(untouched.shader_format == UINT32_C(0xfeed));
   CHECK(!d12_present_format_core_supported(0));

   for (uint32_t format = 0; format < 192; ++format) {
      struct d12_present_format_info info;
      if (!d12_present_format_core_get(format, &info))
         continue;
      ++supported_count;
      CHECK(info.shader_format > LIBAGX_PRESENT_FORMAT_INVALID);
      CHECK(info.shader_format <= LIBAGX_PRESENT_FORMAT_BGRA8_SRGB);
      CHECK(info.pixel_size_B == 2 || info.pixel_size_B == 4 ||
            info.pixel_size_B == 8);
      CHECK(info.srgb ==
            (format == D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM_SRGB ||
             format == D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM_SRGB));
   }
   CHECK(supported_count == 9);

   puts("d12_present_format_core_tests: PASS");
   return 0;
}
