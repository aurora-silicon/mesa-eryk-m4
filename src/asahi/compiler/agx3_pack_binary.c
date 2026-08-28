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
 * The production subset includes grounded ALU, device-memory, special-register
 * and structured-control-flow forms, plus the graphics path: varying
 * interpolation and UVS stores for vertex shaders, and coefficient loads, tile
 * load/store and the pixel writeout barriers for fragment shaders. It fails the
 * shader -- never aborts, since this runs inside the UMD -- on any unmapped op
 * or out-of-domain instruction. In particular, a compare after an asynchronous
 * producer is refused until the compare/cnd hazard fields are grounded;
 * emitting the witnessed all-clear form there could consume stale data.
 */

#include "agx3_alu_core.h"
#include "agx3_atomic_core.h"
#include "agx3_cf_core.h"
#include "agx3_falu_mod_core.h"
#include "agx3_funop_core.h"
#include "agx3_local_tps_core.h"
#include "agx3_mem_core.h"
#include "agx3_pack.h"
#include "agx3_pixel_core.h"
#include "agx3_scs_core.h"
#include "agx3_select_core.h"
#include "agx3_slot_core.h"
#include "agx3_sr_core.h"
#include "agx3_static_spill_core.h"
#include "agx3_subgroup_core.h"
#include "agx3_tex_core.h"
#include "agx3_tile_core.h"
#include "agx3_vary_core.h"
#include "agx_compiler.h"
#include "agx_debug.h"

#include <stdio.h>
#include <string.h>

struct agx3_branch_fixup {
   agx_block *block;
   off_t offset;
   bool skip_to_end;
   bool is_bn;
};

/*
 * This backend is fail-closed per instruction: a shader containing anything
 * outside the grounded domain is refused, never mis-encoded. A bare refusal is
 * hard to act on, so name the instruction that closed the door under the
 * compiler's existing shader-debug flag (AGX_MESA_DEBUG=shaders). This is the
 * difference between "AGX3 rejected this shader" and a usable bug report.
 */
static void
agx3_report_refusal(const agx_instr *I, unsigned line)
{
   if (agx_compiler_debug & AGX_DBG_SHADERS) {
      fprintf(stderr,
              "agx3_pack_binary.c:%u: no grounded AGX3 encoding for %s\n",
              line, agx_opcodes_info[I->op].name);
   }
}

#define AGX3_REFUSE(I)                                                         \
   do {                                                                        \
      agx3_report_refusal((I), __LINE__);                                      \
      goto fail;                                                               \
   } while (0)

static bool
agx3_append(struct util_dynarray *emission, const uint8_t *bytes, uint8_t len)
{
   memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
   return true;
}

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
   case AGX_OPCODE_BFI:
      return AGX3_SEL_OP_SLL;
   case AGX_OPCODE_BFEIL:
      return AGX3_SEL_OP_LSR;
   case AGX_OPCODE_RCP:
      return AGX3_SEL_OP_F_RCP;
   case AGX_OPCODE_RSQRT:
      return AGX3_SEL_OP_F_RSQRT;
   case AGX_OPCODE_ROUNDEVEN:
      return AGX3_SEL_OP_F_RINT;
   default:
      return AGX3_SEL_OP_UNSUPPORTED;
   }
}

/* A source that is the immediate constant zero -- how Mesa spells the base of a
 * bitfield insert that is really a shift. */
static bool
agx3_src_is_zero_imm(agx_index s)
{
   return s.type == AGX_INDEX_IMMEDIATE && s.value == 0;
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

/* Fill the destination and a single value source into the normalized form,
 * used by the ops the generic path cannot describe (convert). */
static void
agx3_set_dest_and_value(const agx_instr *I, agx_index value,
                        struct agx3_sel_instr *S)
{
   if (I->nr_dests >= 1 && I->dest[0].type != AGX_INDEX_NULL) {
      S->has_dest = true;
      S->dest = I->dest[0].value;
      S->dest_size = I->dest[0].size;
   }
   S->nr_srcs = 1;
   S->src[0].reg = value.value;
   S->src[0].size = value.size;
   S->src[0].is_reg = agx3_src_is_register(value);
}

static bool
agx3_translate_instr(const agx_instr *I, struct agx3_sel_instr *S)
{
   memset(S, 0, sizeof(*S));

   /* convert carries its direction as an immediate mode in the first source
    * and its rounding in `round`; the actual value is the second source. Only
    * the four 32-bit language casts NIR emits (f2i/f2u round-to-zero, i2f/u2f
    * round-to-nearest-even) are grounded. */
   if (I->op == AGX_OPCODE_CONVERT) {
      if (I->nr_srcs != 2 || I->src[0].type != AGX_INDEX_IMMEDIATE)
         return false;

      const unsigned mode = I->src[0].value;
      const enum agx_round round = I->round;

      if (mode == AGX_CONVERT_F_TO_S32 && round == AGX_ROUND_RTZ)
         S->op = AGX3_SEL_OP_F_TO_I32;
      else if (mode == AGX_CONVERT_F_TO_U32 && round == AGX_ROUND_RTZ)
         S->op = AGX3_SEL_OP_F_TO_U32;
      else if (mode == AGX_CONVERT_S32_TO_F && round == AGX_ROUND_RTE)
         S->op = AGX3_SEL_OP_I32_TO_F;
      else if (mode == AGX_CONVERT_U32_TO_F && round == AGX_ROUND_RTE)
         S->op = AGX3_SEL_OP_U32_TO_F;
      else
         return false;

      agx3_set_dest_and_value(I, I->src[1], S);
      return true;
   }

   /* fmin/fmax lower to fcmpsel(a, b, a, b, cond) -- a compare-select whose two
    * selected values are the two compared values, tied. cond LTN is a minimum,
    * GTN a maximum. Any other fcmpsel (a general ?: select) has no grounded
    * AGX3 mapping and is refused. */
   if (I->op == AGX_OPCODE_FCMPSEL) {
      if (I->nr_srcs != 4 || I->invert_cond ||
          !agx3_src_is_register(I->src[0]) ||
          !agx3_src_is_register(I->src[1]) || I->src[0].abs || I->src[0].neg ||
          I->src[1].abs || I->src[1].neg ||
          I->src[0].value != I->src[2].value ||
          I->src[1].value != I->src[3].value)
         return false;

      if (I->fcond == AGX_FCOND_LTN)
         S->op = AGX3_SEL_OP_FMIN;
      else if (I->fcond == AGX_FCOND_GTN)
         S->op = AGX3_SEL_OP_FMAX;
      else
         return false;

      if (I->nr_dests >= 1 && I->dest[0].type != AGX_INDEX_NULL) {
         S->has_dest = true;
         S->dest = I->dest[0].value;
         S->dest_size = I->dest[0].size;
      }
      S->nr_srcs = 2;
      S->src[0].reg = I->src[0].value;
      S->src[0].size = I->src[0].size;
      S->src[0].is_reg = true;
      S->src[1].reg = I->src[1].value;
      S->src[1].size = I->src[1].size;
      S->src[1].is_reg = true;
      return true;
   }

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

   /* NIR ishl/ushr arrive as a bitfield insert/extract with a zero base and a
    * zero insert mask: bfi(zero, value, amount) and bfeil(zero, value,
    * amount). That exact shape is a shift; anything else with these opcodes is
    * a genuine bitfield op with no grounded AGX3 mapping, so refuse it. Rewrite
    * the shift into the core's two-source (value, amount) form. */
   if (S->op == AGX3_SEL_OP_SLL || S->op == AGX3_SEL_OP_LSR) {
      if (I->nr_srcs != 3 || !agx3_src_is_zero_imm(I->src[0]) || I->imm != 0)
         return false;

      S->src[0] = S->src[1]; /* value  */
      S->src[1] = S->src[2]; /* amount */
      S->nr_srcs = 2;
      S->src[2] = (struct agx3_sel_src){0};
   }

   /* NIR isub is iadd(a, -b): an iadd whose second source is negated and whose
    * first is not. Map it to the grounded isub and drop the neg. A negate
    * (iadd(0, -a)) has an immediate-zero first source and is left unmapped. */
   if (S->op == AGX3_SEL_OP_IADD && I->nr_srcs == 2 && I->src[1].neg &&
       !I->src[0].neg && agx3_src_is_register(I->src[0]) &&
       agx3_src_is_register(I->src[1]) && I->shift == 0) {
      S->op = AGX3_SEL_OP_ISUB;
      S->src[1].neg = false;
   }

   /* NIR imul is imad(a, b, 0): a multiply-add with a zero addend. Map it to
    * the grounded imul. */
   if (S->op == AGX3_SEL_OP_IMAD && I->nr_srcs == 3 &&
       agx3_src_is_zero_imm(I->src[2]) && I->shift == 0) {
      S->op = AGX3_SEL_OP_IMUL;
      S->nr_srcs = 2;
      S->src[2] = (struct agx3_sel_src){0};
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

/* A source usable by the modifier long forms: a plain 32-bit register. */
static bool
agx3_mod_src_ok(agx_index s)
{
   return agx3_src_is_register(s) && !s.memory && s.size == AGX_SIZE_32 &&
          (s.value & 1) == 0 && s.value < 256;
}

/*
 * Route a modifier-carrying float add/mul/fma through the long-form modifier
 * encoder. Returns 1 if it emitted the instruction, 0 if this is not a
 * modifier-bearing float op (the caller should use the ordinary path), or -1
 * if it is one but out of the grounded domain (fail the shader).
 */
static int
agx3_try_float_modifier(struct util_dynarray *emission,
                        enum agx_isa_encoding enc, const agx_instr *I)
{
   const bool is_add = I->op == AGX_OPCODE_FADD;
   const bool is_mul = I->op == AGX_OPCODE_FMUL;
   const bool is_fma = I->op == AGX_OPCODE_FFMA;
   if (!is_add && !is_mul && !is_fma)
      return 0;

   bool has_mod = I->saturate;
   for (unsigned s = 0; s < I->nr_srcs; ++s)
      has_mod |= I->src[s].abs || I->src[s].neg;
   if (!has_mod)
      return 0; /* the compact no-modifier packer handles it */

   const unsigned want = is_fma ? 3 : 2;
   if (I->nr_dests < 1 || I->dest[0].type == AGX_INDEX_NULL ||
       I->dest[0].size != AGX_SIZE_32 || (I->dest[0].value & 1) != 0 ||
       I->dest[0].value >= 256 || I->nr_srcs != want)
      return -1;
   for (unsigned s = 0; s < want; ++s)
      if (!agx3_mod_src_ok(I->src[s]))
         return -1;

   struct agx3_falu_mods m = {
      .src0_abs = I->src[0].abs,
      .src0_neg = I->src[0].neg,
      .src1_abs = I->src[1].abs,
      .src1_neg = I->src[1].neg,
      .saturate = I->saturate,
   };

   uint8_t bytes[AGX3_FALU_MOD_MAX_LENGTH];
   uint8_t len = 0;
   bool ok;
   const uint16_t d = I->dest[0].value, s0 = I->src[0].value,
                  s1 = I->src[1].value;
   if (is_add)
      ok = agx3_falu_add_mod(enc, d, s0, s1, &m, bytes, &len);
   else if (is_mul)
      ok = agx3_falu_mul_mod(enc, d, s0, s1, &m, bytes, &len);
   else {
      m.addend_abs = I->src[2].abs;
      m.addend_neg = I->src[2].neg;
      ok = agx3_falu_fma_mod(enc, d, s0, s1, I->src[2].value, &m, bytes, &len);
   }
   if (!ok)
      return -1;

   memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
   return 1;
}

/*
 * The transcendental / rounding / gradient funops with a corpus-grounded
 * AGX3 form. roundeven keeps its existing select-core route; sin_pt_1 and
 * sin_pt_2 are deliberately absent: AGX3 has no counterpart of the sin_pt_1
 * quadrant fixup at all (Apple computes it with an fma/isel polynomial), and
 * although f32sinc's *encoding* is grounded (agx3_funop_core emits it and
 * replays Apple's bytes), whether its input contract equals AGX2 sin_pt_2's
 * is not decidable offline -- Apple folds a phase constant into the preceding
 * polynomial. Routing sin_pt_2 onto f32sinc would be a semantic guess, so
 * fsin_agx stays fail-closed until an executed-numerics witness exists.
 */
static int
agx3_map_funop(unsigned op)
{
   switch (op) {
   case AGX_OPCODE_FLOOR:
      return AGX3_FUNOP_FLOOR;
   case AGX_OPCODE_CEIL:
      return AGX3_FUNOP_CEIL;
   case AGX_OPCODE_TRUNC:
      return AGX3_FUNOP_TRUNC;
   case AGX_OPCODE_LOG2:
      return AGX3_FUNOP_LOG2;
   case AGX_OPCODE_EXP2:
      return AGX3_FUNOP_EXP2;
   case AGX_OPCODE_DFDX:
      return AGX3_FUNOP_DFDX;
   case AGX_OPCODE_DFDY:
      return AGX3_FUNOP_DFDY;
   default:
      return -1;
   }
}

static bool
agx3_emit_funop(struct util_dynarray *emission, enum agx_isa_encoding enc,
                const agx_instr *I, enum agx3_funop op, uint16_t wait_mask)
{
   if (I->nr_dests != 1 || I->nr_srcs != 1 ||
       I->dest[0].type == AGX_INDEX_NULL ||
       !agx3_src_is_register(I->dest[0]) || I->dest[0].memory ||
       I->dest[0].size != AGX_SIZE_32 || !agx3_src_is_register(I->src[0]) ||
       I->src[0].memory || I->src[0].size != AGX_SIZE_32)
      return false;

   uint8_t bytes[AGX3_FUNOP_MAX_LENGTH];
   uint8_t len = 0;
   return agx3_funop_encode(enc, op, I->dest[0].value, I->src[0].value,
                            I->src[0].abs, I->src[0].neg, I->saturate,
                            wait_mask, bytes, &len) &&
          agx3_append(emission, bytes, len);
}

/* An async producer whose result later consumers must wait on. */
static bool
agx3_is_async(const agx_instr *I)
{
   if ((I->op == AGX_OPCODE_ATOMIC || I->op == AGX_OPCODE_LOCAL_ATOMIC) &&
       I->nr_dests == 1 && !agx_is_null(I->dest[0]))
      return true;

   return I->op == AGX_OPCODE_DEVICE_LOAD || I->op == AGX_OPCODE_LOCAL_LOAD ||
          I->op == AGX_OPCODE_LDCF || I->op == AGX_OPCODE_GET_SR ||
          I->op == AGX_OPCODE_TEXTURE_LOAD ||
          I->op == AGX_OPCODE_TEXTURE_SAMPLE || I->op == AGX_OPCODE_LD_TILE ||
          I->op == AGX_OPCODE_SCS_LOAD_AGX3 ||
          I->op == AGX_OPCODE_SCS_LOAD_UNIFORM_INDEXED_AGX3 ||
          I->op == AGX_OPCODE_STACK_LOAD ||
          I->op == AGX_OPCODE_STACK_STORE;
}

/* Map a Mesa special register to the corpus-witnessed Apple SR number and
 * destination width. Only registers tied to a source-level builtin by an
 * isolating probe are mapped; everything else -- including any (SR, width)
 * combination Apple never emitted -- fails closed so the shader is honestly
 * rejected, not miscompiled. */
static bool
agx3_map_sr(enum agx_sr sr, uint8_t *apple, enum agx3_sr_width *width)
{
#define SR(mesa, num, w)                                                       \
   case mesa:                                                                  \
      *apple = (num);                                                          \
      *width = (w);                                                            \
      return true
   switch (sr) {
      SR(AGX_SR_THREADGROUP_POSITION_IN_GRID_X, 26, AGX3_SR_W32);
      SR(AGX_SR_THREADGROUP_POSITION_IN_GRID_Y, 27, AGX3_SR_W32);
      SR(AGX_SR_THREADGROUP_POSITION_IN_GRID_Z, 28, AGX3_SR_W32);
      SR(AGX_SR_THREADS_PER_THREADGROUP_X, 17, AGX3_SR_W16);
      SR(AGX_SR_THREADS_PER_THREADGROUP_Y, 18, AGX3_SR_W16);
      SR(AGX_SR_THREADS_PER_THREADGROUP_Z, 19, AGX3_SR_W16);
      SR(AGX_SR_DISPATCH_THREADS_PER_THREADGROUP_X, 20, AGX3_SR_W16);
      SR(AGX_SR_DISPATCH_THREADS_PER_THREADGROUP_Y, 21, AGX3_SR_W16);
      SR(AGX_SR_DISPATCH_THREADS_PER_THREADGROUP_Z, 22, AGX3_SR_W16);
      SR(AGX_SR_THREAD_POSITION_IN_THREADGROUP_X, 9, AGX3_SR_W16);
      SR(AGX_SR_THREAD_POSITION_IN_THREADGROUP_Y, 10, AGX3_SR_W16);
      SR(AGX_SR_THREAD_POSITION_IN_THREADGROUP_Z, 11, AGX3_SR_W16);
      SR(AGX_SR_THREAD_INDEX_IN_THREADGROUP, 13, AGX3_SR_W16);
      SR(AGX_SR_THREAD_INDEX_IN_SUBGROUP, 7, AGX3_SR_W16);
      SR(AGX_SR_SUBGROUP_INDEX_IN_THREADGROUP, 6, AGX3_SR_W16);
      SR(AGX_SR_THREAD_POSITION_IN_GRID_X, 23, AGX3_SR_W32);
      SR(AGX_SR_THREAD_POSITION_IN_GRID_Y, 24, AGX3_SR_W32);
      SR(AGX_SR_THREAD_POSITION_IN_GRID_Z, 25, AGX3_SR_W32);
   default:
      return false;
   }
#undef SR
}

/* Translate a get_sr into the normalized SR-read descriptor. The destination
 * size must match the witnessed width of the register: Apple reads 16-bit SRs
 * into a half register (consumers or an explicit zero-extend handle the rest),
 * and no witness reads one at another width. */
static bool
agx3_translate_sr(const agx_instr *I, uint8_t slot, uint16_t wait_mask,
                  struct agx3_sr_desc *d)
{
   memset(d, 0, sizeof(*d));

   if (I->nr_dests < 1 || I->dest[0].type == AGX_INDEX_NULL)
      return false;
   const agx_index dst = I->dest[0];
   if (!agx3_src_is_register(dst) || dst.memory || dst.value >= 256)
      return false;

   uint8_t apple;
   enum agx3_sr_width width;
   if (!agx3_map_sr(I->sr, &apple, &width))
      return false;
   if (width == AGX3_SR_W32 ? dst.size != AGX_SIZE_32 : dst.size != AGX_SIZE_16)
      return false;

   d->dest = dst.value;
   d->width = width;
   d->sr = apple;
   d->slot = slot;
   d->wait_mask = wait_mask;
   return true;
}

static bool
agx3_map_vary_location(enum agx_interpolation interpolation,
                        enum agx3_vary_location *location)
{
   switch (interpolation) {
   case AGX_INTERPOLATION_CENTER:
      *location = AGX3_VARY_CENTER;
      return true;
   case AGX_INTERPOLATION_CENTROID:
      *location = AGX3_VARY_CENTROID;
      return true;
   default:
      return false;
   }
}

static bool
agx3_vary_reg32(agx_index index)
{
   return agx3_src_is_register(index) && !index.memory &&
          index.size == AGX_SIZE_32 && (index.value & 1) == 0 &&
          index.value < 256 && !index.abs && !index.neg;
}

static bool
agx3_emit_interp(struct util_dynarray *emission, enum agx_isa_encoding enc,
                 const agx_instr *I, uint16_t wait_mask)
{
   if (wait_mask != 0 || I->nr_dests != 1 || I->nr_srcs != 2 ||
       !agx3_vary_reg32(I->dest[0]) ||
       I->src[0].type != AGX_INDEX_IMMEDIATE ||
       I->src[1].type != AGX_INDEX_IMMEDIATE || I->src[1].value != 0)
      return false;

   enum agx3_vary_location location;
   if (!agx3_map_vary_location(I->interpolation, &location))
      return false;

   uint8_t bytes[AGX3_VARY_MAX_LENGTH];
   uint8_t len = 0;
   return agx3_vary_interp_at(enc, I->dest[0].value, I->src[0].value,
                              I->perspective, location, bytes, &len) &&
          agx3_append(emission, bytes, len);
}

static bool
agx3_emit_ldcf(struct util_dynarray *emission, enum agx_isa_encoding enc,
               const agx_instr *I, uint8_t slot, uint16_t wait_mask)
{
   if (wait_mask != 0 || I->nr_dests != 1 || I->nr_srcs != 1 ||
       !agx3_vary_reg32(I->dest[0]) ||
       I->src[0].type != AGX_INDEX_IMMEDIATE || I->channels != 1)
      return false;

   uint8_t bytes[AGX3_VARY_MAX_LENGTH];
   uint8_t len = 0;
   return agx3_vary_ldcf(enc, I->dest[0].value, I->src[0].value, slot, bytes,
                         &len) &&
          agx3_append(emission, bytes, len);
}

static bool
agx3_emit_uvsw(struct util_dynarray *emission, enum agx_isa_encoding enc,
               const agx_instr *I, uint8_t slot, uint16_t wait_mask)
{
   if (I->nr_dests != 0 || I->nr_srcs != 2 ||
       I->src[0].type != AGX_INDEX_IMMEDIATE ||
       !agx3_vary_reg32(I->src[1]))
      return false;

   uint8_t bytes[AGX3_VARY_MAX_LENGTH];
   uint8_t len = 0;
   return agx3_vary_uvsw(enc, I->src[1].value, I->src[0].value, slot,
                         I->src[1].kill, wait_mask, bytes, &len) &&
          agx3_append(emission, bytes, len);
}

static bool
agx3_tex_plain_reg(agx_index index)
{
   return agx3_src_is_register(index) && !index.memory && !index.abs &&
          !index.neg;
}

static bool
agx3_tex_imm(agx_index index, uint16_t value)
{
   return index.type == AGX_INDEX_IMMEDIATE && index.value == value &&
          !index.memory && !index.abs && !index.neg;
}

/* Translate only the corpus-grounded 2D, non-array, immediate-index texel
 * fetch at LOD0.  Every other texture axis remains a deliberate refusal. */
static bool
agx3_emit_texture_fetch(struct util_dynarray *emission,
                        enum agx_isa_encoding enc, const agx_instr *I,
                        uint8_t slot, uint16_t wait_mask, bool final)
{
   if (I->nr_dests != 1 || I->nr_srcs != 6 ||
       !agx3_tex_plain_reg(I->dest[0]) ||
       (I->dest[0].size != AGX_SIZE_16 &&
        I->dest[0].size != AGX_SIZE_32) ||
       agx_channels(I->dest[0]) != 4 || I->dest[0].value >= 128 ||
       (I->dest[0].size == AGX_SIZE_32 && (I->dest[0].value & 1)) ||
       !agx3_tex_plain_reg(I->src[0]) ||
       I->src[0].size != AGX_SIZE_32 || agx_channels(I->src[0]) != 2 ||
       (I->src[0].value & 3) || I->src[0].value > 60 ||
       !agx3_tex_imm(I->src[1], 0) || !agx3_tex_imm(I->src[2], 0) ||
       I->src[3].type != AGX_INDEX_IMMEDIATE || I->src[3].value > 15 ||
       I->src[3].memory || I->src[3].abs || I->src[3].neg ||
       !agx3_tex_imm(I->src[4], 0) ||
       !agx_is_null(I->src[5]) || I->dim != AGX_DIM_2D ||
       I->lod_mode != AGX_LOD_MODE_AUTO_LOD ||
       (I->mask != 1 && I->mask != 15) || I->offset || I->shadow ||
       I->query_lod || I->gather != AGX_GATHER_NONE || I->sparse)
      return false;

   const struct agx3_tex_fetch_desc d = {
      .dest = I->dest[0].value,
      .coord = I->src[0].value,
      .width = I->dest[0].size == AGX_SIZE_32 ? AGX3_TEX_W32 : AGX3_TEX_W16,
      .mask = I->mask,
      .texture = I->src[3].value,
      .slot = slot,
      .wait_mask = wait_mask,
      .coord_kill = I->src[0].kill,
      .final = final,
   };
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t len = 0;
   return agx3_tex_encode_fetch(enc, &d, bytes, &len) &&
          agx3_append(emission, bytes, len);
}

/*
 * Translate the corpus-grounded filtered 2D sample: implicit LOD (the shape
 * every fragment `texture()` reaches the backend with -- Mesa also folds
 * `textureLod(.., 0)` into it), immediate texture and sampler state, no
 * array/offset/shadow/gather axis.
 *
 * Every witnessed smp reads its coordinates through a scoreboard-signalling
 * producer and waits on that slot: Apple stages ALU-computed coordinates with
 * in-place `copy` instructions carrying the smp's slot (x_coordreg,
 * fs_bufcoord), so this path emits that exact three-instruction group --
 * copy(x), copy(y), smp -- with any producer dependency of the coordinates
 * carried on the first copy, precisely where Apple's own schedule puts it.
 */
static bool
agx3_emit_texture_sample(struct util_dynarray *emission,
                         enum agx_isa_encoding enc, const agx_instr *I,
                         uint8_t slot, uint16_t wait_mask, bool final)
{
   if (I->nr_dests != 1 || I->nr_srcs != 6 ||
       !agx3_tex_plain_reg(I->dest[0]) ||
       (I->dest[0].size != AGX_SIZE_16 &&
        I->dest[0].size != AGX_SIZE_32) ||
       agx_channels(I->dest[0]) != 4 || I->dest[0].value >= 128 ||
       (I->dest[0].size == AGX_SIZE_32 && (I->dest[0].value & 1)) ||
       !agx3_tex_plain_reg(I->src[0]) ||
       I->src[0].size != AGX_SIZE_32 || agx_channels(I->src[0]) != 2 ||
       (I->src[0].value & 3) || I->src[0].value > 60 ||
       !agx3_tex_imm(I->src[1], 0) || !agx3_tex_imm(I->src[2], 0) ||
       I->src[3].type != AGX_INDEX_IMMEDIATE || I->src[3].value > 15 ||
       I->src[3].memory || I->src[3].abs || I->src[3].neg ||
       I->src[4].type != AGX_INDEX_IMMEDIATE || I->src[4].value > 15 ||
       I->src[4].memory || I->src[4].abs || I->src[4].neg ||
       !agx_is_null(I->src[5]) || I->dim != AGX_DIM_2D ||
       I->lod_mode != AGX_LOD_MODE_AUTO_LOD ||
       (I->mask != 1 && I->mask != 3 && I->mask != 8 && I->mask != 12 &&
        I->mask != 15) ||
       I->offset || I->shadow || I->query_lod ||
       I->gather != AGX_GATHER_NONE || I->sparse)
      return false;

   /* The witnessed copy form carries at most one producer dependency. */
   if (wait_mask & (wait_mask - 1))
      return false;

   const uint16_t coord = I->src[0].value;
   uint8_t bytes[AGX3_TEX_MAX_LENGTH];
   uint8_t len = 0;

   if (!agx3_tex_stage_copy(enc, coord, coord, slot, wait_mask, bytes, &len) ||
       !agx3_append(emission, bytes, len))
      return false;
   if (!agx3_tex_stage_copy(enc, coord + 2, coord + 2, slot, 0, bytes, &len) ||
       !agx3_append(emission, bytes, len))
      return false;

   const struct agx3_tex_sample_desc d = {
      .dest = I->dest[0].value,
      .coord = coord,
      .width = I->dest[0].size == AGX_SIZE_32 ? AGX3_TEX_W32 : AGX3_TEX_W16,
      .mask = I->mask,
      .texture = I->src[3].value,
      .sampler = I->src[4].value,
      .lod = AGX3_TEX_LOD_AUTO,
      .slot = slot,
      .wait_mask = (uint16_t)(1u << (slot - 1)),
      .coord_kill = I->src[0].kill,
      .final = final,
   };
   return agx3_tex_encode_sample(enc, &d, bytes, &len) &&
          agx3_append(emission, bytes, len);
}

static int
agx3_tile_format_width(enum agx_format format)
{
   switch (format) {
   case AGX_FORMAT_I8:
   case AGX_FORMAT_U8NORM:
   case AGX_FORMAT_S8NORM:
   case AGX_FORMAT_SRGBA8:
      return AGX3_TILE_W8;
   case AGX_FORMAT_I16:
   case AGX_FORMAT_F16:
   case AGX_FORMAT_U16NORM:
   case AGX_FORMAT_S16NORM:
      return AGX3_TILE_W16;
   case AGX_FORMAT_I32:
   case AGX_FORMAT_RGB10A2:
   case AGX_FORMAT_RG11B10F:
   case AGX_FORMAT_RGB9E5:
      return AGX3_TILE_W32;
   default:
      return -1;
   }
}

static bool
agx3_tile_value_ok(agx_index value, uint8_t mask)
{
   if (!agx3_src_is_register(value) || value.memory || value.abs || value.neg ||
       (value.size != AGX_SIZE_16 && value.size != AGX_SIZE_32) ||
       (value.value & (agx_size_align_16(value.size) - 1)) ||
       agx_channels(value) != util_bitcount(mask))
      return false;

   return value.value + agx_index_size_16(value) <= 256;
}

/*
 * AGX3's single-sample coverage group (f51=1) encodes no sample selector at
 * all: it stores or loads the whole pixel. The only sample source it can
 * faithfully represent is therefore one that names every sample, which
 * agx_nir_lower_tilebuffer spells as the ALL_SAMPLES immediate 0xFF.
 *
 * A narrower immediate, and any register source, mean per-sample traffic. That
 * is the f51=2 group, whose f44 selector is not grounded (its register-versus-
 * selector meaning is an open question in the corpus), so both fail closed
 * rather than silently becoming a whole-pixel access.
 */
#define AGX3_TILE_ALL_SAMPLES 0xFFu

static bool
agx3_tile_pixel_rate_sample(agx_index sample)
{
   return sample.type == AGX_INDEX_IMMEDIATE &&
          sample.value == AGX3_TILE_ALL_SAMPLES && sample.size == AGX_SIZE_16 &&
          !sample.memory && !sample.abs && !sample.neg && !sample.cache &&
          !sample.discard && !sample.kill;
}

/* Bind the directly witnessed single-sample tile subset.  Explicit pixel
 * coordinates and the per-sample selector remain closed because f44's exact
 * register/selector meaning is not yet grounded. */
static bool
agx3_emit_tile(struct util_dynarray *emission, enum agx_isa_encoding enc,
               const agx_instr *I, uint8_t slot, uint16_t wait_mask,
               bool first_load, bool last_load)
{
   const bool load = I->op == AGX_OPCODE_LD_TILE;
   if ((!load && I->op != AGX_OPCODE_ST_TILE) ||
       I->nr_dests != (load ? 1 : 0) || I->nr_srcs != (load ? 2 : 3) ||
       I->mask < 1 || I->mask > 15 || I->pixel_offset > UINT8_MAX ||
       I->explicit_coords)
      return false;

   const agx_index value = load ? I->dest[0] : I->src[0];
   const agx_index sample = load ? I->src[0] : I->src[1];
   const agx_index coords = load ? I->src[1] : I->src[2];
   const int width = agx3_tile_format_width(I->format);
   if (width < 0 || !agx3_tile_value_ok(value, I->mask) ||
       !agx3_tile_pixel_rate_sample(sample) || !agx_is_null(coords))
      return false;

   /* AGX3b f13 varies independently in high-register store witnesses.  The
    * low-register, single-sample subset is the only one with a stable zero
    * selection that production IR can infer today. */
   if (!load && enc == AGX_ISA_ENCODING_AGX3B && value.value >= 16)
      return false;

   uint8_t bytes[AGX3_TILE_MAX_LENGTH];
   uint8_t length = 0;
   const bool ok =
      load ? agx3_tile_load(enc, value.value, (enum agx3_tile_width)width,
                            slot, I->mask, I->pixel_offset, AGX3_TILE_COV_1X,
                            wait_mask, first_load, last_load, bytes, &length)
           : agx3_tile_store(enc, value.value,
                             (enum agx3_tile_width)width, I->mask,
                             I->pixel_offset, AGX3_TILE_COV_1X, wait_mask,
                             bytes, &length);
   return ok && agx3_append(emission, bytes, length);
}

/*
 * Fragment ops that stay refused, and why. Each is a missing *witness*, not
 * missing routing, so grounding one is a corpus session rather than a patch:
 *
 * - sample_mask (Mesa's mask+value write, from discard or an explicit
 *   gl_SampleMask). Apple's discard and [[sample_mask]] shaders both use the
 *   same writeout group as depthf -- a staging `copy` whose destination field
 *   is 0 in every witness, then `movmsk` under the class-1 pixwait/pixrel
 *   bracket -- and never `cmsk` (cmsk appears only in alpha-to-coverage
 *   shaders). movmsk's operand roles (f13/f17) are not isolated and the
 *   staged-register ABI below applies equally, so both spellings stay closed.
 *
 * - zs_emit (depthf). depthf's enable bits are grade A (f25 depth, f26
 *   stencil), but the instruction carries no value register: the values are
 *   staged by a preceding `copy` whose destination field is 0 (r0) in all
 *   nine witnesses (five corpus + four fresh RA-contrast probes, 2026-08-27),
 *   with the stencil byte reaching r0h via a 32-bit copy in the stencil
 *   probes. Two things are still missing before emitting it: the depth value
 *   is produced by an un-decomposed iadd(imm15)/fsel cluster whose
 *   destination-field identity is unproven (so "r0" is a strong correlation,
 *   not a traced ABI), and that cluster may be a required format transform --
 *   Apple never stages the raw float. Emitting depthf against a guessed ABI
 *   would write undefined bits into the depth test.
 *
 * See research/gpu/GPU_AGX3_TILE_OPS_2026-08-24.md sections 4 and 6.
 */

/*
 * Pixel writeout barriers: wait_pix -> pixwait, signal_pix -> pixrel.
 *
 * The IR carries the writeout-class bitmask in `writeout`, which is the same
 * mask Apple's fragment corpus puts in the instruction, so it passes straight
 * through; agx3_pixel_core refuses any class Apple never emitted.
 *
 * signal_pix has no producer in Mesa today. It is routed anyway because it is
 * grounded, but only for the release descriptors witnessed without the
 * end-of-sequence bit: Mesa's IR has no way to say "this is the final release",
 * and inventing that bit is exactly the kind of guess this backend refuses.
 */
static bool
agx3_emit_pixel(struct util_dynarray *emission, enum agx_isa_encoding enc,
                const agx_instr *I, uint16_t wait_mask)
{
   if (I->nr_dests != 0 || I->nr_srcs != 0)
      return false;

   uint8_t bytes[AGX3_PIXEL_MAX_LENGTH];
   uint8_t len = 0;
   const bool ok =
      I->op == AGX_OPCODE_WAIT_PIX
         ? agx3_pixel_wait(enc, I->writeout, wait_mask, bytes, &len)
         : agx3_pixel_release(enc, I->writeout, false, wait_mask, bytes, &len);

   return ok && agx3_append(emission, bytes, len);
}

/* Append the half-registers an operand occupies (empty for immediates,
 * uniforms, or null). */
static unsigned
agx3_expand_regs(agx_index idx, uint16_t *arr, unsigned n)
{
   if (!agx3_src_is_register(idx) || idx.memory)
      return n;
   const unsigned halves = agx_size_align_16(idx.size) * agx_channels(idx);
   for (unsigned i = 0; i < halves && n < AGX3_SLOT_MAX_REGS; ++i)
      arr[n++] = (uint16_t)(idx.value + i);
   return n;
}

static int
agx3_fmt_to_width(enum agx_format f)
{
   switch (f) {
   case AGX_FORMAT_I8:
      return AGX3_MEM_W8;
   case AGX_FORMAT_I16:
   case AGX_FORMAT_F16:
      return AGX3_MEM_W16;
   case AGX_FORMAT_I32:
      return AGX3_MEM_W32;
   default:
      return -1; /* norm/packed formats have no AGX3 device-op witness */
   }
}

static bool
agx3_scs_plain_index(agx_index index)
{
   return !index.kill && !index.cache && !index.discard && !index.abs &&
          !index.neg && !index.memory && !index.has_reg &&
          agx_channels(index) == 1;
}

static bool
agx3_translate_scs(const agx_instr *I, bool is_store, bool indexed,
                   uint8_t slot, uint16_t wait_mask,
                   struct agx3_scs_desc *d)
{
   memset(d, 0, sizeof(*d));
   const int width = agx3_fmt_to_width(I->format);
   const int64_t byte_offset = (int64_t)I->imm;
   const unsigned expected_srcs = indexed ? (is_store ? 3 : 2)
                                          : (is_store ? 1 : 0);
   if (width < 0 || byte_offset < INT16_MIN || byte_offset > INT16_MAX ||
       I->nr_dests != (is_store ? 0 : 1) ||
       I->nr_srcs != expected_srcs ||
       ((is_store || !indexed) &&
        (I->scs_zero_extend || I->scs_c_load_marker)))
      return false;

   const agx_index data = is_store ? I->src[0] : I->dest[0];
   const enum agx_size expected =
      width == AGX3_MEM_W32 ? AGX_SIZE_32 : AGX_SIZE_16;
   if (!agx3_src_is_register(data) || !agx3_scs_plain_index(data) ||
       data.size != expected || agx_channels(data) != 1 ||
       (data.value & (agx_size_align_16(expected) - 1)) ||
       data.value + agx_size_align_16(expected) > 256)
      return false;

   *d = (struct agx3_scs_desc){
      .data = data.value,
      .width = (enum agx3_mem_width)width,
      .byte_offset = (int16_t)byte_offset,
      .address_kind = indexed ? AGX3_SCS_ADDRESS_UNIFORM_INDEXED
                              : AGX3_SCS_ADDRESS_DIRECT,
      .slot = is_store ? 0 : slot,
      .wait_mask = wait_mask,
   };

   if (indexed) {
      const agx_index base = I->src[is_store ? 1 : 0];
      const agx_index index = I->src[is_store ? 2 : 1];

      /* The pseudo itself preserves the address-space provenance.  Require
       * the physical operands to preserve it too: a scalar 64-bit uniform
       * base and a scalar aligned 32-bit GPR index.  In particular, no
       * register base, vector, source modifier, or inferred last-use bit is
       * silently encoded into fields whose semantics are not yet grounded.
       */
      if (base.type != AGX_INDEX_UNIFORM || base.size != AGX_SIZE_64 ||
          !agx3_scs_plain_index(base) || base.value >= 512 ||
          !agx3_src_is_register(index) || index.size != AGX_SIZE_32 ||
          !agx3_scs_plain_index(index) || (index.value & 1) ||
          index.value + agx_size_align_16(index.size) > 256)
         return false;

      d->uniform_base = base.value;
      d->index = index.value;
      d->zero_extend = !is_store && I->scs_zero_extend;
      d->c_load_marker = !is_store && I->scs_c_load_marker;
   }

   return true;
}

/* Translate a device_load/device_store into the normalized memory descriptor.
 * Returns false for any operand shape outside the grounded domain. */
static bool
agx3_translate_mem(const agx_instr *I, bool is_store, uint8_t slot,
                   uint16_t wait_mask, struct agx3_mem_desc *d)
{
   memset(d, 0, sizeof(*d));

   const int w = agx3_fmt_to_width(I->format);
   if (w < 0 || I->mask < 1 || I->mask > 15 || I->shift > 4)
      return false;
   d->width = (enum agx3_mem_width)w;
   d->mask = (uint8_t)I->mask;
   d->shift = (uint8_t)I->shift;

   const agx_index data = is_store ? I->src[0] : I->dest[0];
   if (!agx3_src_is_register(data) || data.memory || (data.value & 1) != 0 ||
       data.value >= 256)
      return false;
   d->data = data.value;

   const agx_index base = I->src[is_store ? 1 : 0];
   if (base.type == AGX_INDEX_UNIFORM) {
      d->base_is_uniform = true;
      d->base = base.value;
   } else if (agx3_src_is_register(base) && !base.memory) {
      d->base_is_uniform = false;
      d->base = base.value;
      d->base_kill = base.kill;
   } else {
      return false;
   }

   const agx_index index = I->src[is_store ? 2 : 1];
   if (index.type == AGX_INDEX_IMMEDIATE) {
      d->idx_kind = AGX3_MEM_IDX_IMM;
      const int64_t bytes = (int64_t)index.value << I->shift;
      if (bytes >= -32768 && bytes <= 32767) {
         d->idx_scaled = false;
         d->byte_off = (int32_t)bytes;
         d->shift = 0;
      } else if (index.value <= 0xffff) {
         d->idx_scaled = true;
         d->elem_idx = index.value;
      } else {
         return false; /* needs address-math legalization */
      }
   } else if (agx3_src_is_register(index) && !index.memory) {
      d->idx_kind = AGX3_MEM_IDX_REG;
      if ((index.value & 1) != 0 || index.value > 191)
         return false;
      d->idx_reg = index.value;
      d->idx_kill = index.kill;
      d->idx_signed = false;
   } else {
      return false;
   }

   d->slot = slot;
   d->wait_mask = wait_mask;
   return true;
}

static uint8_t
agx3_local_vector_shift(enum agx3_mem_width width, uint8_t mask)
{
   const unsigned channels = util_bitcount(mask);
   unsigned bytes = channels << width;
   uint8_t shift = 0;
   while ((1u << shift) < bytes)
      shift++;
   return shift;
}

/* Translate the shared-memory shape Mesa currently emits. Address arithmetic
 * is folded into the 16-bit base by agx_compile, leaving an immediate-zero
 * index. This is exactly the grounded Apple ldtg/sttg subset; a register index
 * is refused because Mesa's byte-addressed IR has not yet been legalized to
 * AGX3's scaled-index convention. */
static bool
agx3_translate_local(const agx_instr *I, bool is_store, uint8_t slot,
                     uint16_t wait_mask, struct agx3_local_desc *d)
{
   memset(d, 0, sizeof(*d));

   const int w = agx3_fmt_to_width(I->format);
   if (w < 0 || I->mask < 1 || I->mask > 15 ||
       I->mask != BITFIELD_MASK(util_bitcount(I->mask)))
      return false;
   d->width = (enum agx3_mem_width)w;
   d->mask = (uint8_t)I->mask;
   d->shift = agx3_local_vector_shift(d->width, d->mask);

   const agx_index data = is_store ? I->src[0] : I->dest[0];
   const enum agx_size expected =
      d->width == AGX3_MEM_W32 ? AGX_SIZE_32 : AGX_SIZE_16;
   const unsigned halves = agx_size_align_16(expected) * util_bitcount(I->mask);
   if (!agx3_src_is_register(data) || data.memory || data.size != expected ||
       (data.value & (agx_size_align_16(expected) - 1)) ||
       data.value + halves > 256)
      return false;
   d->data = data.value;
   d->value_kill = is_store && data.kill;

   const agx_index base = I->src[is_store ? 1 : 0];
   if (base.size != AGX_SIZE_16 || base.memory || base.abs || base.neg)
      return false;
   if (base.type == AGX_INDEX_IMMEDIATE && base.value == 0) {
      d->base_kind = AGX3_LOCAL_BASE_IMMEDIATE;
   } else if (base.type == AGX_INDEX_UNIFORM && base.value < 512) {
      d->base_kind = AGX3_LOCAL_BASE_UNIFORM;
      d->base = base.value;
   } else if (agx3_src_is_register(base) && base.value < 256) {
      d->base_kind = AGX3_LOCAL_BASE_REGISTER;
      d->base = base.value;
      d->base_kill = base.kill;
   } else {
      return false;
   }

   const agx_index index = I->src[is_store ? 2 : 1];
   if (index.type != AGX_INDEX_IMMEDIATE || index.size != AGX_SIZE_16 ||
       index.value != 0 || index.memory || index.abs || index.neg)
      return false;

   d->narrow_extend = d->width == AGX3_MEM_W8;
   d->slot = slot;
   d->wait_mask = wait_mask;
   return true;
}

static bool
agx3_atomic_plain_reg(agx_index index, enum agx_size size, unsigned channels)
{
   return agx3_src_is_register(index) && !index.memory && !index.abs &&
          !index.neg && !index.kill && !(index.cache && index.discard) &&
          index.size == size &&
          agx_channels(index) == channels &&
          (index.value & (agx_size_align_16(size) - 1)) == 0 &&
          index.value + agx_size_align_16(size) * channels <= 256;
}

static bool
agx3_map_atomic_op(enum agx_atomic_opc in, enum agx3_atomic_op *out)
{
   switch (in) {
#define OP(x)                                                                  \
   case AGX_ATOMIC_OPC_##x:                                                    \
      *out = AGX3_ATOMIC_##x;                                                  \
      return true
      OP(ADD);
      OP(SUB);
      OP(XCHG);
      OP(CMPXCHG);
      OP(UMIN);
      OP(IMIN);
      OP(UMAX);
      OP(IMAX);
      OP(AND);
      OP(OR);
      OP(XOR);
#undef OP
   }

   return false;
}

/* Translate Mesa's global atomic IR into the fully witnessed 32-bit AGX3
 * family.  The address lowering represents a constant byte displacement as a
 * 32-bit element index; fold it back into the signed byte-offset field when it
 * fits, matching Apple.  AGX3b returned atomics are physically in-place, so
 * only an RA-coalesced value/result pair is accepted on that encoding. */
static bool
agx3_translate_device_atomic(const agx_instr *I,
                             enum agx_isa_encoding enc, uint8_t slot,
                             uint16_t wait_mask,
                             struct agx3_device_atomic_desc *d)
{
   memset(d, 0, sizeof(*d));
   if (I->op != AGX_OPCODE_ATOMIC || I->nr_dests != 1 || I->nr_srcs != 3 ||
       !agx3_map_atomic_op(I->atomic_opc, &d->op))
      return false;

   const unsigned value_channels =
      d->op == AGX3_ATOMIC_CMPXCHG ? 2 : 1;
   const agx_index value = I->src[0];
   if (!agx3_atomic_plain_reg(value, AGX_SIZE_32, value_channels))
      return false;
   d->value = value.value;
   d->value_kill = value.discard;

   const agx_index result = I->dest[0];
   d->returns_value = !agx_is_null(result);
   if (d->returns_value) {
      if (!agx3_atomic_plain_reg(result, AGX_SIZE_32, 1) ||
          (enc == AGX_ISA_ENCODING_AGX3B && result.value != value.value))
         return false;
      d->result = result.value;
      d->slot = slot;
   } else if (slot != 0) {
      return false;
   }

   const agx_index base = I->src[1];
   if (base.memory || base.abs || base.neg || base.kill ||
       (base.cache && base.discard) || base.size != AGX_SIZE_64 ||
       agx_channels(base) != 1) {
      return false;
   } else if (base.type == AGX_INDEX_UNIFORM) {
      d->base_kind = AGX3_ATOMIC_BASE_UNIFORM;
      d->base = base.value;
   } else if (agx3_src_is_register(base)) {
      d->base_kind = AGX3_ATOMIC_BASE_REGISTER;
      d->base = base.value;
      /* atomicdev has no base cache/discard field. As on AGX2, silently
       * dropping these allocator hints is semantics-preserving. */
   } else {
      return false;
   }

   const agx_index index = I->src[2];
   if (index.memory || index.abs || index.neg || index.kill ||
       (index.cache && index.discard) || agx_channels(index) != 1)
      return false;

   if (index.type == AGX_INDEX_IMMEDIATE) {
      if (index.kill || index.cache || index.discard ||
          index.value > UINT16_MAX)
         return false;
      const uint64_t byte_offset = (uint64_t)index.value << 2;
      if (byte_offset <= INT16_MAX) {
         d->index_kind = AGX3_ATOMIC_INDEX_IMMEDIATE;
         d->byte_offset = byte_offset;
      } else {
         d->index_kind = AGX3_ATOMIC_INDEX_IMMEDIATE;
         d->index = index.value;
         d->index_shift = 2;
      }
   } else if (agx3_src_is_register(index) && index.size == AGX_SIZE_32 &&
              !index.kill && !(index.cache && index.discard) &&
              (index.value & 1) == 0) {
      d->index_kind = AGX3_ATOMIC_INDEX_REGISTER;
      d->index = index.value;
      d->index_kill = index.discard;
      d->index_shift = 2;
   } else if (index.type == AGX_INDEX_UNIFORM && index.size == AGX_SIZE_32 &&
              index.value < 512 && !index.kill && !index.cache &&
              !index.discard) {
      d->index_kind = AGX3_ATOMIC_INDEX_UNIFORM;
      d->index = index.value;
      d->index_shift = 2;
   } else {
      return false;
   }

   d->wait_mask = wait_mask;
   return true;
}

/* Only Apple's directly witnessed scalar shared-variable form is selected:
 * returning 32-bit atomics at local address zero.  Dynamic/shared-array
 * addressing remains closed until its operand field is isolated. */
static bool
agx3_translate_local_atomic(const agx_instr *I, uint8_t slot,
                            uint16_t wait_mask,
                            struct agx3_local_atomic_desc *d)
{
   memset(d, 0, sizeof(*d));
   if (I->op != AGX_OPCODE_LOCAL_ATOMIC || I->nr_dests != 1 ||
       I->nr_srcs != 3 || agx_is_null(I->dest[0]) ||
       !agx3_map_atomic_op(I->atomic_opc, &d->op))
      return false;

   const unsigned value_channels =
      d->op == AGX3_ATOMIC_CMPXCHG ? 2 : 1;
   if (!agx3_atomic_plain_reg(I->src[0], AGX_SIZE_32, value_channels) ||
       !agx3_atomic_plain_reg(I->dest[0], AGX_SIZE_32, 1))
      return false;

   const agx_index base = I->src[1], index = I->src[2];
   if (base.type != AGX_INDEX_IMMEDIATE || base.value != 0 ||
       base.size != AGX_SIZE_16 || base.memory || base.abs || base.neg ||
       base.cache || base.discard || base.kill ||
       index.type != AGX_INDEX_IMMEDIATE || index.value != 0 ||
       index.size != AGX_SIZE_16 || index.memory || index.abs || index.neg ||
       index.cache || index.discard || index.kill)
      return false;

   d->value = I->src[0].value;
   d->result = I->dest[0].value;
   d->value_kill = I->src[0].discard;
   d->slot = slot;
   d->wait_mask = wait_mask;
   return true;
}

/* Mesa's scalar 32-bit stack operation is an exact match for Apple's static
 * copy/rld family: the immediate byte offset names a TPS half-register and the
 * single data operand names one aligned GPR pair.  Vector masks, 16-bit values,
 * dynamic offsets and modifiers stay on the existing fail-closed path because
 * they do not uniquely determine the witnessed structural fields/forms. */
static bool
agx3_translate_static_spill(const agx_instr *I, bool is_store, uint8_t slot,
                            uint16_t wait_mask,
                            struct agx3_static_spill_store_desc *store,
                            struct agx3_static_spill_load_desc *load)
{
   if (I->op != (is_store ? AGX_OPCODE_STACK_STORE : AGX_OPCODE_STACK_LOAD) ||
       I->nr_dests != (is_store ? 0 : 1) ||
       I->nr_srcs != (is_store ? 2 : 1) || I->format != AGX_FORMAT_I32 ||
       I->mask != 1)
      return false;

   const agx_index data = is_store ? I->src[0] : I->dest[0];
   const agx_index offset = I->src[is_store ? 1 : 0];
   if (!agx3_src_is_register(data) || data.memory || data.abs || data.neg ||
       data.cache || data.discard || data.size != AGX_SIZE_32 ||
       agx_channels(data) != 1 || (data.value & 1) || data.value > 254 ||
       (!is_store && data.kill) || offset.type != AGX_INDEX_IMMEDIATE ||
       offset.memory || offset.abs || offset.neg || offset.cache ||
       offset.discard || offset.kill || offset.size != AGX_SIZE_16 ||
       agx_channels(offset) != 1 || (offset.value & 3))
      return false;

   const uint32_t tps_half = offset.value >> 1;
   if (tps_half + 2 > AGX3_SLOT_TPS_HALF_TOKENS)
      return false;

   if (is_store) {
      if (store == NULL)
         return false;
      *store = (struct agx3_static_spill_store_desc){
         .tps_half = tps_half,
         .src = data.value,
         .width = AGX3_MEM_W32,
         .src_kill = data.kill,
         .slot = slot,
         .wait_mask = wait_mask,
      };
   } else {
      if (load == NULL)
         return false;
      *load = (struct agx3_static_spill_load_desc){
         .dest = data.value,
         .tps_half = tps_half,
         .width = AGX3_MEM_W32,
         .slot = slot,
         .slot_mirror = slot,
         .wait_mask = wait_mask,
      };
   }

   return true;
}

static bool
agx3_append_static_spill_tokens(const agx_instr *I,
                                struct agx3_slot_instr *slot_instr)
{
   const bool is_store = I->op == AGX_OPCODE_STACK_STORE;
   struct agx3_static_spill_store_desc store;
   struct agx3_static_spill_load_desc load;
   if (!agx3_translate_static_spill(I, is_store, 1, 0, &store, &load))
      return false;

   const uint32_t tps_half = is_store ? store.tps_half : load.tps_half;
   for (unsigned i = 0; i < 2; ++i) {
      if (slot_instr->nr_srcs >= AGX3_SLOT_MAX_REGS ||
          slot_instr->nr_dsts >= AGX3_SLOT_MAX_REGS)
         return false;
      const uint16_t token =
         (uint16_t)(AGX3_SLOT_TPS_TOKEN_BASE + tps_half + i);
      slot_instr->src[slot_instr->nr_srcs++] = token;
      slot_instr->dst[slot_instr->nr_dsts++] = token;
   }
   return true;
}

/* Map a resolved selection op to the composer ALU op that can carry a wait.
 * Returns AGX3_ALU_NUM for ops with no wait-carrying form. */
static enum agx3_alu_op
agx3_sel_to_alu(const struct agx3_sel_instr *S)
{
   switch (S->op) {
   case AGX3_SEL_OP_MOV_IMM:
      return AGX3_ALU_LDIMM;
   case AGX3_SEL_OP_IADD:
      return AGX3_ALU_IADD;
   case AGX3_SEL_OP_ISUB:
      return AGX3_ALU_ISUB;
   case AGX3_SEL_OP_IMAD:
      return AGX3_ALU_IMADD;
   case AGX3_SEL_OP_IMUL:
      return AGX3_ALU_IMUL;
   case AGX3_SEL_OP_FADD:
      return AGX3_ALU_FADD;
   case AGX3_SEL_OP_FMUL:
      return AGX3_ALU_FMUL;
   case AGX3_SEL_OP_FFMA:
      return AGX3_ALU_FMA;
   case AGX3_SEL_OP_ASR:
      return AGX3_ALU_ASR;
   case AGX3_SEL_OP_SLL:
      return AGX3_ALU_SLL;
   case AGX3_SEL_OP_LSR:
      return AGX3_ALU_LSR;
   case AGX3_SEL_OP_F_RCP:
      return AGX3_ALU_RCP;
   case AGX3_SEL_OP_F_RSQRT:
      return AGX3_ALU_RSQRT;
   case AGX3_SEL_OP_F_RINT:
      return AGX3_ALU_RINT;
   case AGX3_SEL_OP_I32_TO_F:
      return AGX3_ALU_I2F;
   case AGX3_SEL_OP_U32_TO_F:
      return AGX3_ALU_U2F;
   case AGX3_SEL_OP_F_TO_I32:
      return AGX3_ALU_F2I;
   case AGX3_SEL_OP_BITOP:
      switch (S->truth_table & 0xfu) {
      case AGX3_SEL_TT_AND:
         return AGX3_ALU_BAND;
      case AGX3_SEL_TT_OR:
         return AGX3_ALU_BOR;
      case AGX3_SEL_TT_XOR:
         return AGX3_ALU_BXOR;
      case AGX3_SEL_TT_NOT:
         return AGX3_ALU_BNOT;
      default:
         return AGX3_ALU_NUM;
      }
   default:
      return AGX3_ALU_NUM; /* F_TO_U32, FMIN/FMAX: no wait-carrying form yet */
   }
}

/* Emit one selected ALU instruction, carrying a wait mask when needed. With no
 * wait it uses the compact hand packer; with a wait it uses the composer form,
 * which is byte-identical for wait 0. */
static bool
agx3_emit_alu(struct util_dynarray *emission, enum agx_isa_encoding enc,
              const struct agx3_sel_instr *S, uint16_t wait_mask)
{
   if (wait_mask == 0)
      return agx3_emit(emission, enc, S);

   const enum agx3_alu_op op = agx3_sel_to_alu(S);
   if (op == AGX3_ALU_NUM)
      return false; /* consumer of a load with no wait-carrying encoding */

   uint8_t bytes[AGX3_ALU_MAX_LENGTH];
   uint8_t len = 0;
   const uint16_t s0 = S->nr_srcs > 0 ? S->src[0].reg : 0;
   const uint16_t s1 = S->nr_srcs > 1 ? S->src[1].reg : 0;
   const uint16_t s2 = S->nr_srcs > 2 ? S->src[2].reg : 0;

   if (agx3_alu_compose(enc, op, S->dest, s0, s1, s2, S->imm, wait_mask,
                        bytes, &len)) {
      return agx3_append(emission, bytes, len);
   }

   /* Some compact ALU forms cannot carry the needed scoreboard wait. Retire
    * it with a synchronous prelude, then emit the ordinary no-wait form.
    * A fresh destination can take an ldimm; an in-place accumulator instead
    * uses bit_and(x, x), which preserves every bit of its live source.
    */
   if (!S->has_dest || S->dest_size != AGX_SIZE_32)
      return false;

   bool overlaps_source = false;
   bool exact_in_place = false;
   for (unsigned i = 0; i < S->nr_srcs; ++i) {
      if (!S->src[i].is_reg)
         continue;
      const unsigned dest_halves = agx_size_align_16(S->dest_size);
      const unsigned halves = agx_size_align_16(S->src[i].size);
      if (S->dest < S->src[i].reg + halves &&
          S->src[i].reg < S->dest + dest_halves) {
         overlaps_source = true;
         exact_in_place |=
            S->src[i].reg == S->dest && S->src[i].size == AGX_SIZE_32;
      }
   }

   if (overlaps_source &&
       (!exact_in_place || (wait_mask & (wait_mask - 1)) != 0))
      return false;

   while (wait_mask != 0) {
      const uint16_t prewait = wait_mask & -wait_mask;
      uint8_t prelude[AGX3_ALU_MAX_LENGTH];
      uint8_t prelude_len = 0;
      const enum agx3_alu_op prelude_op =
         exact_in_place ? AGX3_ALU_BAND : AGX3_ALU_LDIMM;
      if (!agx3_alu_compose(enc, prelude_op, S->dest, S->dest, S->dest, 0, 0,
                            prewait, prelude, &prelude_len) ||
          !agx3_append(emission, prelude, prelude_len))
         return false;
      wait_mask &= ~prewait;
   }

   if (!agx3_alu_compose(enc, op, S->dest, s0, s1, s2, S->imm, 0, bytes,
                         &len))
      return false;

   return agx3_append(emission, bytes, len);
}

static bool
agx3_subgroup_plain(agx_index index, enum agx_index_type type)
{
   return index.type == type && !index.memory && index.size == AGX_SIZE_32 &&
          agx_channels(index) == 1 && !index.abs && !index.neg && !index.cache &&
          !index.discard && !index.kill && (index.value & 1) == 0;
}

/* Preserve the older bounded immediate broadcast witnesses. */
static bool
agx3_emit_simd_broadcast(struct util_dynarray *emission,
                         enum agx_isa_encoding enc, const agx_instr *I,
                         uint16_t wait_mask)
{
   if (wait_mask != 0 || I->op != AGX_OPCODE_SHUFFLE || I->nr_dests != 1 ||
       I->nr_srcs != 2 || !agx3_mod_src_ok(I->dest[0]) ||
       !agx3_mod_src_ok(I->src[0]) || I->src[0].abs || I->src[0].neg ||
       agx_channels(I->dest[0]) != 1 || agx_channels(I->src[0]) != 1 ||
       I->src[1].type != AGX_INDEX_IMMEDIATE || I->src[1].memory ||
       I->src[1].abs || I->src[1].neg ||
       agx_channels(I->src[1]) != 1 || I->saturate ||
       (I->src[1].value != 0 && I->src[1].value != 3))
      return false;

   struct agx3_encoded_simd_broadcast_f32_imm encoded;
   return agx3_pack_simd_broadcast_f32_imm(
             enc, I->dest[0].value, I->src[0].value, I->src[1].value,
             &encoded) &&
          agx3_append(emission, encoded.bytes, encoded.length);
}

static bool
agx3_map_shuffle(unsigned op, bool *quad,
                 enum agx3_subgroup_shuffle_op *shuffle)
{
   *quad = false;
   switch (op) {
   case AGX_OPCODE_SHUFFLE:
      *shuffle = AGX3_SUBGROUP_SHUFFLE_BROADCAST;
      return true;
   case AGX_OPCODE_SHUFFLE_XOR:
      *shuffle = AGX3_SUBGROUP_SHUFFLE_XOR;
      return true;
   case AGX_OPCODE_SHUFFLE_UP:
      *shuffle = AGX3_SUBGROUP_SHUFFLE_UP;
      return true;
   case AGX_OPCODE_SHUFFLE_DOWN:
      *shuffle = AGX3_SUBGROUP_SHUFFLE_DOWN;
      return true;
   case AGX_OPCODE_QUAD_SHUFFLE:
      *quad = true;
      *shuffle = AGX3_SUBGROUP_SHUFFLE_BROADCAST;
      return true;
   case AGX_OPCODE_QUAD_SHUFFLE_XOR:
      *quad = true;
      *shuffle = AGX3_SUBGROUP_SHUFFLE_XOR;
      return true;
   case AGX_OPCODE_QUAD_SHUFFLE_UP:
      *quad = true;
      *shuffle = AGX3_SUBGROUP_SHUFFLE_UP;
      return true;
   case AGX_OPCODE_QUAD_SHUFFLE_DOWN:
      *quad = true;
      *shuffle = AGX3_SUBGROUP_SHUFFLE_DOWN;
      return true;
   default:
      return false;
   }
}

static bool
agx3_emit_subgroup_shuffle(struct util_dynarray *emission,
                           enum agx_isa_encoding enc, const agx_instr *I,
                           uint16_t wait_mask)
{
   bool quad;
   enum agx3_subgroup_shuffle_op op;
   if (!agx3_map_shuffle(I->op, &quad, &op))
      return false;

   if (I->op == AGX_OPCODE_SHUFFLE && I->nr_srcs == 2 &&
       I->src[1].type == AGX_INDEX_IMMEDIATE)
      return agx3_emit_simd_broadcast(emission, enc, I, wait_mask);

   if (I->nr_dests != 1 || I->nr_srcs != 2 || I->saturate ||
       !agx3_subgroup_plain(I->dest[0], AGX_INDEX_REGISTER) ||
       !agx3_subgroup_plain(I->src[0], AGX_INDEX_REGISTER))
      return false;

   enum agx3_subgroup_lane_kind lane_kind;
   if (agx3_subgroup_plain(I->src[1], AGX_INDEX_REGISTER))
      lane_kind = AGX3_SUBGROUP_LANE_REGISTER;
   else if (agx3_subgroup_plain(I->src[1], AGX_INDEX_UNIFORM))
      lane_kind = AGX3_SUBGROUP_LANE_UNIFORM;
   else
      return false;

   const struct agx3_subgroup_shuffle_desc d = {
      .quad = quad,
      .op = op,
      .lane_kind = lane_kind,
      .dest = I->dest[0].value,
      .src = I->src[0].value,
      .lane = I->src[1].value,
      .wait_mask = wait_mask,
   };
   uint8_t bytes[AGX3_SUBGROUP_MAX_LENGTH], length = 0;
   return agx3_subgroup_encode_shuffle(enc, &d, bytes, &length) &&
          agx3_append(emission, bytes, length);
}

static bool
agx3_map_reduce_op(enum agx_simd_op in, enum agx3_subgroup_reduce_op *out)
{
   switch (in) {
   case AGX_SIMD_OP_AND:  *out = AGX3_SUBGROUP_REDUCE_AND; return true;
   case AGX_SIMD_OP_FADD: *out = AGX3_SUBGROUP_REDUCE_FADD; return true;
   case AGX_SIMD_OP_OR:   *out = AGX3_SUBGROUP_REDUCE_OR; return true;
   case AGX_SIMD_OP_FMUL: *out = AGX3_SUBGROUP_REDUCE_FMUL; return true;
   case AGX_SIMD_OP_XOR:  *out = AGX3_SUBGROUP_REDUCE_XOR; return true;
   case AGX_SIMD_OP_FMIN: *out = AGX3_SUBGROUP_REDUCE_FMIN; return true;
   case AGX_SIMD_OP_FMAX: *out = AGX3_SUBGROUP_REDUCE_FMAX; return true;
   case AGX_SIMD_OP_IADD: *out = AGX3_SUBGROUP_REDUCE_IADD; return true;
   case AGX_SIMD_OP_SMIN: *out = AGX3_SUBGROUP_REDUCE_SMIN; return true;
   case AGX_SIMD_OP_SMAX: *out = AGX3_SUBGROUP_REDUCE_SMAX; return true;
   case AGX_SIMD_OP_UMIN: *out = AGX3_SUBGROUP_REDUCE_UMIN; return true;
   case AGX_SIMD_OP_UMAX: *out = AGX3_SUBGROUP_REDUCE_UMAX; return true;
   default: return false;
   }
}

static bool
agx3_emit_subgroup_reduce(struct util_dynarray *emission,
                          enum agx_isa_encoding enc, const agx_instr *I,
                          uint16_t wait_mask)
{
   const bool quad = I->op == AGX_OPCODE_QUAD_REDUCE ||
                     I->op == AGX_OPCODE_QUAD_PREFIX;
   const bool prefix = I->op == AGX_OPCODE_SIMD_PREFIX ||
                       I->op == AGX_OPCODE_QUAD_PREFIX;
   if (I->nr_dests != 1 || I->nr_srcs != 1 || I->saturate ||
       !agx3_subgroup_plain(I->dest[0], AGX_INDEX_REGISTER) ||
       !agx3_subgroup_plain(I->src[0], AGX_INDEX_REGISTER))
      return false;

   enum agx3_subgroup_reduce_op op;
   if (!agx3_map_reduce_op(I->simd_op, &op))
      return false;
   const struct agx3_subgroup_reduce_desc d = {
      .quad = quad,
      .prefix = prefix,
      .op = op,
      .dest = I->dest[0].value,
      .src = I->src[0].value,
      .wait_mask = wait_mask,
   };
   uint8_t bytes[AGX3_SUBGROUP_MAX_LENGTH], length = 0;
   return agx3_subgroup_encode_reduce(enc, &d, bytes, &length) &&
          agx3_append(emission, bytes, length);
}

static bool
agx3_emit_subgroup_ballot(struct util_dynarray *emission,
                          enum agx_isa_encoding enc, const agx_instr *I,
                          uint16_t wait_mask)
{
   const bool is_float = I->op == AGX_OPCODE_FCMP_BALLOT ||
                         I->op == AGX_OPCODE_FCMP_QUAD_BALLOT;
   const bool quad = I->op == AGX_OPCODE_ICMP_QUAD_BALLOT ||
                     I->op == AGX_OPCODE_FCMP_QUAD_BALLOT;
   if (I->nr_dests != 1 || I->nr_srcs != 2 || I->saturate ||
       !agx3_subgroup_plain(I->dest[0], AGX_INDEX_REGISTER) ||
       !agx3_subgroup_plain(I->src[0], AGX_INDEX_REGISTER) ||
       I->src[1].type != AGX_INDEX_IMMEDIATE || I->src[1].value != 0 ||
       I->src[1].memory || I->src[1].abs || I->src[1].neg ||
       I->src[1].cache || I->src[1].discard || I->src[1].kill)
      return false;

   uint8_t condition;
   if (is_float) {
      condition = I->fcond;
   } else {
      switch (I->icond) {
      case AGX_ICOND_UEQ:
      case AGX_ICOND_SEQ: condition = 4; break;
      case AGX_ICOND_SLT: condition = 5; break;
      case AGX_ICOND_SGT: condition = 6; break;
      default: return false;
      }
   }

   const struct agx3_subgroup_ballot_desc d = {
      .quad = quad,
      .type = is_float ? AGX3_SUBGROUP_BALLOT_FLOAT
                       : AGX3_SUBGROUP_BALLOT_INTEGER,
      .dest = I->dest[0].value,
      .src = I->src[0].value,
      .condition = condition,
      .invert = I->invert_cond,
      .wait_mask = wait_mask,
   };
   uint8_t bytes[AGX3_SUBGROUP_MAX_LENGTH], length = 0;
   return agx3_subgroup_encode_ballot(enc, &d, bytes, &length) &&
          agx3_append(emission, bytes, length);
}

static bool
agx3_emit_scoped_barrier(struct util_dynarray *emission,
                         enum agx_isa_encoding enc, const agx_instr *I,
                         uint16_t wait_mask)
{
   if (wait_mask != 0 || I->op != AGX_OPCODE_SCOPED_BARRIER_AGX3 ||
       I->nr_dests != 0 || I->nr_srcs != 0 ||
       I->imm > AGX3_BARRIER_SCOPE_TEXTURE)
      return false;

   struct agx3_encoded_scoped_barrier encoded;
   return agx3_pack_scoped_barrier(enc, I->imm, &encoded) &&
          agx3_append(emission, encoded.bytes, encoded.length);
}

/* The grounded AGX3 compare forms are 32-bit and address the lower 128
 * half-registers. Source modifiers and memory/uniform operands select forms
 * whose routing has not been proved. */
static bool
agx3_cf_reg32(agx_index src)
{
   return agx3_src_is_register(src) && !src.memory && src.size == AGX_SIZE_32 &&
          (src.value & 1) == 0 && src.value < 128 && !src.abs && !src.neg;
}

static bool
agx3_cf_map_icond(enum agx_icond in, enum agx3_cf_icond *out)
{
   switch (in) {
   case AGX_ICOND_UEQ:
   case AGX_ICOND_SEQ:
      *out = AGX3_CF_ICOND_EQ;
      return true;
   case AGX_ICOND_ULT:
      *out = AGX3_CF_ICOND_ULT;
      return true;
   case AGX_ICOND_UGT:
      *out = AGX3_CF_ICOND_UGT;
      return true;
   case AGX_ICOND_SLT:
      *out = AGX3_CF_ICOND_SLT;
      return true;
   case AGX_ICOND_SGT:
      *out = AGX3_CF_ICOND_SGT;
      return true;
   }

   return false;
}

/* Normalize Mesa's seven float relations to AGX3's three base conditions,
 * or-NaN selector, and the consuming cnd instruction's invert bit. */
static bool
agx3_cf_map_fcond(enum agx_fcond in, enum agx3_cf_fcond *out, bool *or_nan,
                  bool *invert)
{
   *or_nan = false;

   switch (in) {
   case AGX_FCOND_EQ:
      *out = AGX3_CF_FCOND_EQ;
      return true;
   case AGX_FCOND_LT:
      *out = AGX3_CF_FCOND_LT;
      return true;
   case AGX_FCOND_GT:
      *out = AGX3_CF_FCOND_GT;
      return true;
   case AGX_FCOND_LTN:
      *out = AGX3_CF_FCOND_LT;
      *or_nan = true;
      return true;
   case AGX_FCOND_GTN:
      *out = AGX3_CF_FCOND_GT;
      *or_nan = true;
      return true;
   case AGX_FCOND_GE:
      *out = AGX3_CF_FCOND_LT;
      *or_nan = true;
      *invert = !*invert;
      return true;
   case AGX_FCOND_LE:
      *out = AGX3_CF_FCOND_GT;
      *or_nan = true;
      *invert = !*invert;
      return true;
   }

   return false;
}

static bool
agx3_cf_is_zero(agx_index src)
{
   return src.type == AGX_INDEX_IMMEDIATE && src.value == 0 && !src.abs &&
          !src.neg;
}

/* Emit the compare half of a fused AGX IR control-flow instruction. The flag
 * is consumed by the immediately following cnd instruction, so slot zero can
 * be reused by every lowered IR instruction without overlapping lifetimes. */
static bool
agx3_emit_cf_compare(struct util_dynarray *emission, enum agx_isa_encoding enc,
                     const agx_instr *I, bool is_float, unsigned flag_slot,
                     bool *invert)
{
   uint8_t bytes[AGX3_CF_MAX_LENGTH];
   uint8_t len = 0;

   if (I->nr_srcs != 2 || !agx3_cf_reg32(I->src[0]))
      return false;

   bool ok;
   if (is_float) {
      enum agx3_cf_fcond cond;
      bool or_nan;
      if (!agx3_cf_reg32(I->src[1]) ||
          !agx3_cf_map_fcond(I->fcond, &cond, &or_nan, invert))
         return false;

      ok = agx3_cf_fcmp_reg(enc, cond, or_nan, flag_slot, I->src[0].value,
                            I->src[1].value, bytes, &len);
   } else {
      enum agx3_cf_icond cond;
      if (!agx3_cf_map_icond(I->icond, &cond))
         return false;

      if (I->src[1].type == AGX_INDEX_IMMEDIATE && !I->src[1].abs &&
          !I->src[1].neg && I->src[1].value <= UINT8_MAX) {
         ok = agx3_cf_icmp_imm(enc, cond, flag_slot, I->src[0].value,
                               I->src[1].value, bytes, &len);
      } else if (agx3_cf_reg32(I->src[1])) {
         ok = agx3_cf_icmp_reg(enc, cond, flag_slot, I->src[0].value,
                               I->src[1].value, bytes, &len);
      } else {
         return false;
      }
   }

   return ok && agx3_append(emission, bytes, len);
}

static bool
agx3_emit_cnd(struct util_dynarray *emission, enum agx_isa_encoding enc,
              unsigned op, unsigned flag_slot, bool invert, unsigned count)
{
   uint8_t bytes[AGX3_CF_MAX_LENGTH];
   uint8_t len = 0;
   bool ok;

   switch (op) {
   case AGX_OPCODE_IF_ICMP:
   case AGX_OPCODE_IF_FCMP:
      ok = agx3_cf_cndst(enc, flag_slot, invert, count, bytes, &len);
      break;
   case AGX_OPCODE_WHILE_ICMP:
   case AGX_OPCODE_WHILE_FCMP:
   case AGX_OPCODE_BREAK_IF_ICMP:
   case AGX_OPCODE_BREAK_IF_FCMP:
      ok = agx3_cf_cndlt(enc, flag_slot, invert, bytes, &len);
      break;
   case AGX_OPCODE_ELSE_ICMP:
   case AGX_OPCODE_ELSE_FCMP:
      ok = agx3_cf_cndef(enc, bytes, &len);
      break;
   case AGX_OPCODE_POP_EXEC:
      ok = agx3_cf_cndend(enc, count, bytes, &len);
      break;
   default:
      return false;
   }

   return ok && agx3_append(emission, bytes, len);
}

/* Returns 1 when I is a grounded control-flow instruction and was emitted,
 * zero when it is not control flow, and -1 when it is control flow but falls
 * outside the grounded domain. */
static int
agx3_try_control_flow(struct util_dynarray *emission,
                      struct util_dynarray *fixups, enum agx_isa_encoding enc,
                      const agx_instr *I, uint16_t wait_mask, bool async_seen,
                      bool after_loop_backedge)
{
   const bool is_if =
      I->op == AGX_OPCODE_IF_ICMP || I->op == AGX_OPCODE_IF_FCMP;
   const bool is_while =
      I->op == AGX_OPCODE_WHILE_ICMP || I->op == AGX_OPCODE_WHILE_FCMP;
   const bool is_break_if =
      I->op == AGX_OPCODE_BREAK_IF_ICMP || I->op == AGX_OPCODE_BREAK_IF_FCMP;
   const bool is_else =
      I->op == AGX_OPCODE_ELSE_ICMP || I->op == AGX_OPCODE_ELSE_FCMP;
   const bool is_jump = I->op == AGX_OPCODE_JMP_EXEC_ANY ||
                        I->op == AGX_OPCODE_JMP_EXEC_NONE ||
                        I->op == AGX_OPCODE_JMP_EXEC_NONE_AFTER;

   /* Slot assignment is deliberately linear.  Once an async producer has
    * appeared, accepting any later control-flow boundary would require
    * predecessor-aware scoreboard state (including loop-carried state), which
    * is not grounded yet.  Keep those shaders closed instead of letting the
    * linear pre-pass invent a path-specific wait set. */
   if (async_seen &&
       (I->op == AGX_OPCODE_BEGIN_CF || is_if || is_while || is_break_if ||
        is_else || I->op == AGX_OPCODE_POP_EXEC || is_jump))
      return -1;

   if (I->op == AGX_OPCODE_BEGIN_CF)
      return wait_mask == 0 ? 1 : -1; /* architectural state: zero bytes */

   if (is_if && I->op == AGX_OPCODE_IF_FCMP && I->target == NULL &&
       I->nest == 1 && I->fcond == AGX_FCOND_EQ && !I->invert_cond &&
       I->nr_srcs == 2 && agx3_cf_is_zero(I->src[0]) &&
       agx3_cf_is_zero(I->src[1])) {
      /* agx_push_exec(1), used before a nested loop, becomes Apple's
       * unconditional TRUE-slot loop entry and opens two architectural
       * exec-mask levels. No compare is emitted. */
      return wait_mask == 0 && agx3_emit_cnd(emission, enc, I->op, 6, false, 2)
                ? 1
                : -1;
   }

   if (is_if || is_while || is_break_if) {
      /* The all-clear compare/cnd forms are witnessed only for register-ready
       * inputs.  A local dependency wait also keeps memory-dependent control
       * flow fail-closed. */
      if (wait_mask != 0)
         return -1;
      if ((is_if && I->nest != 1) || (is_break_if && I->nest != 1))
         return -1;

      bool invert = I->invert_cond;
      const bool is_float = I->op == AGX_OPCODE_IF_FCMP ||
                            I->op == AGX_OPCODE_WHILE_FCMP ||
                            I->op == AGX_OPCODE_BREAK_IF_FCMP;
      if (!agx3_emit_cf_compare(emission, enc, I, is_float, 0, &invert))
         return -1;
      if (!agx3_emit_cnd(emission, enc, I->op, 0, invert, is_if ? 1 : 2))
         return -1;
      return 1;
   }

   if (is_else) {
      /* The only witnessed else is the canonical unconditional
       * else_fcmp(0, 0, eq, nest=1) emitted by Mesa's structurizer. */
      if (wait_mask != 0 || I->op != AGX_OPCODE_ELSE_FCMP || I->nest != 1 ||
          I->fcond != AGX_FCOND_EQ || I->invert_cond || I->nr_srcs != 2 ||
          !agx3_cf_is_zero(I->src[0]) || !agx3_cf_is_zero(I->src[1]))
         return -1;
      return agx3_emit_cnd(emission, enc, I->op, 0, false, 1) ? 1 : -1;
   }

   if (I->op == AGX_OPCODE_POP_EXEC) {
      /* Mesa's loop footer is jmp_exec_any + pop_exec(1), while AGX3's loop
       * entry/test opens two architectural levels and therefore pops two. */
      const unsigned count = after_loop_backedge ? 2 : I->nest;
      return wait_mask == 0 &&
                   agx3_emit_cnd(emission, enc, I->op, 0, false, count)
                ? 1
                : -1;
   }

   if (is_jump) {
      if (wait_mask != 0 || I->target == NULL)
         return -1;

      const bool is_bn = I->op != AGX_OPCODE_JMP_EXEC_ANY;
      struct agx3_branch_fixup fixup = {
         .block = I->target,
         .offset = emission->size,
         .skip_to_end = I->op == AGX_OPCODE_JMP_EXEC_NONE_AFTER,
         .is_bn = is_bn,
      };
      util_dynarray_append(fixups, fixup);

      uint8_t bytes[AGX3_CF_MAX_LENGTH];
      uint8_t len = 0;
      return agx3_cf_branch(enc, is_bn, bytes, &len) &&
                   agx3_append(emission, bytes, len)
                ? 1
                : -1;
   }

   /* An unconditional break is deliberately not included: no AGX3 witness
    * exists, and agx_lower_pseudo preserves it so it reaches this refusal. */
   if (I->op == AGX_OPCODE_BREAK)
      return -1;

   return 0;
}

static bool
agx3_fixup_branch(struct util_dynarray *emission, enum agx_isa_encoding enc,
                  struct agx3_branch_fixup fixup)
{
   const off_t target =
      fixup.skip_to_end ? fixup.block->last_offset : fixup.block->offset;
   const int64_t displacement = (int64_t)target - (int64_t)fixup.offset;

   /* Only Apple's witnessed directions are accepted: bn skips forward and b
    * is a backwards loop edge. This also rejects zero-displacement branches. */
   if ((fixup.is_bn && displacement <= 0) ||
       (!fixup.is_bn && displacement >= 0))
      return false;
   if (fixup.offset < 0 ||
       (uint64_t)fixup.offset + AGX3_CF_MAX_LENGTH > emission->size)
      return false;

   uint8_t *branch = (uint8_t *)emission->data + fixup.offset;
   return agx3_cf_patch_branch(enc, fixup.is_bn, branch, displacement);
}

bool
agx3_pack_binary(agx_context *ctx, struct util_dynarray *emission)
{
   const enum agx_isa_encoding enc = ctx->key->dev.encoding;
   const unsigned nslots = (enc == AGX_ISA_ENCODING_AGX3C) ? 8 : 6;
   const unsigned initial_size = emission->size;
   struct util_dynarray fixups;
   bool async_seen = false;
   uint8_t next_uvs_slot = 1;
   unsigned texture_ops_left = 0;
   unsigned tile_loads_left = 0;
   unsigned tile_loads_seen = 0;
   unsigned count = 0;

   assert(enc == AGX_ISA_ENCODING_AGX3B || enc == AGX_ISA_ENCODING_AGX3C);
   util_dynarray_init(&fixups, ctx);

   agx_foreach_block(ctx, scan_block) {
      agx_foreach_instr_in_block(scan_block, scan_I) {
         if (scan_I->op != AGX_OPCODE_STOP)
            count++;
         /* Apple's "last texture op" marker goes on the final smp of the
          * whole stream, fetch or sample alike. */
         if (scan_I->op == AGX_OPCODE_TEXTURE_LOAD ||
             scan_I->op == AGX_OPCODE_TEXTURE_SAMPLE)
            texture_ops_left++;
         if (scan_I->op == AGX_OPCODE_LD_TILE)
            tile_loads_left++;
      }
   }

   /* Assign one scoreboard timeline over the whole linearized shader, rather
    * than restarting at each basic block.  Fallthrough blocks may consume an
    * async result from their predecessor.  Control flow after an async op is
    * rejected below until CFG-aware state merging is grounded. */
   struct agx3_slot_instr *sl =
      rzalloc_array(ctx, struct agx3_slot_instr, count ? count : 1);
   struct agx3_slot_result *res =
      rzalloc_array(ctx, struct agx3_slot_result, count ? count : 1);
   if (sl == NULL || res == NULL)
      goto fail;

   unsigned k = 0;
   agx_foreach_block(ctx, slot_block) {
      agx_foreach_instr_in_block(slot_block, I) {
         if (I->op == AGX_OPCODE_STOP)
            continue;
         struct agx3_slot_instr *e = &sl[k++];
         e->is_async = agx3_is_async(I);
         unsigned nd = 0, ns = 0;
         for (unsigned d = 0; d < I->nr_dests; ++d)
            nd = agx3_expand_regs(I->dest[d], e->dst, nd);
         for (unsigned s = 0; s < I->nr_srcs; ++s)
            ns = agx3_expand_regs(I->src[s], e->src, ns);
         e->nr_dsts = (uint8_t)nd;
         e->nr_srcs = (uint8_t)ns;
         if ((I->op == AGX_OPCODE_STACK_LOAD ||
              I->op == AGX_OPCODE_STACK_STORE) &&
             !agx3_append_static_spill_tokens(I, e))
            goto fail;
      }
   }
   assert(k == count);
   if (!agx3_slot_assign(sl, count, nslots, res))
      goto fail;

   k = 0;

   agx_foreach_block(ctx, block) {
      block->offset = emission->size;
      bool after_loop_backedge = false;
      agx_foreach_instr_in_block(block, I) {
         block->last_offset = emission->size;

         if (I->op == AGX_OPCODE_STOP)
            continue;

         const uint8_t slot = res[k].slot;
         const uint16_t wait = res[k].wait_mask;
         k++;

         const int cf = agx3_try_control_flow(emission, &fixups, enc, I, wait,
                                              async_seen, after_loop_backedge);
         if (cf < 0)
            AGX3_REFUSE(I);
         if (cf > 0) {
            after_loop_backedge = I->op == AGX_OPCODE_JMP_EXEC_ANY;
            continue;
         }
         after_loop_backedge = false;

         if (I->op == AGX_OPCODE_INTERP_AGX3) {
            if (!agx3_emit_interp(emission, enc, I, wait))
               AGX3_REFUSE(I);
            continue;
         }

         if (I->op == AGX_OPCODE_LDCF) {
            if (!agx3_emit_ldcf(emission, enc, I, slot, wait))
               AGX3_REFUSE(I);
            async_seen = true;
            continue;
         }

         if (I->op == AGX_OPCODE_ST_VARY) {
            if (!agx3_emit_uvsw(emission, enc, I, next_uvs_slot, wait))
               AGX3_REFUSE(I);
            next_uvs_slot = (next_uvs_slot % nslots) + 1;
            continue;
         }

         if (I->op == AGX_OPCODE_TEXTURE_LOAD) {
            assert(texture_ops_left > 0);
            const bool final = --texture_ops_left == 0;
            if (!agx3_emit_texture_fetch(emission, enc, I, slot, wait, final))
               AGX3_REFUSE(I);
            async_seen = true;
            continue;
         }

         if (I->op == AGX_OPCODE_TEXTURE_SAMPLE) {
            assert(texture_ops_left > 0);
            const bool final = --texture_ops_left == 0;
            /* The implicit-LOD form needs the 2x2 quad's derivatives; every
             * witness is a fragment shader and the hardware has no quad
             * outside one. */
            if (ctx->stage != MESA_SHADER_FRAGMENT ||
                !agx3_emit_texture_sample(emission, enc, I, slot, wait,
                                          final))
               AGX3_REFUSE(I);
            async_seen = true;
            continue;
         }

         if (I->op == AGX_OPCODE_WAIT_PIX ||
             I->op == AGX_OPCODE_SIGNAL_PIX) {
            if (!agx3_emit_pixel(emission, enc, I, wait))
               AGX3_REFUSE(I);
            continue;
         }

         if (I->op == AGX_OPCODE_LD_TILE || I->op == AGX_OPCODE_ST_TILE) {
            bool first = false, last = false;
            if (I->op == AGX_OPCODE_LD_TILE) {
               assert(tile_loads_left > 0);
               first = tile_loads_seen++ == 0;
               last = --tile_loads_left == 0;
            }
            if (!agx3_emit_tile(emission, enc, I, slot, wait, first, last))
               AGX3_REFUSE(I);
            async_seen |= I->op == AGX_OPCODE_LD_TILE;
            continue;
         }

         /* Special-register read. */
         if (I->op == AGX_OPCODE_GET_SR) {
            struct agx3_sr_desc d;
            uint8_t bytes[AGX3_SR_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_sr(I, slot, wait, &d) ||
                !agx3_sr_encode(enc, &d, bytes, &len))
               AGX3_REFUSE(I);
            memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
            async_seen = true;
            continue;
         }

         /* Device memory. */
         if (I->op == AGX_OPCODE_DEVICE_LOAD ||
             I->op == AGX_OPCODE_DEVICE_STORE) {
            const bool is_store = I->op == AGX_OPCODE_DEVICE_STORE;
            struct agx3_mem_desc d;
            uint8_t bytes[AGX3_MEM_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_mem(I, is_store, slot, wait, &d))
               AGX3_REFUSE(I);
            const bool ok = is_store
                               ? agx3_mem_encode_store(enc, &d, bytes, &len)
                               : agx3_mem_encode_load(enc, &d, bytes, &len);
            if (!ok)
               AGX3_REFUSE(I);
            memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
            async_seen |= !is_store;
            continue;
         }

         if (I->op == AGX_OPCODE_ATOMIC) {
            struct agx3_device_atomic_desc d;
            uint8_t bytes[AGX3_ATOMIC_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_device_atomic(I, enc, slot, wait, &d) ||
                !agx3_atomic_encode_device(enc, &d, bytes, &len) ||
                !agx3_append(emission, bytes, len))
               AGX3_REFUSE(I);
            async_seen |= d.returns_value;
            continue;
         }

         /* Threadgroup memory. The scaled register-indexed axis stays closed;
          * current Mesa IR reaches this path with its byte address in base and
          * an immediate-zero index. */
         if (I->op == AGX_OPCODE_LOCAL_LOAD ||
             I->op == AGX_OPCODE_LOCAL_STORE) {
            const bool is_store = I->op == AGX_OPCODE_LOCAL_STORE;
            struct agx3_local_desc d;
            uint8_t bytes[AGX3_LOCAL_TPS_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_local(I, is_store, slot, wait, &d))
               AGX3_REFUSE(I);
            const bool ok =
               is_store ? agx3_local_encode_store(enc, &d, bytes, &len)
                        : agx3_local_encode_load(enc, &d, bytes, &len);
            if (!ok)
               AGX3_REFUSE(I);
            memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
            async_seen |= !is_store;
            continue;
         }

         if (I->op == AGX_OPCODE_LOCAL_ATOMIC) {
            struct agx3_local_atomic_desc d;
            uint8_t bytes[AGX3_ATOMIC_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_local_atomic(I, slot, wait, &d) ||
                !agx3_atomic_encode_local(enc, &d, bytes, &len) ||
                !agx3_append(emission, bytes, len))
               AGX3_REFUSE(I);
            async_seen = true;
            continue;
         }

         if (I->op == AGX_OPCODE_SCS_LOAD_AGX3 ||
             I->op == AGX_OPCODE_SCS_STORE_AGX3 ||
             I->op == AGX_OPCODE_SCS_LOAD_UNIFORM_INDEXED_AGX3 ||
             I->op == AGX_OPCODE_SCS_STORE_UNIFORM_INDEXED_AGX3) {
            const bool is_store = I->op == AGX_OPCODE_SCS_STORE_AGX3 ||
                                  I->op ==
                                     AGX_OPCODE_SCS_STORE_UNIFORM_INDEXED_AGX3;
            const bool indexed =
               I->op == AGX_OPCODE_SCS_LOAD_UNIFORM_INDEXED_AGX3 ||
               I->op == AGX_OPCODE_SCS_STORE_UNIFORM_INDEXED_AGX3;
            struct agx3_scs_desc d;
            uint8_t bytes[AGX3_SCS_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_scs(I, is_store, indexed, slot, wait, &d))
               AGX3_REFUSE(I);
            const bool ok =
               is_store ? agx3_scs_encode_store(enc, &d, bytes, &len)
                        : agx3_scs_encode_load(enc, &d, bytes, &len);
            if (!ok)
               AGX3_REFUSE(I);
            memcpy(util_dynarray_grow_bytes(emission, 1, len), bytes, len);
            async_seen |= !is_store;
            continue;
         }

         /* Static TPS spill/reload. Both copy and rld signal scoreboard slots;
          * their synthetic TPS dependency tokens make a reload wait the copy
          * that produced the same stack location. */
         if (I->op == AGX_OPCODE_STACK_LOAD ||
             I->op == AGX_OPCODE_STACK_STORE) {
            const bool is_store = I->op == AGX_OPCODE_STACK_STORE;
            struct agx3_static_spill_store_desc store;
            struct agx3_static_spill_load_desc load;
            uint8_t bytes[AGX3_STATIC_SPILL_MAX_LENGTH];
            uint8_t len = 0;
            if (!agx3_translate_static_spill(I, is_store, slot, wait, &store,
                                             &load))
               AGX3_REFUSE(I);
            const bool ok =
               is_store ? agx3_static_spill_encode_store(enc, &store, bytes,
                                                          &len)
                        : agx3_static_spill_encode_load(enc, &load, bytes,
                                                        &len);
            if (!ok || !agx3_append(emission, bytes, len))
               AGX3_REFUSE(I);
            async_seen = true;
            continue;
         }

         bool subgroup_quad;
         enum agx3_subgroup_shuffle_op subgroup_shuffle;
         if (agx3_map_shuffle(I->op, &subgroup_quad, &subgroup_shuffle)) {
            if (!agx3_emit_subgroup_shuffle(emission, enc, I, wait))
               AGX3_REFUSE(I);
            continue;
         }

         if (I->op == AGX_OPCODE_SIMD_REDUCE ||
             I->op == AGX_OPCODE_QUAD_REDUCE ||
             I->op == AGX_OPCODE_SIMD_PREFIX ||
             I->op == AGX_OPCODE_QUAD_PREFIX) {
            if (!agx3_emit_subgroup_reduce(emission, enc, I, wait))
               AGX3_REFUSE(I);
            continue;
         }

         if (I->op == AGX_OPCODE_ICMP_BALLOT ||
             I->op == AGX_OPCODE_ICMP_QUAD_BALLOT ||
             I->op == AGX_OPCODE_FCMP_BALLOT ||
             I->op == AGX_OPCODE_FCMP_QUAD_BALLOT) {
            if (!agx3_emit_subgroup_ballot(emission, enc, I, wait))
               AGX3_REFUSE(I);
            continue;
         }

         if (I->op == AGX_OPCODE_SCOPED_BARRIER_AGX3) {
            if (!agx3_emit_scoped_barrier(emission, enc, I, wait))
               AGX3_REFUSE(I);
            continue;
         }

         /* Transcendental / rounding / gradient funops. The gradients need
          * the 2x2 quad, and every witness is a fragment shader. */
         const int fop = agx3_map_funop(I->op);
         if (fop >= 0) {
            if (((fop == AGX3_FUNOP_DFDX || fop == AGX3_FUNOP_DFDY) &&
                 ctx->stage != MESA_SHADER_FRAGMENT) ||
                !agx3_emit_funop(emission, enc, I, (enum agx3_funop)fop, wait))
               AGX3_REFUSE(I);
            continue;
         }

         /* Modifier-carrying float ALU (no wait support yet). */
         const int mod = agx3_try_float_modifier(emission, enc, I);
         if (mod == 1) {
            if (wait != 0)
               AGX3_REFUSE(I); /* modifier form cannot carry a wait */
            continue;
         }
         if (mod < 0)
            AGX3_REFUSE(I);

         struct agx3_sel_instr S;
         if (!agx3_translate_instr(I, &S) ||
             !agx3_emit_alu(emission, enc, &S, wait))
            AGX3_REFUSE(I);
      }
   }

   util_dynarray_foreach(&fixups, struct agx3_branch_fixup, fixup) {
      if (!agx3_fixup_branch(emission, enc, *fixup))
         goto fail;
   }

   if (!ctx->key->no_stop || ctx->is_preamble) {
      const struct agx3_sel_instr end = {.op = AGX3_SEL_OP_END};
      if (!agx3_emit(emission, enc, &end))
         goto fail;
   }

   util_dynarray_fini(&fixups);
   return true;

fail:
   /* Do not leave a valid-looking prefix behind.  The compiler entrypoint
    * propagates this failure and clears the shader part transactionally. */
   emission->size = initial_size;
   util_dynarray_fini(&fixups);
   return false;
}
