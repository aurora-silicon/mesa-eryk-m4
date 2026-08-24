/*
 * Copyright 2026 Aurora Silicon Project
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asahi/isa/disasm.h"
#include "util/bitset.h"

typedef signed (*disassemble_instr_fn)(BITSET_WORD *, FILE *, unsigned,
                                       size_t, bool);

static bool
check(disassemble_instr_fn disassemble, const char *variant,
      const uint8_t *code, size_t code_size, const char *expected)
{
   BITSET_WORD words[2] = {0};
   char *output = NULL;
   size_t output_size = 0;

   memcpy(words, code, code_size);

   FILE *outputp = open_memstream(&output, &output_size);
   if (outputp == NULL) {
      fprintf(stderr, "%s: open_memstream failed\n", variant);
      return false;
   }

   signed ret = disassemble(words, outputp, 0, code_size, false);
   fclose(outputp);

   bool pass = ret == (signed)code_size && strcmp(output, expected) == 0;
   if (!pass) {
      fprintf(stderr,
              "%s: got length %d and output %s; expected length %zu and "
              "output %s",
              variant, ret, output, code_size, expected);
   }

   free(output);
   return pass;
}

int
main(void)
{
   static const uint8_t short_b[] = {0x0c, 0x01};
   static const uint8_t short_c[] = {0x04, 0x01};
   static const uint8_t long_r0[] = {
      0x0c, 0xf8, 0x02, 0x12, 0x18, 0x08, 0xa2, 0x01,
   };
   static const uint8_t long_r127[] = {
      0xfc, 0xf8, 0xca, 0x12, 0x18, 0x08, 0xa2, 0x01,
   };

   unsigned failures = 0;

   failures += !check(agx3b_disassemble_instr, "AGX3b short", short_b,
                      sizeof(short_b), "ldimm    r0, 1\n");
   failures += !check(agx3c_disassemble_instr, "AGX3c short", short_c,
                      sizeof(short_c), "ldimm    r0, 1\n");

   failures += !check(agx3b_disassemble_instr, "AGX3b long r0", long_r0,
                      sizeof(long_r0), "ldimm    r0, 305419896\n");
   failures += !check(agx3c_disassemble_instr, "AGX3c long r0", long_r0,
                      sizeof(long_r0), "ldimm    r0, 305419896\n");

   failures += !check(agx3b_disassemble_instr, "AGX3b long r127", long_r127,
                      sizeof(long_r127), "ldimm    r127, 305419896\n");
   failures += !check(agx3c_disassemble_instr, "AGX3c long r127", long_r127,
                      sizeof(long_r127), "ldimm    r127, 305419896\n");

   printf("AGX3 ldimm disassembly: %u/6 passed\n", 6 - failures);
   return failures ? 1 : 0;
}
