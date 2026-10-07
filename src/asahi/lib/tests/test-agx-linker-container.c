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

static int
check_auxiliary_block(void)
{
   /* Apple 32023.917's 128-byte RT0 EOT compiler text comprises a 64-byte
    * constant program plus this 64-byte main (the prefix is tested above).
    * It must not become a raw LDSHDR entry at allocation +0x40. */
   static const uint8_t eot_main[64] = {
      0x3c, 0xa0, 0x10, 0x06, 0x2c, 0xa1, 0x10, 0x26,
      0x0b, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00,
      0x97, 0x05, 0x54, 0x04, 0x00, 0x00, 0x00, 0x00,
      0x50, 0x50, 0x10, 0x00, 0x0b, 0x07, 0x2f, 0x00,
      0x42, 0x20, 0x00, 0x08, 0x00, 0x00, 0x1b, 0x05,
      0x2f, 0x00, 0x42, 0x40, 0x00, 0x08, 0x00, 0x00,
      0x57, 0x40, 0x54, 0x00, 0x00, 0x00, 0x00, 0x80,
      0xaa, 0x75, 0x02, 0x00, 0x0e, 0x00, 0x00, 0x00,
   };
   uint8_t constant[64], image[0x101];
   agx3_build_container_constant_program(constant, true);
   memset(image, 0xa5, sizeof(image));
   if (agx3_code_block_size(128) != 0x100 ||
       !agx3_build_code_block(image, sizeof(image), eot_main, sizeof(eot_main),
                               128, constant, true) ||
       memcmp(image, (uint8_t[]){0, 1, 0, 0}, 4) ||
       memcmp(image + 0x40, constant, 64) ||
       memcmp(image + 0x80, eot_main, 64) || image[0x100] != 0xa5)
      return fprintf(stderr, "EOT CODE block framing mismatch\n"), 1;
   for (unsigned i = 0xc0; i < 0x100; ++i) {
      if (image[i] != 0)
         return fprintf(stderr, "EOT fetch padding is not zero\n"), 1;
   }

   /* A raw clear body has no compiler constant prefix. Fill that prefix
    * without moving the main relative to the shared CODE block contract. */
   memset(image, 0xa5, sizeof(image));
   if (agx3_code_block_size(65) != 0x100 ||
       !agx3_build_code_block(image, sizeof(image), eot_main, 63, 63, NULL, true) ||
       memcmp(image + 0x40, constant, 64) ||
       memcmp(image + 0x80, eot_main, 63) || image[0xbf] != 0 ||
       image[0xc0] != 0xa5 ||
       agx3_code_block_size(0) || agx3_code_block_size(SIZE_MAX) ||
       agx3_build_code_block(image, 0xbf, eot_main, 64, 64, constant, true) ||
       agx3_build_code_block(image, sizeof(image), eot_main, 64, 63, constant,
                               true))
      return fprintf(stderr, "auxiliary CODE block bounds mismatch\n"), 1;
   return 0;
}

/* With scheduling stalls the vertex/compute program keeps its two
 * instructions and puts the stall words between them; 0 words is the
 * unstalled program. */
static int
check_stalled_program(const uint8_t *prefix, size_t prefix_size)
{
   static const uint8_t stall[] = {0x06, 0xfc};
   const size_t first = 10;
   for (unsigned words = 0; words <= AGX3_CONTAINER_CONSTANT_PROGRAM_MAX_STALL_WORDS;
        ++words) {
      uint8_t actual[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE];
      uint8_t want[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE];
      memset(actual, 0xa5, sizeof(actual));
      agx3_build_container_constant_program_stalled(actual, false, words);

      size_t offset = 0;
      memcpy(want, prefix, first);
      offset = first;
      for (unsigned i = 0; i < words; ++i, offset += sizeof(stall))
         memcpy(want + offset, stall, sizeof(stall));
      memcpy(want + offset, prefix + first, prefix_size - first);
      offset += prefix_size - first;
      for (; offset < sizeof(want); offset += 2) {
         want[offset] = 0x06;
         want[offset + 1] = 0x00;
      }
      if (memcmp(actual, want, sizeof(want)) != 0) {
         fprintf(stderr, "stalled container program mismatch (%u words)\n",
                 words);
         return 1;
      }

      /* A fragment program is a single stop and takes no stall words. */
      uint8_t fragment[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE];
      uint8_t plain[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE];
      agx3_build_container_constant_program_stalled(fragment, true, words);
      agx3_build_container_constant_program(plain, true);
      if (memcmp(fragment, plain, sizeof(plain)) != 0) {
         fprintf(stderr, "stalled fragment program mismatch\n");
         return 1;
      }
   }
   return 0;
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
                        sizeof(fragment_prefix)) ||
          check_stalled_program(vertex_compute_prefix,
                                sizeof(vertex_compute_prefix)) ||
          check_auxiliary_block();
}
