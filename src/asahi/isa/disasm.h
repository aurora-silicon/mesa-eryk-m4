/*
 * Copyright 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include "util/bitset.h"

/*
 * One decoder per instruction encoding. AGX2 covers G13/G14 (M1/M2). AGX3 is a
 * different instruction set with two encodings, established by decoding a
 * 274-kernel corpus with each of Apple's own encoder tables:
 *
 *   agx3b -- G15 (M3), G16 (M4) and G17P, all dies.
 *   agx3c -- G17 (M5), all dies.
 *
 * The two AGX3 tables share no decode logic; a stream from one decodes as
 * valid-looking but wrong instructions under the other, so pick by chip via
 * agx_chip_isa_encoding() rather than by trying both.
 */
signed agx2_disassemble_instr(BITSET_WORD *code, FILE *fp, unsigned offset,
                              size_t available, bool verbose);
signed agx3b_disassemble_instr(BITSET_WORD *code, FILE *fp, unsigned offset,
                               size_t available, bool verbose);
signed agx3c_disassemble_instr(BITSET_WORD *code, FILE *fp, unsigned offset,
                               size_t available, bool verbose);

/*
 * Disassemble a whole shader, stopping at max_len.
 *
 * AGX2 additionally stops at the first `trap`, which is what pads the tail of
 * an AGX2 shader. AGX3 has no `trap` instruction at all -- it ends shaders with
 * `end`/`cndend`, which are ordinary instructions worth printing -- and what
 * AGX3 pads with has not been established. So the AGX3 loops decode exactly the
 * length they are given and nothing is inferred about the tail.
 */
#define AGX_DISASSEMBLE_LOOP(prefix, stop_on_trap)                             \
   static inline bool prefix##_disassemble(uint8_t *code, size_t max_len,      \
                                           FILE *fp)                           \
   {                                                                           \
      size_t i = 0;                                                            \
      bool errors = false;                                                     \
                                                                               \
      while (i < max_len) {                                                    \
         size_t available = max_len - i;                                       \
                                                                               \
         if (available < 2) {                                                  \
            fprintf(fp, "XXX truncated instruction\n");                        \
            return true;                                                       \
         }                                                                     \
                                                                               \
         /* Break on trap */                                                   \
         if ((stop_on_trap) && code[i + 0] == 0x08 && code[i + 1] == 0x00)     \
            break;                                                             \
                                                                               \
         signed ret = prefix##_disassemble_instr((BITSET_WORD *)(code + i), fp,\
                                                 i, available, true);          \
         if (ret < 0)                                                          \
            fprintf(fp, "XXX error here\n");                                   \
                                                                               \
         errors |= ret < 0;                                                    \
         unsigned length = abs(ret);                                           \
         if (length == 0 || length > available)                                \
            return true;                                                       \
         i += length;                                                          \
      }                                                                        \
                                                                               \
      return errors;                                                           \
   }

AGX_DISASSEMBLE_LOOP(agx2, true)
AGX_DISASSEMBLE_LOOP(agx3b, false)
AGX_DISASSEMBLE_LOOP(agx3c, false)
