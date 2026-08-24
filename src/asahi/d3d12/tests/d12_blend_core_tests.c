/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_blend_core.h"
#include "../d12_bundle_state_core.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   assert(d12_alpha_blend_factor_classify(D12_CORE_BLEND_ALPHA_FACTOR) ==
          D12_ALPHA_BLEND_FACTOR_CONSTANT);
   assert(d12_alpha_blend_factor_classify(
             D12_CORE_BLEND_INV_ALPHA_FACTOR) ==
          D12_ALPHA_BLEND_FACTOR_INVERTED);
   assert(d12_alpha_blend_factor_classify(0) ==
          D12_ALPHA_BLEND_FACTOR_NONE);
   assert(d12_alpha_blend_factor_classify(UINT32_MAX) ==
          D12_ALPHA_BLEND_FACTOR_NONE);

   /* Reset and ClearState must restore the component consumed by
    * D3D12_BLEND_ALPHA_FACTOR to the D3D12 default, 1.0f. */
   const struct d12_dynamic_state_values reset =
      d12_dynamic_state_reset(false, 9.0f, 9.0f, 9.0f, 2);
   for (unsigned i = 0; i < 4; ++i)
      assert(reset.blend_factor[i] == D12_CORE_DEFAULT_BLEND_FACTOR);

   puts("d12_blend_core_tests: PASS (8 checks)");
   return 0;
}
