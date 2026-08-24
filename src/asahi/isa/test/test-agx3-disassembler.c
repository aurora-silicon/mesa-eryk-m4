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

#include "agx3_corpus_lengths.h"

typedef signed (*disassemble_instr_fn)(BITSET_WORD *, FILE *, unsigned,
                                       size_t, bool);

static bool
check(disassemble_instr_fn disassemble, const char *variant,
      const uint8_t *code, size_t code_size, const char *expected)
{
   BITSET_WORD words[BITSET_WORDS(128)] = {0};
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


/*
 * The handful of hand-picked cases above prove specific mnemonics decode.  The
 * real foundation claim is broader: the generated in-tree disassembler must
 * length *every* instruction of real M4/M5 shader code exactly as Apple's own
 * encoder tables do, and must never abort doing it (an AGX3 field range can be
 * 47 bits wide, which asserted before the chunked bits() helper landed).  This
 * replays a pinned corpus slice with the exact expected segmentation.
 */
static unsigned
run_corpus_lengths(void)
{
   FILE *sink = fopen("/dev/null", "w");
   if (sink == NULL)
      sink = stderr;

   unsigned failures = 0;
   for (size_t i = 0; i < agx3_corpus_length_case_count; ++i) {
      const struct agx3_corpus_length_case *c = &agx3_corpus_length_cases[i];
      BITSET_WORD words[BITSET_WORDS(192)] = {0};
      memcpy(words, c->bytes, sizeof(c->bytes));

      signed ret = c->variant == 0
         ? agx3b_disassemble_instr(words, sink, 0, sizeof(c->bytes), false)
         : agx3c_disassemble_instr(words, sink, 0, sizeof(c->bytes), false);
      unsigned got = ret < 0 ? (unsigned)-ret : (unsigned)ret;

      if (got != (unsigned)c->expected_len) {
         if (failures < 8)
            fprintf(stderr,
                    "corpus case %zu (agx3%c): expected length %d, got %d\n",
                    i, c->variant == 0 ? 'b' : 'c', c->expected_len, ret);
         failures++;
      }
   }

   if (sink != stderr)
      fclose(sink);

   printf("AGX3 corpus length agreement: %zu/%zu instructions\n",
          agx3_corpus_length_case_count - failures,
          agx3_corpus_length_case_count);
   return failures;
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
   static const uint8_t end[] = {0x0e, 0x00, 0x00, 0x00};
   static const uint8_t update_node_barrier_b[] = {
      0x07, 0x04, 0x54, 0x08, 0x09, 0x00,
   };
   static const uint8_t update_node_op_b[] = {0xdf, 0x05, 0x54, 0x00};
   static const uint8_t update_node_barrier_c[] = {
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00,
   };
   static const uint8_t update_node_op_c[] = {
      0x27, 0x00, 0x01, 0x00, 0x22, 0x00, 0xad, 0x02,
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

   failures += !check(agx3b_disassemble_instr, "AGX3b end", end, sizeof(end),
                      "end\n");
   failures += !check(agx3c_disassemble_instr, "AGX3c end", end, sizeof(end),
                      "end\n");

   failures += !check(agx3b_disassemble_instr, "AGX3b update.node barrier",
                      update_node_barrier_b, sizeof(update_node_barrier_b),
                      "barrier\n");
   failures += !check(agx3b_disassemble_instr, "AGX3b update.node operation",
                      update_node_op_b, sizeof(update_node_op_b),
                      "update_node\n");
   failures += !check(agx3c_disassemble_instr, "AGX3c update.node barrier",
                      update_node_barrier_c, sizeof(update_node_barrier_c),
                      "barrier\n");
   failures += !check(agx3c_disassemble_instr, "AGX3c update.node operation",
                      update_node_op_c, sizeof(update_node_op_c),
                      "update_node\n");

   printf("AGX3 proven-subset disassembly: %u/12 passed\n", 12 - failures);

   failures += run_corpus_lengths();
   return failures ? 1 : 0;
}
