/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_code_container_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
check_program(const char *label, bool fragment, const uint8_t *prefix,
              size_t prefix_size)
{
   uint8_t actual[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE];
   memset(actual, 0xa5, sizeof(actual));
   agx3_build_container_constant_program(actual, fragment);

   if (memcmp(actual, prefix, prefix_size) != 0)
      goto mismatch;
   for (size_t offset = prefix_size; offset < sizeof(actual); offset += 2) {
      if (actual[offset] != 0x06 || actual[offset + 1] != 0x00)
         goto mismatch;
   }

   return 0;

mismatch:
   fprintf(stderr, "%s G17P container program mismatch\n", label);
   return 1;
}

int
main(void)
{
   /* Apple Metal 32023.917, applegpu_g17p, one device-buffer float2 load and
    * one position output.  The nested vertex __TEXT,__text starts with this
    * exact pre-entry program and puts its main entry at +0x40. */
   static const uint8_t vertex_compute_prefix[] = {
      0x03, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00,
      0x60, 0x00, 0x0e, 0x00, 0x00, 0x00,
   };

   /* The matched constant-colour/no-varying fragment object instead has a
    * bare stop followed by the same 0x0006 fill up to its +0x40 main entry. */
   static const uint8_t fragment_prefix[] = {
      0x0e, 0x00, 0x00, 0x00,
   };

   return check_program("vertex/compute", false, vertex_compute_prefix,
                        sizeof(vertex_compute_prefix)) ||
          check_program("fragment", true, fragment_prefix,
                        sizeof(fragment_prefix));
}
