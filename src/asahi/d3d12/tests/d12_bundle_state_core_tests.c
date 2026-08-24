/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_bundle_state_core.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   struct d12_bundle_dynamic_state state = {0};
   struct d12_dynamic_state_values inherited = {
      .stencil_ref_front = 3,
      .stencil_ref_back = 4,
      .blend_factor = {1.0f, 1.0f, 1.0f, 1.0f},
      .depth_bias = 5.0f,
      .depth_bias_clamp = 6.0f,
      .slope_scaled_depth_bias = 7.0f,
      .strip_cut = 2,
   };

   d12_bundle_dynamic_state_apply(&state, &inherited);
   assert(inherited.stencil_ref_front == 3 &&
          inherited.stencil_ref_back == 4);
   assert(inherited.blend_factor[0] == 1.0f);
   assert(inherited.depth_bias == 5.0f && inherited.strip_cut == 2);

   d12_bundle_dynamic_state_set_stencil(&state, 17, 29);
   d12_bundle_dynamic_state_apply(&state, &inherited);
   assert(inherited.stencil_ref_front == 17 &&
          inherited.stencil_ref_back == 29);

   const float expected[4] = {0.25f, 0.5f, 0.75f, 1.0f};
   d12_bundle_dynamic_state_set_blend(&state, expected);
   inherited.stencil_ref_front = 0;
   inherited.stencil_ref_back = 0;
   memset(inherited.blend_factor, 0, sizeof(inherited.blend_factor));
   d12_bundle_dynamic_state_apply(&state, &inherited);
   assert(inherited.stencil_ref_front == 17 &&
          inherited.stencil_ref_back == 29);
   assert(memcmp(inherited.blend_factor, expected, sizeof(expected)) == 0);

   /* SetPipelineState overwrites prior dynamic PSO values, while leaving
    * independently dynamic stencil and blend state untouched. */
   const struct d12_dynamic_state_values pso =
      d12_dynamic_state_reset(true, -1.25f, 2.5f, 3.75f, 1);
   d12_bundle_dynamic_state_set_pipeline(&state, &pso);
   d12_bundle_dynamic_state_apply(&state, &inherited);
   assert(inherited.depth_bias == -1.25f);
   assert(inherited.depth_bias_clamp == 2.5f);
   assert(inherited.slope_scaled_depth_bias == 3.75f);
   assert(inherited.strip_cut == 1);
   assert(inherited.stencil_ref_front == 17 &&
          inherited.stencil_ref_back == 29);
   assert(memcmp(inherited.blend_factor, expected, sizeof(expected)) == 0);

   d12_bundle_dynamic_state_set_depth_bias(&state, 8.0f, 9.0f, 10.0f);
   d12_bundle_dynamic_state_set_strip_cut(&state, 2);
   d12_bundle_dynamic_state_apply(&state, &inherited);
   assert(inherited.depth_bias == 8.0f &&
          inherited.depth_bias_clamp == 9.0f &&
          inherited.slope_scaled_depth_bias == 10.0f);
   assert(inherited.strip_cut == 2);

   /* Rebinding the PSO after explicit dynamic setters restores its initial
    * values, which is the DDI0099 PSOReset conformance case. */
   d12_bundle_dynamic_state_set_pipeline(&state, &pso);
   d12_bundle_dynamic_state_apply(&state, &inherited);
   assert(inherited.depth_bias == -1.25f && inherited.strip_cut == 1);

   /* Reset/ClearState restore the D3D12 blend-factor default. In particular,
    * Options13's ALPHA_FACTOR consumes component A and requires 1.0f here. A
    * graphics PSO contributes only its other dynamic PSO defaults. */
   const struct d12_dynamic_state_values empty =
      d12_dynamic_state_reset(false, 99.0f, 99.0f, 99.0f, 2);
   assert(empty.stencil_ref_front == 0 && empty.stencil_ref_back == 0);
   assert(empty.blend_factor[0] == 1.0f && empty.blend_factor[3] == 1.0f);
   assert(empty.depth_bias == 0.0f && empty.strip_cut == 0);
   assert(pso.stencil_ref_front == 0 && pso.stencil_ref_back == 0);
   assert(pso.blend_factor[0] == 1.0f && pso.blend_factor[3] == 1.0f);

   int32_t reset_result = 123;
   const int32_t invalid_argument = INT32_C(-2147024809);
   const int32_t generic_failure = INT32_C(-2147467259);
   assert(!d12_command_list_failed_close_reset(
      false, true, invalid_argument, generic_failure, &reset_result));
   assert(!d12_command_list_failed_close_reset(
      true, false, invalid_argument, generic_failure, &reset_result));
   assert(d12_command_list_failed_close_reset(
      true, true, invalid_argument, generic_failure, &reset_result));
   assert(reset_result == invalid_argument);
   assert(d12_command_list_failed_close_reset(
      true, true, 0, generic_failure, &reset_result));
   assert(reset_result == generic_failure);

   puts("d12_bundle_state_core_tests: PASS");
   return 0;
}
