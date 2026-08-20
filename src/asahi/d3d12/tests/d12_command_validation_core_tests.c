/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_command_validation_core.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   assert(!d12_command_has_graphics_pipeline(false, false));
   assert(!d12_command_has_graphics_pipeline(false, true));
   assert(d12_command_has_graphics_pipeline(true, false));
   assert(!d12_command_has_graphics_pipeline(true, true));

   assert(!d12_command_has_compute_pipeline(false, false));
   assert(!d12_command_has_compute_pipeline(false, true));
   assert(!d12_command_has_compute_pipeline(true, false));
   assert(d12_command_has_compute_pipeline(true, true));

   puts("d12_command_validation_core_tests: PASS");
   return 0;
}
