/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exercises the AGX3 instruction-selection core: that every grade-A milestone
 * op encodes byte-for-byte what its hand-written packer produces, and that the
 * core fails closed on every out-of-domain instruction.
 */

#include "agx3_select_core.h"
#include "agx3_pack.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);          \
         failures++;                                                           \
      }                                                                        \
   } while (0)

static const enum agx_isa_encoding encs[] = {AGX_ISA_ENCODING_AGX3B,
                                              AGX_ISA_ENCODING_AGX3C};
static const uint16_t regs[] = {0, 2, 4, 42, 126};

static struct agx3_sel_src
reg32(uint16_t r)
{
   return (struct agx3_sel_src){.reg = r, .is_reg = true, .abs = false,
                                .neg = false, .size = AGX3_SEL_SIZE_32};
}

/* Encode through the core and require it matches `want`. */
static void
expect_bytes(enum agx_isa_encoding enc, const struct agx3_sel_instr *I,
             const uint8_t *want, uint8_t want_len)
{
   uint8_t out[AGX3_SEL_MAX_LENGTH];
   uint8_t len = 0;
   CHECK(agx3_select_encode(enc, I, out, &len));
   CHECK(len == want_len);
   CHECK(memcmp(out, want, want_len) == 0);
}

/* Encode through the core and require it refuses. */
static void
expect_refused(enum agx_isa_encoding enc, const struct agx3_sel_instr *I)
{
   uint8_t out[AGX3_SEL_MAX_LENGTH];
   uint8_t len = 0xff;
   CHECK(!agx3_select_encode(enc, I, out, &len));
}

static struct agx3_sel_instr
unop(enum agx3_sel_op op, uint16_t d, uint16_t a)
{
   struct agx3_sel_instr I = {0};
   I.op = op;
   I.has_dest = true;
   I.dest = d;
   I.dest_size = AGX3_SEL_SIZE_32;
   I.nr_srcs = 1;
   I.src[0] = reg32(a);
   return I;
}

static struct agx3_sel_instr
binop(enum agx3_sel_op op, uint16_t d, uint16_t a, uint16_t bb)
{
   struct agx3_sel_instr I = {0};
   I.op = op;
   I.has_dest = true;
   I.dest = d;
   I.dest_size = AGX3_SEL_SIZE_32;
   I.nr_srcs = 2;
   I.src[0] = reg32(a);
   I.src[1] = reg32(bb);
   return I;
}

static void
test_matches_hand_packers(void)
{
   for (unsigned e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc = encs[e];

      /* Nullary terminators. */
      {
         struct agx3_encoded_end end;
         CHECK(agx3_pack_end(enc, &end));
         struct agx3_sel_instr I = {.op = AGX3_SEL_OP_END};
         expect_bytes(enc, &I, end.bytes, end.length);

         struct agx3_encoded_threadgroup_barrier bar;
         CHECK(agx3_pack_threadgroup_barrier(enc, &bar));
         struct agx3_sel_instr B = {.op = AGX3_SEL_OP_BARRIER};
         expect_bytes(enc, &B, bar.bytes, bar.length);
      }

      /* Unary float funops over the register spread. */
      for (unsigned r = 0; r < sizeof(regs) / sizeof(regs[0]); ++r) {
         for (unsigned s = 0; s < sizeof(regs) / sizeof(regs[0]); ++s) {
            const uint16_t d = regs[r], a = regs[s];
            struct agx3_encoded_f32_rcp_reg rcp;
            if (agx3_pack_f32_rcp_reg(enc, d, a, &rcp)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_F_RCP, d, a);
               expect_bytes(enc, &I, rcp.bytes, rcp.length);
            }
            struct agx3_encoded_f32_rsqrt_reg rsq;
            if (agx3_pack_f32_rsqrt_reg(enc, d, a, &rsq)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_F_RSQRT, d, a);
               expect_bytes(enc, &I, rsq.bytes, rsq.length);
            }
            struct agx3_encoded_f32_rint_reg rint;
            if (agx3_pack_f32_rint_reg(enc, d, a, &rint)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_F_RINT, d, a);
               expect_bytes(enc, &I, rint.bytes, rint.length);
            }
         }
      }

      /* mov_imm over a spread of dest registers and immediates. */
      static const uint32_t imms[] = {0, 1, 0x1234, 0xdeadbeef, 0xffffffff};
      for (unsigned r = 0; r < sizeof(regs) / sizeof(regs[0]); ++r) {
         for (unsigned m = 0; m < sizeof(imms) / sizeof(imms[0]); ++m) {
            struct agx3_encoded_ldimm want;
            if (!agx3_pack_ldimm_u32(enc, regs[r], imms[m], &want))
               continue;
            struct agx3_sel_instr I = {.op = AGX3_SEL_OP_MOV_IMM,
                                       .has_dest = true, .dest = regs[r],
                                       .dest_size = AGX3_SEL_SIZE_32,
                                       .imm = imms[m]};
            expect_bytes(enc, &I, want.bytes, want.length);
         }
      }

      /* Register/register and register/register/register ALU forms, byte-exact
       * against their packers over the register sweep. */
      for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
         for (unsigned j = 0; j < sizeof(regs) / sizeof(regs[0]); ++j) {
            const uint16_t d = regs[i], a = regs[j];
            const uint16_t bb = regs[(i + j) % 5];
            const uint16_t cc = regs[(i + 2 * j) % 5];

            struct agx3_encoded_fadd_f32_reg_reg fadd;
            if (agx3_pack_fadd_f32_reg_reg(enc, d, a, bb, &fadd)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_FADD, d, a, bb);
               expect_bytes(enc, &I, fadd.bytes, fadd.length);
            }

            struct agx3_encoded_fmul_f32_reg_reg fmul;
            if (agx3_pack_fmul_f32_reg_reg(enc, d, a, bb, &fmul)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_FMUL, d, a, bb);
               expect_bytes(enc, &I, fmul.bytes, fmul.length);
            }

            struct agx3_encoded_iadd_u32_reg_reg iadd;
            if (agx3_pack_iadd_u32_reg_reg(enc, d, a, bb, &iadd)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_IADD, d, a, bb);
               expect_bytes(enc, &I, iadd.bytes, iadd.length);
            }

            struct agx3_encoded_asr_i32_reg_reg asr;
            if (agx3_pack_asr_i32_reg_reg(enc, d, a, bb, &asr)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_ASR, d, a, bb);
               expect_bytes(enc, &I, asr.bytes, asr.length);
            }

            struct agx3_encoded_sll_u32_reg_reg sll;
            if (agx3_pack_sll_u32_reg_reg(enc, d, a, bb, &sll)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_SLL, d, a, bb);
               expect_bytes(enc, &I, sll.bytes, sll.length);
            }

            struct agx3_encoded_lsr_u32_reg_reg lsr;
            if (agx3_pack_lsr_u32_reg_reg(enc, d, a, bb, &lsr)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_LSR, d, a, bb);
               expect_bytes(enc, &I, lsr.bytes, lsr.length);
            }

            /* Unary 32-bit conversions, byte-exact against the convert packers.
             */
            struct agx3_encoded_f32_to_int_rtz_reg f2i;
            if (agx3_pack_f32_to_int_rtz_reg(enc, AGX3_F32_TO_I32_RTZ, d, a,
                                             &f2i)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_F_TO_I32, d, a);
               expect_bytes(enc, &I, f2i.bytes, f2i.length);
            }
            struct agx3_encoded_f32_to_int_rtz_reg f2u;
            if (agx3_pack_f32_to_int_rtz_reg(enc, AGX3_F32_TO_U32_RTZ, d, a,
                                             &f2u)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_F_TO_U32, d, a);
               expect_bytes(enc, &I, f2u.bytes, f2u.length);
            }
            struct agx3_encoded_i32_to_f32_reg i2f;
            if (agx3_pack_i32_to_f32_reg(enc, d, a, &i2f)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_I32_TO_F, d, a);
               expect_bytes(enc, &I, i2f.bytes, i2f.length);
            }
            struct agx3_encoded_u32_to_f32_reg u2f;
            if (agx3_pack_u32_to_f32_reg(enc, d, a, &u2f)) {
               struct agx3_sel_instr I = unop(AGX3_SEL_OP_U32_TO_F, d, a);
               expect_bytes(enc, &I, u2f.bytes, u2f.length);
            }

            struct agx3_encoded_isub_u32_reg_reg isub;
            if (agx3_pack_isub_u32_reg_reg(enc, d, a, bb, &isub)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_ISUB, d, a, bb);
               expect_bytes(enc, &I, isub.bytes, isub.length);
            }

            struct agx3_encoded_imul_u32_reg_reg imul;
            if (agx3_pack_imul_u32_reg_reg(enc, d, a, bb, &imul)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_IMUL, d, a, bb);
               expect_bytes(enc, &I, imul.bytes, imul.length);
            }

            struct agx3_encoded_fma_f32_reg_reg_reg fma;
            if (agx3_pack_fma_f32_reg_reg_reg(enc, d, a, bb, cc, &fma)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_FFMA, d, a, bb);
               I.nr_srcs = 3;
               I.src[2] = reg32(cc);
               expect_bytes(enc, &I, fma.bytes, fma.length);
            }

            struct agx3_encoded_imadd_u32_reg_reg_reg imad;
            if (agx3_pack_imadd_u32_reg_reg_reg(enc, d, a, bb, cc, &imad)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_IMAD, d, a, bb);
               I.nr_srcs = 3;
               I.src[2] = reg32(cc);
               expect_bytes(enc, &I, imad.bytes, imad.length);
            }

            /* bitop and/or/xor by truth table, byte-exact against the bitwise
             * packers. */
            struct agx3_encoded_bit_and_u32_reg_reg band;
            if (agx3_pack_bit_and_u32_reg_reg(enc, d, a, bb, &band)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_BITOP, d, a, bb);
               I.truth_table = AGX3_SEL_TT_AND;
               expect_bytes(enc, &I, band.bytes, band.length);
            }
            struct agx3_encoded_bit_or_u32_reg_reg bor;
            if (agx3_pack_bit_or_u32_reg_reg(enc, d, a, bb, &bor)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_BITOP, d, a, bb);
               I.truth_table = AGX3_SEL_TT_OR;
               expect_bytes(enc, &I, bor.bytes, bor.length);
            }
            struct agx3_encoded_bit_xor_u32_reg_reg bxor;
            if (agx3_pack_bit_xor_u32_reg_reg(enc, d, a, bb, &bxor)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_BITOP, d, a, bb);
               I.truth_table = AGX3_SEL_TT_XOR;
               expect_bytes(enc, &I, bxor.bytes, bxor.length);
            }
            /* NOT is unary; the second source is ignored, and its truth table
             * depends only on the first operand. */
            struct agx3_encoded_bit_not_u32_reg bnot;
            if (agx3_pack_bit_not_u32_reg(enc, d, a, &bnot)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_BITOP, d, a, bb);
               I.truth_table = AGX3_SEL_TT_NOT;
               expect_bytes(enc, &I, bnot.bytes, bnot.length);
               /* Even with a non-register second source it still encodes. */
               I.src[1].is_reg = false;
               expect_bytes(enc, &I, bnot.bytes, bnot.length);
            }
            /* A move lowers to iadd d, s, #0. */
            struct agx3_encoded_iadd_u32_imm8 mov;
            if (agx3_pack_iadd_u32_imm8(enc, d, a, 0, &mov)) {
               struct agx3_sel_instr I = binop(AGX3_SEL_OP_BITOP, d, a, bb);
               I.truth_table = AGX3_SEL_TT_MOV;
               expect_bytes(enc, &I, mov.bytes, mov.length);
            }
         }
      }
   }
}

static void
test_fails_closed(void)
{
   const enum agx_isa_encoding enc = AGX_ISA_ENCODING_AGX3B;

   /* Wrong encoding: AGX2 has its own packer. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_FADD, 0, 2, 4);
      expect_refused(AGX_ISA_ENCODING_AGX2, &I);
      expect_refused(AGX_ISA_ENCODING_UNKNOWN, &I);
   }

   /* Unmapped op. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_UNSUPPORTED, 0, 2, 4);
      expect_refused(enc, &I);
      CHECK(!agx3_select_op_supported(AGX3_SEL_OP_UNSUPPORTED));
      CHECK(agx3_select_op_supported(AGX3_SEL_OP_FADD));
   }

   /* Float modifiers cannot be expressed by the no-modifier packers. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_FADD, 0, 2, 4);
      I.src[0].neg = true;
      expect_refused(enc, &I);
      I.src[0].neg = false;
      I.src[1].abs = true;
      expect_refused(enc, &I);
      I.src[1].abs = false;
      I.saturate = true;
      expect_refused(enc, &I);
   }

   /* Non-32-bit sizes. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_FMUL, 0, 2, 4);
      I.src[0].size = 0; /* 16-bit */
      expect_refused(enc, &I);
      I.src[0].size = AGX3_SEL_SIZE_32;
      I.dest_size = 2; /* 64-bit */
      expect_refused(enc, &I);
   }

   /* Immediate / uniform sources. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_IADD, 0, 2, 4);
      I.src[1].is_reg = false;
      expect_refused(enc, &I);
   }

   /* A shift on the integer add/mad forms. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_IADD, 0, 2, 4);
      I.shift = 1;
      expect_refused(enc, &I);
   }

   /* An unmapped bitop truth table (nand = 0x7) fails closed; the identity
    * (0xA) is a move and is accepted, tested positively in the sweep. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_BITOP, 0, 2, 4);
      I.truth_table = 0x7; /* nand */
      expect_refused(enc, &I);
      I.truth_table = 0x1; /* nor */
      expect_refused(enc, &I);
   }

   /* Wrong source count. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_FFMA, 0, 2, 4);
      /* ffma needs 3 sources; binop() left nr_srcs == 2. */
      expect_refused(enc, &I);
   }

   /* Odd (mis-aligned) 32-bit register: the packers require even halves. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_IADD, 1, 2, 4);
      expect_refused(enc, &I);
   }

   /* mov_imm with a non-32-bit dest. */
   {
      struct agx3_sel_instr I = {.op = AGX3_SEL_OP_MOV_IMM, .has_dest = true,
                                 .dest = 0, .dest_size = 0, .imm = 1};
      expect_refused(enc, &I);
   }

   /* Null out/len arguments. */
   {
      struct agx3_sel_instr I = binop(AGX3_SEL_OP_FADD, 0, 2, 4);
      uint8_t out[AGX3_SEL_MAX_LENGTH];
      uint8_t len;
      CHECK(!agx3_select_encode(enc, NULL, out, &len));
      CHECK(!agx3_select_encode(enc, &I, NULL, &len));
      CHECK(!agx3_select_encode(enc, &I, out, NULL));
   }
}

int
main(void)
{
   test_matches_hand_packers();
   test_fails_closed();

   if (failures) {
      fprintf(stderr, "agx3_select_core: %u of %u checks failed\n", failures,
              checks);
      return 1;
   }

   printf("agx3_select_core: all %u checks passed\n", checks);
   return 0;
}
