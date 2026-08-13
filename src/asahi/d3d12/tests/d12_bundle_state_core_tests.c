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
   uint8_t front = 3, back = 4;
   float blend[4] = {1.0f, 1.0f, 1.0f, 1.0f};

   d12_bundle_dynamic_state_apply(&state, &front, &back, blend);
   assert(front == 3 && back == 4 && blend[0] == 1.0f);

   d12_bundle_dynamic_state_set_stencil(&state, 17, 29);
   d12_bundle_dynamic_state_apply(&state, &front, &back, blend);
   assert(front == 17 && back == 29);

   const float expected[4] = {0.25f, 0.5f, 0.75f, 1.0f};
   d12_bundle_dynamic_state_set_blend(&state, expected);
   front = 0;
   back = 0;
   memset(blend, 0, sizeof(blend));
   d12_bundle_dynamic_state_apply(&state, &front, &back, blend);
   assert(front == 17 && back == 29);
   assert(memcmp(blend, expected, sizeof(expected)) == 0);

   puts("d12_bundle_state_core_tests: PASS");
   return 0;
}
