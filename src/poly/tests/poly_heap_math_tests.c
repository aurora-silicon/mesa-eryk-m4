/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../poly_heap_math.h"

int
main(void)
{
   assert(poly_heap_allocation_fits(0, 64, 64));
   assert(poly_heap_allocation_fits(48, 16, 64));
   assert(!poly_heap_allocation_fits(49, 16, 64));
   assert(!poly_heap_allocation_fits(0, 65, 64));

   /* Validate each atomic allocation from its returned offset. A later
    * allocation may advance the shared bottom without invalidating an
    * earlier exact-fit result. */
   assert(poly_heap_allocation_fits(0, 32, 64));
   assert(poly_heap_allocation_fits(32, 32, 64));

   puts("poly_heap_math_tests: PASS");
   return 0;
}
