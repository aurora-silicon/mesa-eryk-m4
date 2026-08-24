/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 binary emission: the thin shim between the shared Mesa pipeline and the
 * AGX3 instruction-selection core.
 *
 * The entire front end -- NIR lowering, optimization, register allocation and
 * scheduling -- is encoding-independent and reused verbatim from AGX2. Only the
 * final pack stage forks: where agx_pack_binary emits AGX2, this walks the same
 * scheduled agx_instr IR, normalizes each instruction into agx3_select_core's
 * operand struct, and emits AGX3 bytes.
 *
 * Everything that could be wrong -- the op-to-form mapping, the grounded domain
 * of each form, how operands bind -- lives in agx3_select_core and is tested
 * byte-for-byte in the host suite. This file is the translation and the block
 * walk, nothing more.
 *
 * Milestone one: the grade-A no-modifier 32-bit ALU / move / immediate core,
 * straight-line only (no branches, no memory, no texture). It fails the shader
 * -- never aborts, since this runs inside the UMD -- on any unmapped op or
 * out-of-domain instruction. AGX3's wait model is the fence mask on the
 * terminal `end`, and every real kernel ends with mask 0, so agx_insert_waits
 * does not run for AGX3 and this emits a single canonical `end`.
 */

#include "agx_compiler.h"
#include "agx3_select_core.h"

#include <string.h>

static enum agx3_sel_op
agx3_map_opcode(unsigned op)
{
   switch (op) {
   case AGX_OPCODE_STOP:
      return AGX3_SEL_OP_END;
   case AGX_OPCODE_THREADGROUP_BARRIER:
      return AGX3_SEL_OP_BARRIER;
   case AGX_OPCODE_MOV_IMM:
      return AGX3_SEL_OP_MOV_IMM;
   case AGX_OPCODE_FADD:
      return AGX3_SEL_OP_FADD;
   case AGX_OPCODE_FMUL:
      return AGX3_SEL_OP_FMUL;
   case AGX_OPCODE_FFMA:
      return AGX3_SEL_OP_FFMA;
   case AGX_OPCODE_IADD:
      return AGX3_SEL_OP_IADD;
   case AGX_OPCODE_IMAD:
      return AGX3_SEL_OP_IMAD;
   case AGX_OPCODE_ASR:
      return AGX3_SEL_OP_ASR;
   case AGX_OPCODE_BITOP:
      return AGX3_SEL_OP_BITOP;
   default:
      return AGX3_SEL_OP_UNSUPPORTED;
   }
}

/* A source the no-modifier packers can consume is a plain register: not an
 * immediate, uniform, or memory operand. Milestone one refuses the rest, which
 * a pre-RA legalize pass will later materialize. */
static bool
agx3_src_is_register(agx_index s)
{
   return !s.memory &&
          (s.type == AGX_INDEX_REGISTER || s.type == AGX_INDEX_NORMAL);
}

static bool
agx3_translate_instr(const agx_instr *I, struct agx3_sel_instr *S)
{
   memset(S, 0, sizeof(*S));

   S->op = agx3_map_opcode(I->op);
   if (S->op == AGX3_SEL_OP_UNSUPPORTED)
      return false;

   if (I->nr_srcs > 3)
      return false;

   S->saturate = I->saturate;
   S->shift = I->shift;
   S->imm = (uint32_t)I->imm;
   S->truth_table = I->truth_table;

   if (I->nr_dests >= 1 && I->dest[0].type != AGX_INDEX_NULL) {
      S->has_dest = true;
      S->dest = I->dest[0].value;
      S->dest_size = I->dest[0].size;
   }

   S->nr_srcs = I->nr_srcs;
   for (unsigned i = 0; i < I->nr_srcs; ++i) {
      S->src[i].reg = I->src[i].value;
      S->src[i].size = I->src[i].size;
      S->src[i].abs = I->src[i].abs;
      S->src[i].neg = I->src[i].neg;
      S->src[i].is_reg = agx3_src_is_register(I->src[i]);
   }

   return true;
}

static bool
agx3_emit(struct util_dynarray *emission, enum agx_isa_encoding enc,
          const struct agx3_sel_instr *S)
{
   uint8_t bytes[AGX3_SEL_MAX_LENGTH];
   uint8_t len = 0;

   if (!agx3_select_encode(enc, S, bytes, &len))
      return false;

   memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
   return true;
}

bool
agx3_pack_binary(agx_context *ctx, struct util_dynarray *emission)
{
   const enum agx_isa_encoding enc = ctx->key->dev.encoding;

   assert(enc == AGX_ISA_ENCODING_AGX3B || enc == AGX_ISA_ENCODING_AGX3C);

   agx_foreach_block(ctx, block) {
      block->offset = emission->size;

      agx_foreach_instr_in_block(block, I) {
         block->last_offset = emission->size;

         /* stop is the shader terminator; it becomes the single trailing `end`
          * below, matching the corpus (one end per region). */
         if (I->op == AGX_OPCODE_STOP)
            continue;

         struct agx3_sel_instr S;
         if (!agx3_translate_instr(I, &S) || !agx3_emit(emission, enc, &S))
            return false;
      }
   }

   if (!ctx->key->no_stop || ctx->is_preamble) {
      const struct agx3_sel_instr end = {.op = AGX3_SEL_OP_END};
      if (!agx3_emit(emission, enc, &end))
         return false;
   }

   return true;
}
