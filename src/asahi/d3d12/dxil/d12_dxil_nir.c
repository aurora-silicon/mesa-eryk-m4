/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * DXIL -> NIR translation. This is the piece that makes the campaign's
 * "no SPIR-V" claim real: DXIL instructions and dx.op intrinsics are
 * lowered straight onto NIR with nir_builder, and NIR is what the AGX
 * backend already consumes.
 *
 *     DXIL bitcode --(here)--> NIR --> agx_compile_shader_nir --> AGX ISA
 *
 * DXIL is emitted by DXC as LLVM 3.7 IR whose shader semantics live in
 * calls to `dx.op.*` intrinsics, each taking its opcode as the first
 * argument. Opcode numbering and the LLVM binop/cast codes are taken from
 * Mesa's own MIT-licensed DXIL writer (src/microsoft/compiler), which
 * emits this exact dialect.
 */

#include "d12_dxil.h"

#include <stdlib.h>
#include <string.h>

#include "agx_compile.h"
#include "nir.h"
#include "nir_builder.h"

#define MESA_LOG_TAG "d3d12agx"
#include "util/log.h"

/* dx.op opcodes (subset), from Mesa's enum dxil_intr */
enum d12_dxop {
   D12_DXOP_LOAD_INPUT = 4,
   D12_DXOP_STORE_OUTPUT = 5,
   D12_DXOP_FABS = 6,
   D12_DXOP_SATURATE = 7,
   D12_DXOP_FCOS = 12,
   D12_DXOP_FSIN = 13,
   D12_DXOP_FEXP2 = 21,
   D12_DXOP_FRC = 22,
   D12_DXOP_FLOG2 = 23,
   D12_DXOP_SQRT = 24,
   D12_DXOP_RSQRT = 25,
   D12_DXOP_ROUND_NE = 26,
   D12_DXOP_ROUND_NI = 27,
   D12_DXOP_ROUND_PI = 28,
   D12_DXOP_ROUND_Z = 29,
   D12_DXOP_BFREV = 30,
   D12_DXOP_COUNTBITS = 31,
   D12_DXOP_FIRSTBIT_LO = 32,
   D12_DXOP_FIRSTBIT_HI = 33,
   D12_DXOP_FIRSTBIT_SHI = 34,
   D12_DXOP_FMAX = 35,
   D12_DXOP_FMIN = 36,
   D12_DXOP_IMAX = 37,
   D12_DXOP_IMIN = 38,
   D12_DXOP_UMAX = 39,
   D12_DXOP_UMIN = 40,
   D12_DXOP_FMA = 47,
   D12_DXOP_IBFE = 51,
   D12_DXOP_UBFE = 52,
   D12_DXOP_BFI = 53,
   D12_DXOP_THREAD_ID = 93,
   D12_DXOP_GROUP_ID = 94,
   D12_DXOP_THREAD_ID_IN_GROUP = 95,
   D12_DXOP_FLATTENED_THREAD_ID_IN_GROUP = 96,
};

/* LLVM binop / cast codes, from Mesa's dxil_module.h */
enum {
   D12_BINOP_ADD = 0, D12_BINOP_SUB = 1, D12_BINOP_MUL = 2,
   D12_BINOP_UDIV = 3, D12_BINOP_SDIV = 4, D12_BINOP_UREM = 5,
   D12_BINOP_SREM = 6, D12_BINOP_SHL = 7, D12_BINOP_LSHR = 8,
   D12_BINOP_ASHR = 9, D12_BINOP_AND = 10, D12_BINOP_OR = 11,
   D12_BINOP_XOR = 12,
};

enum {
   D12_CAST_TRUNC = 0, D12_CAST_ZEXT = 1, D12_CAST_SEXT = 2,
   D12_CAST_FPTOUI = 3, D12_CAST_FPTOSI = 4, D12_CAST_UITOFP = 5,
   D12_CAST_SITOFP = 6, D12_CAST_FPTRUNC = 7, D12_CAST_FPEXT = 8,
   D12_CAST_BITCAST = 11,
};

/*
 * DXIL writes outputs one component at a time (storeOutput carries a
 * column index), but the AGX lowering wants one vector store per output.
 * Collect components here and flush them at the end of translation.
 */
#define D12_MAX_OUTPUTS 16

struct output_slot {
   bool used;
   unsigned location;
   unsigned base;
   nir_def *comps[4];
   unsigned write_mask;
};

struct trans_ctx {
   const struct d12_module *mod;
   const struct d12_function *func;
   nir_builder *b;

   /* value index -> NIR def (NULL when not yet defined / not a value) */
   nir_def **defs;
   unsigned num_defs;

   struct output_slot outputs[D12_MAX_OUTPUTS];

   const char *error;
};

static void
trans_fail(struct trans_ctx *ctx, const char *msg)
{
   if (!ctx->error)
      ctx->error = msg;
}

static bool
type_is_float(const struct d12_module *mod, uint32_t type)
{
   if (type >= mod->num_types)
      return false;
   enum d12_type_code k = mod->types[type].kind;
   return k == D12_TYPE_FLOAT || k == D12_TYPE_DOUBLE || k == D12_TYPE_HALF;
}

static unsigned
type_bit_size(const struct d12_module *mod, uint32_t type)
{
   if (type >= mod->num_types)
      return 32;

   const struct d12_type *t = &mod->types[type];
   switch (t->kind) {
   case D12_TYPE_HALF: return 16;
   case D12_TYPE_FLOAT: return 32;
   case D12_TYPE_DOUBLE: return 64;
   case D12_TYPE_INTEGER: return t->int_bits ? t->int_bits : 32;
   default: return 32;
   }
}

/* Resolve a value index to a NIR def, materializing constants on demand. */
static nir_def *
get_def(struct trans_ctx *ctx, uint32_t value_index)
{
   if (value_index >= ctx->mod->num_values) {
      trans_fail(ctx, "operand references an out-of-range value");
      return nir_imm_int(ctx->b, 0);
   }

   if (ctx->defs[value_index])
      return ctx->defs[value_index];

   const struct d12_value *v = &ctx->mod->values[value_index];

   if (v->kind == D12_VALUE_CONSTANT) {
      unsigned bits = type_bit_size(ctx->mod, v->type);
      nir_def *def;

      if (v->is_undef) {
         def = nir_undef(ctx->b, 1, bits);
      } else if (v->is_null) {
         def = nir_imm_intN_t(ctx->b, 0, bits);
      } else if (type_is_float(ctx->mod, v->type)) {
         /* CST_FLOAT carries raw bits of the target float type. */
         if (bits == 32) {
            union { uint32_t u; float f; } bitcast = {
               .u = (uint32_t)v->const_float };
            def = nir_imm_float(ctx->b, bitcast.f);
         } else if (bits == 64) {
            union { uint64_t u; double d; } bitcast = { .u = v->const_float };
            def = nir_imm_double(ctx->b, bitcast.d);
         } else {
            def = nir_imm_intN_t(ctx->b, v->const_float, bits);
         }
      } else {
         def = nir_imm_intN_t(ctx->b, v->const_int, bits);
      }

      ctx->defs[value_index] = def;
      return def;
   }

   trans_fail(ctx, "operand is not a constant or a defined instruction");
   return nir_imm_int(ctx->b, 0);
}

/* Constant integer operand (for dx.op opcodes, signature indices, etc.) */
static bool
const_uint(struct trans_ctx *ctx, uint32_t value_index, uint64_t *out)
{
   if (value_index >= ctx->mod->num_values)
      return false;

   const struct d12_value *v = &ctx->mod->values[value_index];
   if (v->kind != D12_VALUE_CONSTANT || v->is_undef)
      return false;

   *out = v->is_null ? 0 : v->const_int;
   return true;
}

static void
def_result(struct trans_ctx *ctx, const struct d12_inst *inst, nir_def *def)
{
   if (inst->result_value < ctx->mod->num_values)
      ctx->defs[inst->result_value] = def;
}

/*
 * dx.op call translation. `args` are the call's operands: args[0] is the
 * callee, args[1] the opcode constant, args[2..] the real parameters.
 */
static bool
translate_dxop(struct trans_ctx *ctx, const struct d12_inst *inst,
               uint64_t opcode)
{
   nir_builder *b = ctx->b;

   /* Parameters start after callee + opcode. */
   const uint32_t *p = inst->ops + 2;
   unsigned np = inst->num_ops > 2 ? inst->num_ops - 2 : 0;

#define NEED(n)                                                              \
   do {                                                                      \
      if (np < (n)) {                                                        \
         trans_fail(ctx, "dx.op call has too few arguments");                \
         return false;                                                       \
      }                                                                      \
   } while (0)

   switch (opcode) {
   case D12_DXOP_LOAD_INPUT: {
      /* (sigId, rowIndex, colIndex, gsVertexAxis) */
      NEED(3);
      uint64_t sig_id = 0, row = 0, col = 0;
      const_uint(ctx, p[0], &sig_id);
      const_uint(ctx, p[1], &row);
      if (!const_uint(ctx, p[2], &col)) {
         trans_fail(ctx, "dynamic component index on loadInput");
         return false;
      }

      /* Signature element -> IO slot. DXIL indexes signature elements;
       * map element N to a generic slot, consistently with outputs.
       */
      nir_def *val;

      if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         /* Fragment varyings must arrive interpolated: the AGX backend
          * consumes load_interpolated_input with a barycentric source,
          * not a raw load_input.
          */
         unsigned location = VARYING_SLOT_VAR0 + sig_id;
         nir_def *bary = nir_load_barycentric_pixel(b, 32);

         val = nir_load_interpolated_input(
            b, 1, 32, bary, nir_imm_int(b, row),
            .base = (unsigned)sig_id, .component = (unsigned)col,
            .dest_type = nir_type_float32,
            .io_semantics.location = location,
            .io_semantics.num_slots = 1);

         b->shader->info.inputs_read |= BITFIELD64_BIT(location);
      } else {
         unsigned location = VERT_ATTRIB_GENERIC0 + sig_id;

         val = nir_load_input(b, 1, 32, nir_imm_int(b, row),
                              .base = (unsigned)sig_id,
                              .component = (unsigned)col,
                              .dest_type = nir_type_float32,
                              .io_semantics.location = location,
                              .io_semantics.num_slots = 1);

         b->shader->info.inputs_read |= BITFIELD64_BIT(sig_id);
      }

      def_result(ctx, inst, val);
      return true;
   }

   case D12_DXOP_STORE_OUTPUT: {
      /* (sigId, rowIndex, colIndex, value) */
      NEED(4);
      uint64_t sig_id = 0, row = 0, col = 0;
      const_uint(ctx, p[0], &sig_id);
      const_uint(ctx, p[1], &row);
      if (!const_uint(ctx, p[2], &col)) {
         trans_fail(ctx, "dynamic component index on storeOutput");
         return false;
      }

      nir_def *val = get_def(ctx, p[3]);

      unsigned location;
      if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         location = FRAG_RESULT_DATA0 + sig_id;
      } else {
         /* Vertex position is signature element 0 by DXC convention for
          * SV_Position; anything else becomes a generic varying.
          */
         location = sig_id == 0 ? VARYING_SLOT_POS : (VARYING_SLOT_VAR0 + sig_id);
      }
      b->shader->info.outputs_written |= BITFIELD64_BIT(location);

      if (sig_id >= D12_MAX_OUTPUTS || col > 3) {
         trans_fail(ctx, "output signature index or component out of range");
         return false;
      }

      /* Buffered; flushed as one vector store per output. */
      struct output_slot *slot = &ctx->outputs[sig_id];
      slot->used = true;
      slot->location = location;
      slot->base = (unsigned)sig_id;
      slot->comps[col] = val;
      slot->write_mask |= 1u << col;
      return true;
   }

   /* Unary float */
   case D12_DXOP_FABS:     NEED(1); def_result(ctx, inst, nir_fabs(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_SATURATE: NEED(1); def_result(ctx, inst, nir_fsat(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FCOS:     NEED(1); def_result(ctx, inst, nir_fcos(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FSIN:     NEED(1); def_result(ctx, inst, nir_fsin(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FEXP2:    NEED(1); def_result(ctx, inst, nir_fexp2(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FLOG2:    NEED(1); def_result(ctx, inst, nir_flog2(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FRC:      NEED(1); def_result(ctx, inst, nir_ffract(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_SQRT:     NEED(1); def_result(ctx, inst, nir_fsqrt(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_RSQRT:    NEED(1); def_result(ctx, inst, nir_frsq(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_ROUND_NE: NEED(1); def_result(ctx, inst, nir_fround_even(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_ROUND_NI: NEED(1); def_result(ctx, inst, nir_ffloor(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_ROUND_PI: NEED(1); def_result(ctx, inst, nir_fceil(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_ROUND_Z:  NEED(1); def_result(ctx, inst, nir_ftrunc(b, get_def(ctx, p[0]))); return true;

   /* Unary integer */
   case D12_DXOP_BFREV:       NEED(1); def_result(ctx, inst, nir_bitfield_reverse(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_COUNTBITS:   NEED(1); def_result(ctx, inst, nir_bit_count(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FIRSTBIT_LO: NEED(1); def_result(ctx, inst, nir_find_lsb(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FIRSTBIT_HI: NEED(1); def_result(ctx, inst, nir_ufind_msb(b, get_def(ctx, p[0]))); return true;
   case D12_DXOP_FIRSTBIT_SHI:NEED(1); def_result(ctx, inst, nir_ifind_msb(b, get_def(ctx, p[0]))); return true;

   /* Binary */
   case D12_DXOP_FMAX: NEED(2); def_result(ctx, inst, nir_fmax(b, get_def(ctx, p[0]), get_def(ctx, p[1]))); return true;
   case D12_DXOP_FMIN: NEED(2); def_result(ctx, inst, nir_fmin(b, get_def(ctx, p[0]), get_def(ctx, p[1]))); return true;
   case D12_DXOP_IMAX: NEED(2); def_result(ctx, inst, nir_imax(b, get_def(ctx, p[0]), get_def(ctx, p[1]))); return true;
   case D12_DXOP_IMIN: NEED(2); def_result(ctx, inst, nir_imin(b, get_def(ctx, p[0]), get_def(ctx, p[1]))); return true;
   case D12_DXOP_UMAX: NEED(2); def_result(ctx, inst, nir_umax(b, get_def(ctx, p[0]), get_def(ctx, p[1]))); return true;
   case D12_DXOP_UMIN: NEED(2); def_result(ctx, inst, nir_umin(b, get_def(ctx, p[0]), get_def(ctx, p[1]))); return true;

   /* Ternary */
   case D12_DXOP_FMA:
      NEED(3);
      def_result(ctx, inst,
                 nir_ffma(b, get_def(ctx, p[0]), get_def(ctx, p[1]),
                          get_def(ctx, p[2])));
      return true;

   case D12_DXOP_IBFE:
      NEED(3);
      def_result(ctx, inst,
                 nir_ibitfield_extract(b, get_def(ctx, p[2]),
                                       get_def(ctx, p[1]),
                                       get_def(ctx, p[0])));
      return true;

   case D12_DXOP_UBFE:
      NEED(3);
      def_result(ctx, inst,
                 nir_ubitfield_extract(b, get_def(ctx, p[2]),
                                       get_def(ctx, p[1]),
                                       get_def(ctx, p[0])));
      return true;

   case D12_DXOP_BFI:
      /* (width, offset, value, replacedValue) */
      NEED(4);
      def_result(ctx, inst,
                 nir_bitfield_insert(b, get_def(ctx, p[3]),
                                     get_def(ctx, p[2]),
                                     get_def(ctx, p[1]),
                                     get_def(ctx, p[0])));
      return true;

   /* Compute system values */
   case D12_DXOP_THREAD_ID:
   case D12_DXOP_GROUP_ID:
   case D12_DXOP_THREAD_ID_IN_GROUP: {
      NEED(1);
      uint64_t comp = 0;
      if (!const_uint(ctx, p[0], &comp)) {
         trans_fail(ctx, "dynamic component on a thread-id intrinsic");
         return false;
      }

      nir_def *vec = opcode == D12_DXOP_THREAD_ID
                        ? nir_load_global_invocation_id(b, 32)
                        : (opcode == D12_DXOP_GROUP_ID
                              ? nir_load_workgroup_id(b)
                              : nir_load_local_invocation_id(b));

      def_result(ctx, inst, nir_channel(b, vec, (unsigned)comp));
      return true;
   }

   case D12_DXOP_FLATTENED_THREAD_ID_IN_GROUP:
      def_result(ctx, inst, nir_load_local_invocation_index(b));
      return true;

   default:
      mesa_loge("dxil: unhandled dx.op opcode %llu",
                (unsigned long long)opcode);
      trans_fail(ctx, "unhandled dx.op intrinsic");
      return false;
   }
#undef NEED
}

static bool
translate_binop(struct trans_ctx *ctx, const struct d12_inst *inst)
{
   nir_builder *b = ctx->b;

   if (inst->num_ops < 2 || inst->num_extra < 1) {
      trans_fail(ctx, "malformed binop");
      return false;
   }

   nir_def *x = get_def(ctx, inst->ops[0]);
   nir_def *y = get_def(ctx, inst->ops[1]);
   bool is_float = x->bit_size >= 16 &&
                   type_is_float(ctx->mod,
                                 ctx->mod->values[inst->ops[0]].type);
   nir_def *res = NULL;

   switch (inst->extra[0]) {
   case D12_BINOP_ADD:  res = is_float ? nir_fadd(b, x, y) : nir_iadd(b, x, y); break;
   case D12_BINOP_SUB:  res = is_float ? nir_fsub(b, x, y) : nir_isub(b, x, y); break;
   case D12_BINOP_MUL:  res = is_float ? nir_fmul(b, x, y) : nir_imul(b, x, y); break;
   case D12_BINOP_UDIV: res = nir_udiv(b, x, y); break;
   case D12_BINOP_SDIV: res = is_float ? nir_fdiv(b, x, y) : nir_idiv(b, x, y); break;
   case D12_BINOP_UREM: res = nir_umod(b, x, y); break;
   case D12_BINOP_SREM: res = is_float ? nir_frem(b, x, y) : nir_irem(b, x, y); break;
   case D12_BINOP_SHL:  res = nir_ishl(b, x, y); break;
   case D12_BINOP_LSHR: res = nir_ushr(b, x, y); break;
   case D12_BINOP_ASHR: res = nir_ishr(b, x, y); break;
   case D12_BINOP_AND:  res = nir_iand(b, x, y); break;
   case D12_BINOP_OR:   res = nir_ior(b, x, y); break;
   case D12_BINOP_XOR:  res = nir_ixor(b, x, y); break;
   default:
      trans_fail(ctx, "unhandled LLVM binop");
      return false;
   }

   def_result(ctx, inst, res);
   return true;
}

static bool
translate_cast(struct trans_ctx *ctx, const struct d12_inst *inst)
{
   nir_builder *b = ctx->b;

   if (inst->num_ops < 1 || inst->num_extra < 1) {
      trans_fail(ctx, "malformed cast");
      return false;
   }

   nir_def *src = get_def(ctx, inst->ops[0]);
   unsigned dst_bits = type_bit_size(ctx->mod, inst->result_type);
   nir_def *res = NULL;

   switch (inst->extra[0]) {
   case D12_CAST_TRUNC:   res = nir_u2uN(b, src, dst_bits); break;
   case D12_CAST_ZEXT:    res = nir_u2uN(b, src, dst_bits); break;
   case D12_CAST_SEXT:    res = nir_i2iN(b, src, dst_bits); break;
   case D12_CAST_FPTOUI:  res = nir_f2uN(b, src, dst_bits); break;
   case D12_CAST_FPTOSI:  res = nir_f2iN(b, src, dst_bits); break;
   case D12_CAST_UITOFP:  res = nir_u2fN(b, src, dst_bits); break;
   case D12_CAST_SITOFP:  res = nir_i2fN(b, src, dst_bits); break;
   case D12_CAST_FPTRUNC:
   case D12_CAST_FPEXT:   res = nir_f2fN(b, src, dst_bits); break;
   case D12_CAST_BITCAST: res = src; break;
   default:
      trans_fail(ctx, "unhandled LLVM cast");
      return false;
   }

   def_result(ctx, inst, res);
   return true;
}

/* Is this call a dx.op intrinsic, and if so what opcode? */
static bool
call_dxop_opcode(struct trans_ctx *ctx, const struct d12_inst *inst,
                 uint64_t *opcode)
{
   if (inst->num_ops < 2)
      return false;

   uint32_t callee_val = inst->ops[0];
   if (callee_val >= ctx->mod->num_values)
      return false;

   const struct d12_value *v = &ctx->mod->values[callee_val];
   if (v->kind != D12_VALUE_FUNCTION || !v->name)
      return false;

   if (strncmp(v->name, "dx.op.", 6))
      return false;

   return const_uint(ctx, inst->ops[1], opcode);
}

nir_shader *
d12_dxil_to_nir(const struct d12_module *mod, mesa_shader_stage stage,
                const char **error)
{
   /* The entry point is the module's single function definition. */
   const struct d12_function *entry = NULL;
   for (unsigned i = 0; i < mod->num_functions; i++) {
      if (!mod->functions[i].is_declaration) {
         entry = &mod->functions[i];
         break;
      }
   }

   if (!entry) {
      *error = "module has no function definition";
      return NULL;
   }

   nir_builder b = nir_builder_init_simple_shader(stage, &agx_nir_options,
                                                  "dxil");

   struct trans_ctx ctx = {
      .mod = mod,
      .func = entry,
      .b = &b,
      .num_defs = mod->num_values,
   };

   ctx.defs = calloc(mod->num_values, sizeof(*ctx.defs));
   if (!ctx.defs) {
      *error = "out of memory";
      ralloc_free(b.shader);
      return NULL;
   }

   if (stage == MESA_SHADER_FRAGMENT)
      b.shader->info.fs.untyped_color_outputs = false;

   for (unsigned i = 0; i < entry->num_insts && !ctx.error; i++) {
      const struct d12_inst *inst = &entry->insts[i];

      switch (inst->code) {
      case D12_FUNC_INST_CALL: {
         uint64_t opcode;
         if (call_dxop_opcode(&ctx, inst, &opcode)) {
            translate_dxop(&ctx, inst, opcode);
         } else {
            trans_fail(&ctx, "call to a non-dx.op function (inlining "
                             "required)");
         }
         break;
      }

      case D12_FUNC_INST_BINOP:
         translate_binop(&ctx, inst);
         break;

      case D12_FUNC_INST_CAST:
         translate_cast(&ctx, inst);
         break;

      case D12_FUNC_INST_RET:
         /* Falls out of the builder's single block. */
         break;

      case D12_FUNC_DECLAREBLOCKS:
         if (inst->extra[0] > 1) {
            /* Control flow needs the CFG reconstruction that comes with
             * loops/branches; single-block shaders work today.
             */
            trans_fail(&ctx, "multi-block shaders not yet supported");
         }
         break;

      default:
         mesa_loge("dxil: unhandled LLVM instruction code %u", inst->code);
         trans_fail(&ctx, "unhandled LLVM instruction");
         break;
      }
   }

   /* Flush buffered outputs as one vector store each. */
   for (unsigned i = 0; i < D12_MAX_OUTPUTS && !ctx.error; i++) {
      struct output_slot *slot = &ctx.outputs[i];
      if (!slot->used)
         continue;

      unsigned num_comps = util_last_bit(slot->write_mask);
      nir_def *comps[4];
      for (unsigned c = 0; c < num_comps; c++) {
         comps[c] = slot->comps[c] ? slot->comps[c]
                                   : nir_undef(&b, 1, 32);
      }

      nir_store_output(&b, nir_vec(&b, comps, num_comps), nir_imm_int(&b, 0),
                       .base = slot->base,
                       .write_mask = slot->write_mask,
                       .src_type = nir_type_float32,
                       .io_semantics.location = slot->location,
                       .io_semantics.num_slots = 1);
   }

   free(ctx.defs);

   if (ctx.error) {
      *error = ctx.error;
      ralloc_free(b.shader);
      return NULL;
   }

   nir_validate_shader(b.shader, "after DXIL translation");
   return b.shader;
}
