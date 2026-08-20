/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_viewport_math.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   struct d12_viewport_hw_state out[D12_VIEWPORT_STATE_MAX];
   const struct d12_viewport_desc viewports[2] = {
      {.x = 0.0f,
       .y = 0.0f,
       .width = 640.0f,
       .height = 480.0f,
       .min_depth = 0.0f,
       .max_depth = 1.0f},
      {.x = 100.0f,
       .y = 50.0f,
       .width = 200.0f,
       .height = 100.0f,
       .min_depth = 0.25f,
       .max_depth = 0.75f},
   };
   const struct d12_scissor_desc scissors[2] = {
      {.left = 10, .top = 20, .right = 630, .bottom = 470},
      {.left = 125, .top = 60, .right = 250, .bottom = 140},
   };

   assert(d12_viewport_build_hw_state(2, viewports, 2, scissors, 640, 480,
                                      out) == 2);
   assert(out[0].clip[0] == 10 && out[0].clip[1] == 20 &&
          out[0].clip[2] == 630 && out[0].clip[3] == 470);
   assert(out[1].clip[0] == 125 && out[1].clip[1] == 60 &&
          out[1].clip[2] == 250 && out[1].clip[3] == 140);
   assert(out[1].transform[0] == 200.0f &&
          out[1].transform[1] == 100.0f &&
          out[1].transform[2] == 0.25f &&
          out[1].transform[3] == 100.0f &&
          out[1].transform[4] == -50.0f &&
          out[1].transform[5] == 0.5f);

   assert(d12_viewport_build_hw_state(2, viewports, 1, scissors, 640, 480,
                                      out) == 2);
   assert(out[0].clip[2] == 630 && out[0].clip[3] == 470);
   assert(out[1].clip[0] == 0 && out[1].clip[1] == 0 &&
          out[1].clip[2] == 0 && out[1].clip[3] == 0);

   assert(d12_viewport_build_hw_state(1, viewports, 0, NULL, 640, 480,
                                      out) == 1);
   assert(out[0].clip[0] == 0 && out[0].clip[1] == 0 &&
          out[0].clip[2] == 0 && out[0].clip[3] == 0);

   {
      const struct d12_viewport_desc offset_viewports[2] = {
         {.x = 100.25f,
          .y = 50.25f,
          .width = 200.25f,
          .height = 100.25f,
          .min_depth = 0.0f,
          .max_depth = 1.0f},
         {.x = 10.0f,
          .y = 20.0f,
          .width = 500.0f,
          .height = 300.0f,
          .min_depth = 0.0f,
          .max_depth = 1.0f},
      };
      uint32_t width = 0, height = 0;
      assert(d12_viewport_render_extent(2, offset_viewports, &width,
                                        &height));
      assert(width == 510 && height == 320);
      assert(d12_viewport_render_extent(0, NULL, &width, &height));
      assert(width == 1 && height == 1);
   }

   {
      struct d12_viewport_desc invalid = viewports[0];
      invalid.width = -1.0f;
      assert(!d12_viewport_desc_valid(&invalid));
      invalid = viewports[0];
      invalid.min_depth = 0.75f;
      invalid.max_depth = 0.25f;
      assert(!d12_viewport_desc_valid(&invalid));
   }

   assert(d12_viewport_build_hw_state(0, NULL, 0, NULL, 640, 480, out) == 1);
   assert(out[0].clip[0] == 0 && out[0].clip[1] == 0 &&
          out[0].clip[2] == 0 && out[0].clip[3] == 0);

   assert(d12_viewport_build_hw_state(D12_VIEWPORT_STATE_MAX + 1, viewports,
                                      0, NULL, 640, 480, out) == 0);
   assert(d12_viewport_build_hw_state(1, NULL, 0, NULL, 640, 480, out) == 0);
   assert(d12_viewport_build_hw_state(1, viewports, 0, NULL,
                                      UINT16_MAX + UINT32_C(1), 480, out) ==
          0);

   {
      struct d12_viewport_storage_plan plan;
      uint64_t clear_only_bytes = 0;
      uint64_t minimally_drawn_bytes = 0;

      for (uint32_t i = 0; i < 2048; ++i) {
         assert(d12_viewport_plan_storage(false, 256, 16, 64, 12,
                                          &plan));
         clear_only_bytes += plan.scissor_bytes + plan.depth_bias_bytes;
         assert(d12_viewport_plan_storage(true, 256, 16, 64, 12, &plan));
         minimally_drawn_bytes +=
            plan.scissor_bytes + plan.depth_bias_bytes;
      }
      assert(clear_only_bytes == 0);
      assert(minimally_drawn_bytes < 32 * UINT64_C(1024) * 1024);

      assert(d12_viewport_plan_storage(true, 256, 16, 64, 12, &plan));
      assert(plan.scissor_bytes == 4096);
      assert(plan.depth_bias_bytes == 768);
      assert(!d12_viewport_plan_storage(true, 0, 16, 64, 12, &plan));
      assert(!d12_viewport_plan_storage(true, 256, 16, 64, 12, NULL));
   }

   puts("d12_viewport_math_tests: PASS");
   return 0;
}
