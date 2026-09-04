/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_code_container_core.h"

#include <string.h>

void
agx3_build_container_constant_program(
   uint8_t image[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE], bool fragment)
{
   static const uint8_t fragment_stop[] = {
      0x0e, 0x00, 0x00, 0x00,
   };
   static const uint8_t vertex_compute[] = {
      0x03, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00,
      0x00, 0x60, 0x00, 0x0e, 0x00, 0x00, 0x00,
   };

   const uint8_t *program = fragment ? fragment_stop : vertex_compute;
   const size_t program_size =
      fragment ? sizeof(fragment_stop) : sizeof(vertex_compute);

   memcpy(image, program, program_size);
   for (size_t offset = program_size;
        offset < AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE; offset += 2) {
      image[offset] = 0x06;
      image[offset + 1] = 0x00;
   }
}
