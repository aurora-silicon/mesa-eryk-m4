/*
 * Copyright 2026 Aurora Silicon
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
   D12_DXOP_CREATE_HANDLE = 57,
   D12_DXOP_CBUFFER_LOAD = 58,
   D12_DXOP_CBUFFER_LOAD_LEGACY = 59,
   D12_DXOP_BUFFER_LOAD = 68,
   D12_DXOP_BUFFER_STORE = 69,
   D12_DXOP_RAW_BUFFER_LOAD = 139,
   D12_DXOP_RAW_BUFFER_STORE = 140,
   D12_DXOP_ATOMIC_BINOP = 78,
   D12_DXOP_ATOMIC_CMPXCHG = 79,
   /* SM 6.6 replaced createHandle with a binding-descriptor form. It is by
    * far the most common resource intrinsic in modern DXIL.
    */
   /* SM 6.x declares resources as globals; this converts a loaded global
    * into a handle. Like annotateHandle, it is a rename.
    */
   /* Wave (subgroup) operations. AGX has these natively and NIR names
    * them the same way, so the whole family is a mapping exercise.
    */
   D12_DXOP_WAVE_GET_LANE_INDEX = 111,
   D12_DXOP_WAVE_GET_LANE_COUNT = 112,
   D12_DXOP_WAVE_ANY_TRUE = 113,
   D12_DXOP_WAVE_ALL_TRUE = 114,
   D12_DXOP_WAVE_ACTIVE_ALL_EQUAL = 115,
   D12_DXOP_WAVE_READ_LANE_AT = 117,
   D12_DXOP_WAVE_READ_LANE_FIRST = 118,
   D12_DXOP_WAVE_ALL_OP = 119,
   D12_DXOP_WAVE_PREFIX_OP = 121,
   D12_DXOP_WAVE_IS_FIRST_LANE = 110,
   D12_DXOP_CREATE_HANDLE_FOR_LIB = 160,
   D12_DXOP_ANNOTATE_HANDLE = 216,
   D12_DXOP_CREATE_HANDLE_FROM_BINDING = 217,
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

   /* Which slot each binding lives in. Assigned densely from the root
    * signature, so a register space costs nothing unless it is used.
    */
   const struct d12_binding_map *map;

   /* Per binding slot, from dx.resources: the structured element stride
    * (zero for raw and typed buffers) and the resource shape, which is
    * what actually decides how an access is addressed.
    */
   uint32_t slot_stride[D12_MAX_RESOURCE_BINDINGS];
   uint32_t slot_kind[D12_MAX_RESOURCE_BINDINGS];

   /* One NIR block per DXIL basic block, joined by goto/goto_if and
    * structurized afterwards. Empty for a single-block function, which is
    * built structured as before.
    */
   nir_block *blocks[D12_MAX_BLOCKS];
   unsigned num_blocks;

   /* value index -> the register a phi reads, for values that cross a
    * block boundary.
    */
   nir_def **phi_reg;

   const char *error;
};

static void trans_fail(struct trans_ctx *ctx, const char *msg);
static nir_def *get_def(struct trans_ctx *ctx, uint32_t value_index);

/* DXIL carries booleans as i1 already, but a value that reached us as a
 * wider integer has to be compared before NIR will take it as a condition.
 */
static nir_def *
to_bool(struct trans_ctx *ctx, nir_def *val)
{
   if (!val)
      return NULL;
   return val->bit_size == 1 ? val : nir_ine_imm(ctx->b, val, 0);
}

static unsigned type_bit_size(const struct d12_module *mod, uint32_t type);

/*
 * Write the values this block feeds into its successor's phis, then
 * branch. The stores have to happen in the PREDECESSOR, before the jump:
 * that is what makes a phi a phi -- which value arrives depends on where
 * control came from, and only the predecessor knows.
 */
static void
store_phis_for_edge(struct trans_ctx *ctx, const struct d12_function *func,
                    unsigned from_block, unsigned to_block)
{
   for (unsigned i = 0; i < func->num_insts; i++) {
      const struct d12_inst *phi = &func->insts[i];

      if (phi->code != D12_FUNC_INST_PHI || phi->block != to_block)
         continue;
      if (phi->result_value == ~0u || !ctx->phi_reg[phi->result_value])
         continue;

      for (unsigned s = 0; s < phi->num_phi_srcs; s++) {
         if (phi->phi_srcs[s].block != from_block)
            continue;

         nir_def *val = get_def(ctx, phi->phi_srcs[s].value);
         if (!val) {
            /* A back edge whose value is not defined yet. Undefined is
             * wrong but silent; refuse instead.
             */
            trans_fail(ctx, "phi source is not available at the branch");
            return;
         }

         nir_store_reg(ctx->b, val, ctx->phi_reg[phi->result_value]);
         break;
      }
   }
}

static void
translate_branch(struct trans_ctx *ctx, const struct d12_function *func,
                 const struct d12_inst *inst)
{
   nir_builder *b = ctx->b;

   if (ctx->num_blocks <= 1) {
      trans_fail(ctx, "branch in a single-block function");
      return;
   }

   const unsigned from = inst->block;

   if (inst->num_extra == 1) {
      /* Unconditional: [bb#] */
      unsigned target = (unsigned)inst->extra[0];
      if (target >= ctx->num_blocks) {
         trans_fail(ctx, "branch target out of range");
         return;
      }

      store_phis_for_edge(ctx, func, from, target);
      nir_goto(b, ctx->blocks[target]);
      return;
   }

   if (inst->num_extra >= 3 && inst->num_ops >= 1) {
      /* Conditional: [true bb#, false bb#, cond] */
      unsigned t = (unsigned)inst->extra[0];
      unsigned f = (unsigned)inst->extra[1];

      if (t >= ctx->num_blocks || f >= ctx->num_blocks) {
         trans_fail(ctx, "branch target out of range");
         return;
      }

      nir_def *cond = get_def(ctx, inst->ops[0]);
      if (!cond) {
         trans_fail(ctx, "branch condition is not available");
         return;
      }

      /* NIR wants a 1-bit condition. */
      if (cond->bit_size != 1)
         cond = nir_ine_imm(b, cond, 0);

      store_phis_for_edge(ctx, func, from, t);
      if (f != t)
         store_phis_for_edge(ctx, func, from, f);

      if (t == f) {
         nir_goto(b, ctx->blocks[t]);
      } else {
         nir_goto_if(b, ctx->blocks[t], cond, ctx->blocks[f]);
      }
      return;
   }

   trans_fail(ctx, "malformed branch");
}

/*
 * Decode a ResBind constant aggregate: { i32 rangeLowerBound,
 * i32 rangeUpperBound, i32 spaceID, i8 resourceClass }.
 */
static bool
decode_res_bind(struct trans_ctx *ctx, uint32_t value_id, unsigned *lower,
                unsigned *space, unsigned *klass)
{
   const struct d12_module *mod = ctx->mod;

   if (value_id >= mod->num_values) {
      trans_fail(ctx, "resource binding operand out of range");
      return false;
   }

   const struct d12_value *agg = &mod->values[value_id];

   if (agg->kind != D12_VALUE_CONSTANT) {
      trans_fail(ctx, "resource binding is not a constant ResBind struct");
      return false;
   }

   /* An all-zero ResBind -- register 0, space 0, class SRV -- is emitted
    * as a null constant of the struct type rather than as an aggregate of
    * four zeroes. That is the single most common binding in existence, so
    * treating "no elements" as malformed rejects a large fraction of real
    * shaders.
    */
   if (agg->is_null) {
      *lower = 0;
      *space = 0;
      *klass = D12_RES_SRV;
      return true;
   }

   if (agg->num_agg_elems < 4) {
      trans_fail(ctx, "resource binding is not a constant ResBind struct");
      return false;
   }

   unsigned out[4];
   for (unsigned i = 0; i < 4; i++) {
      uint32_t elem_id = agg->agg_elems[i];
      if (elem_id >= mod->num_values) {
         trans_fail(ctx, "malformed ResBind struct");
         return false;
      }
      const struct d12_value *e = &mod->values[elem_id];
      if (e->kind != D12_VALUE_CONSTANT) {
         trans_fail(ctx, "non-constant field in a ResBind struct");
         return false;
      }
      out[i] = e->is_null ? 0 : (unsigned)e->const_int;
   }

   *lower = out[0];
   *space = out[2];
   *klass = out[3];
   return true;
}

/*
 * The binding slot of the resource a global variable stands for, or ~0u if
 * the global is not a resource.
 */
static unsigned
resource_slot_for_global(struct trans_ctx *ctx, uint32_t value_id)
{
   const struct d12_module *mod = ctx->mod;

   if (value_id >= mod->num_values ||
       mod->values[value_id].kind != D12_VALUE_GLOBAL)
      return ~0u;

   for (unsigned i = 0; i < mod->num_resources; i++) {
      const struct d12_resource_binding *rb = &mod->resources[i];
      if (rb->global_value != value_id)
         continue;

      return d12_binding_map_find(ctx->map, rb->klass, rb->space,
                                  rb->lower_bound);
   }

   return ~0u;
}

/*
 * Resolve a DXIL resource reference to a root-table slot.
 *
 * `klass` and `range_id` name an entry in dx.resources; `index` selects a
 * register within that entry's range. DXC emits `index` as the ABSOLUTE
 * shader register, so for the common single-register resource it already
 * equals lowerBound -- but an array declared at t4[8] and indexed at
 * element 2 gives index = 6, not 2. Clamping to the declared range catches
 * a resource reference that does not correspond to anything declared,
 * which would otherwise silently read slot garbage.
 */
static bool
resolve_binding_slot(struct trans_ctx *ctx, unsigned klass,
                     unsigned range_id, unsigned index, unsigned *out_slot)
{
   const struct d12_module *mod = ctx->mod;

   for (unsigned i = 0; i < mod->num_resources; i++) {
      const struct d12_resource_binding *rb = &mod->resources[i];
      if (rb->klass != klass || rb->id != range_id)
         continue;

      unsigned count = d12_range_count(rb);
      if (!count) {
         trans_fail(ctx, "unbounded resource array (bindless) unsupported");
         return false;
      }

      /* Accept either reading of `index`: absolute when it already lands
       * inside the declared range, relative otherwise.
       */
      unsigned reg = index;
      if (reg < rb->lower_bound || reg >= rb->lower_bound + count)
         reg = rb->lower_bound + index;

      *out_slot = d12_binding_map_find(ctx->map, klass, rb->space, reg);
      if (*out_slot == ~0u) {
         trans_fail(ctx, "shader uses a resource the root signature does "
                         "not bind");
         return false;
      }

      if (getenv("D3D12AGX_DEBUG_BINDINGS")) {
         fprintf(stderr,
                 "binding: class %u rangeId %u index %u -> register %u "
                 "space %u, slot %u (declared %u..%u, stride %uB)\n",
                 klass, range_id, index, reg, rb->space, *out_slot,
                 rb->lower_bound,
                 rb->lower_bound + count - 1, rb->stride_B);
      }

      return true;
   }

   trans_fail(ctx, "resource handle names no declared resource");
   return false;
}

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
 * Resource address arithmetic for the binding model described at
 * D12_DXOP_CREATE_HANDLE. The base address of a slot is read from the root
 * table, then the DXIL coordinates are applied:
 *
 *   structured : addr = base + coord0 * stride + coord1
 *   raw        : addr = base + coord0            (coord1 unused)
 *
 * Which of those applies is decided by the resource SHAPE from
 * dx.resources, not by which dx.op was called: SM 6.0 reaches a
 * ByteAddressBuffer through the same bufferLoad a structured buffer uses.
 * Treating every bufferLoad as structured with an assumed 4-byte stride
 * multiplied raw byte offsets by four -- addresses that were wrong, in
 * bounds, and completely silent.
 *
 * The stride, when one applies, is the one the shader declared. It was
 * also previously hardcoded to 4, which is right only for buffers of
 * 32-bit scalars: a StructuredBuffer of a 12-byte struct indexed at
 * element 3 read from byte 12 instead of byte 36.
 */
static nir_def *
resource_address(struct trans_ctx *ctx, unsigned slot, nir_def *coord0,
                 nir_def *coord1, bool raw_op)
{
   nir_builder *b = ctx->b;

   if (slot >= D12_MAX_RESOURCE_BINDINGS) {
      trans_fail(ctx, "resource binding index out of range");
      return nir_imm_int64(b, 0);
   }

   /* One indirection through the root table. load_root_agx is a
    * placeholder lowered to a uniform read after preprocessing.
    */
   nir_def *root = nir_load_root_agx(b);
   nir_def *base = nir_load_global(b, 1, 64,
                                   nir_iadd_imm(b, root, slot * 8),
                                   .align_mul = 8);

   const uint32_t kind = ctx->slot_kind[slot];
   const uint32_t stride = ctx->slot_stride[slot];

   /* rawBufferLoad/Store are byte-addressed whatever the shape is. */
   bool structured =
      !raw_op && kind == D12_RES_KIND_STRUCTURED_BUFFER;

   if (!raw_op && kind == D12_RES_KIND_TYPED_BUFFER) {
      trans_fail(ctx, "typed buffer access (needs format conversion)");
      return nir_imm_int64(b, 0);
   }

   nir_def *offset;
   if (structured) {
      if (!stride) {
         trans_fail(ctx, "structured buffer with no declared element "
                         "stride");
         return nir_imm_int64(b, 0);
      }
      offset = nir_iadd(b, nir_imul_imm(b, coord0, stride), coord1);
   } else {
      offset = coord0;
   }

   return nir_iadd(b, base, nir_u2u64(b, offset));
}

/*
 * dx.op call translation. `args` are the call's operands: args[0] is the
 * callee, args[1] the opcode constant, args[2..] the real parameters.
 */
static bool
translate_dxop(struct trans_ctx *ctx, const struct d12_inst *inst,
               uint64_t opcode, const char *callee)
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

      /* Resolve the semantic so system values become the right NIR
       * intrinsic rather than a generic varying read.
       */
      const struct d12_sig_element *se =
         d12_dxil_find_sig(ctx->mod->input_sig, ctx->mod->num_input_sig,
                           (uint32_t)sig_id);
      const char *sem = se ? se->name : NULL;

      if (sem && !strcmp(sem, "SV_VertexID")) {
         def_result(ctx, inst, nir_load_vertex_id(b));
         return true;
      }

      if (sem && !strcmp(sem, "SV_InstanceID")) {
         def_result(ctx, inst, nir_load_instance_id(b));
         return true;
      }

      if (sem && !strcmp(sem, "SV_Position") &&
          b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         /* Fragment position is the interpolated frag coord, not a
          * user varying.
          */
         nir_def *coord = nir_load_frag_coord(b);
         def_result(ctx, inst, nir_channel(b, coord, (unsigned)col));
         b->shader->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_POS);
         return true;
      }

      if (sem && !strcmp(sem, "SV_IsFrontFace")) {
         def_result(ctx, inst, nir_load_front_face(b, 1));
         return true;
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

      /* Semantic-driven, not positional: SV_Position is the clip-space
       * position wherever it appears in the signature.
       */
      const struct d12_sig_element *se =
         d12_dxil_find_sig(ctx->mod->output_sig, ctx->mod->num_output_sig,
                           (uint32_t)sig_id);
      const char *sem = se ? se->name : NULL;

      unsigned location;
      if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         /* SV_Target[n]; the semantic index rides in start_row. */
         location = FRAG_RESULT_DATA0 + (se ? se->start_row : sig_id);
      } else if (sem && !strcmp(sem, "SV_Position")) {
         location = VARYING_SLOT_POS;
      } else {
         location = VARYING_SLOT_VAR0 + sig_id;
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

   /*
    * Resources. The binding model (campaign OQ-2): each bound resource's
    * 64-bit base address lives in AGX uniform registers, four 16-bit
    * registers per resource, starting at D12_UNIFORM_RESOURCE_BASE. The
    * shader reads them with load_preamble; the driver publishes them with
    * agx_usc_uniform. createHandle therefore resolves to nothing more than
    * its binding index, and the address arithmetic happens at the access.
    */
   case D12_DXOP_CREATE_HANDLE: {
      /* (resourceClass, rangeId, index, nonUniformIndex) */
      NEED(3);
      uint64_t klass = 0, range_id = 0, index = 0;
      if (!const_uint(ctx, p[0], &klass) ||
          !const_uint(ctx, p[1], &range_id)) {
         trans_fail(ctx, "dynamic resource range index unsupported");
         return false;
      }
      if (!const_uint(ctx, p[2], &index)) {
         trans_fail(ctx, "dynamic resource array index unsupported");
         return false;
      }

      unsigned slot;
      if (!resolve_binding_slot(ctx, (unsigned)klass, (unsigned)range_id,
                                (unsigned)index, &slot))
         return false;

      /* The handle IS the binding slot for our model. */
      def_result(ctx, inst, nir_imm_int(b, slot));
      return true;
   }

   case D12_DXOP_ATOMIC_BINOP: {
      /* (handle, atomicOp, offset0, offset1, offset2, value)
       *
       * The three offsets address the resource the same way a load or
       * store does; a buffer uses offset0 and, when structured, offset1.
       */
      NEED(6);

      nir_def *handle = get_def(ctx, p[0]);
      if (!nir_src_is_const(nir_src_for_ssa(handle))) {
         trans_fail(ctx, "non-constant resource handle unsupported");
         return false;
      }
      unsigned slot = nir_src_as_uint(nir_src_for_ssa(handle));

      uint64_t dx_op = 0;
      if (!const_uint(ctx, p[1], &dx_op)) {
         trans_fail(ctx, "non-constant atomic operation");
         return false;
      }

      nir_atomic_op op;
      switch (dx_op) {
      case 0: op = nir_atomic_op_iadd; break;
      case 1: op = nir_atomic_op_iand; break;
      case 2: op = nir_atomic_op_ior; break;
      case 3: op = nir_atomic_op_ixor; break;
      case 4: op = nir_atomic_op_imin; break;
      case 5: op = nir_atomic_op_imax; break;
      case 6: op = nir_atomic_op_umin; break;
      case 7: op = nir_atomic_op_umax; break;
      case 8: op = nir_atomic_op_xchg; break;
      default:
         trans_fail(ctx, "unhandled atomic operation");
         return false;
      }

      nir_def *addr = resource_address(ctx, slot, get_def(ctx, p[2]),
                                       get_def(ctx, p[3]), false);
      def_result(ctx, inst,
                 nir_global_atomic(b, 32, addr, get_def(ctx, p[5]),
                                   .atomic_op = op));
      return true;
   }

   case D12_DXOP_ATOMIC_CMPXCHG: {
      /* (handle, offset0, offset1, offset2, compareValue, newValue) */
      NEED(6);

      nir_def *handle = get_def(ctx, p[0]);
      if (!nir_src_is_const(nir_src_for_ssa(handle))) {
         trans_fail(ctx, "non-constant resource handle unsupported");
         return false;
      }
      unsigned slot = nir_src_as_uint(nir_src_for_ssa(handle));

      nir_def *addr = resource_address(ctx, slot, get_def(ctx, p[1]),
                                       get_def(ctx, p[2]), false);
      def_result(ctx, inst,
                 nir_global_atomic_swap(b, 32, addr, get_def(ctx, p[4]),
                                        get_def(ctx, p[5]),
                                        .atomic_op = nir_atomic_op_cmpxchg));
      return true;
   }

   case D12_DXOP_WAVE_GET_LANE_INDEX:
      def_result(ctx, inst, nir_load_subgroup_invocation(b));
      return true;

   case D12_DXOP_WAVE_GET_LANE_COUNT:
      def_result(ctx, inst, nir_load_subgroup_size(b));
      return true;

   case D12_DXOP_WAVE_IS_FIRST_LANE:
      def_result(ctx, inst, nir_elect(b, 1));
      return true;

   case D12_DXOP_WAVE_ANY_TRUE:
      NEED(1);
      def_result(ctx, inst, nir_vote_any(b, 1, to_bool(ctx, get_def(ctx, p[0]))));
      return true;

   case D12_DXOP_WAVE_ALL_TRUE:
      NEED(1);
      def_result(ctx, inst, nir_vote_all(b, 1, to_bool(ctx, get_def(ctx, p[0]))));
      return true;

   case D12_DXOP_WAVE_ACTIVE_ALL_EQUAL:
      NEED(1);
      def_result(ctx, inst, nir_vote_ieq(b, 1, get_def(ctx, p[0])));
      return true;

   case D12_DXOP_WAVE_READ_LANE_FIRST:
      NEED(1);
      def_result(ctx, inst, nir_read_first_invocation(b, get_def(ctx, p[0])));
      return true;

   case D12_DXOP_WAVE_READ_LANE_AT:
      NEED(2);
      def_result(ctx, inst,
                 nir_read_invocation(b, get_def(ctx, p[0]),
                                     get_def(ctx, p[1])));
      return true;

   case D12_DXOP_WAVE_ALL_OP:
   case D12_DXOP_WAVE_PREFIX_OP: {
      /* (value, op, signedness). WaveActiveSum and friends reduce across
       * the wave; WavePrefixSum scans it exclusively.
       */
      NEED(3);

      uint64_t dx_op = 0, sign = 0;
      if (!const_uint(ctx, p[1], &dx_op) || !const_uint(ctx, p[2], &sign)) {
         trans_fail(ctx, "non-constant wave operation");
         return false;
      }

      nir_def *val = get_def(ctx, p[0]);
      bool is_float = val && val->bit_size >= 16 &&
                      type_is_float(ctx->mod, inst->result_type);
      bool is_signed = sign == 0;

      nir_op op;
      switch (dx_op) {
      case 0: op = is_float ? nir_op_fadd : nir_op_iadd; break; /* Sum */
      case 1: op = is_float ? nir_op_fmul : nir_op_imul; break; /* Product */
      case 2: /* Min */
         op = is_float ? nir_op_fmin : (is_signed ? nir_op_imin : nir_op_umin);
         break;
      case 3: /* Max */
         op = is_float ? nir_op_fmax : (is_signed ? nir_op_imax : nir_op_umax);
         break;
      default:
         trans_fail(ctx, "unhandled wave operation kind");
         return false;
      }

      if (opcode == D12_DXOP_WAVE_ALL_OP) {
         def_result(ctx, inst, nir_reduce(b, val, .reduction_op = op));
      } else {
         /* DXIL's WavePrefix* is exclusive. */
         def_result(ctx, inst, nir_exclusive_scan(b, val, .reduction_op = op));
      }
      return true;
   }

   case D12_DXOP_CREATE_HANDLE_FOR_LIB:
   case D12_DXOP_ANNOTATE_HANDLE: {
      /* (handle, resourceProperties) -> the same handle.
       *
       * SM 6.6 attaches the resource's type and dimensions to a handle so
       * a runtime can validate them; the handle itself is unchanged. It
       * appears in nearly every SM 6.6 shader that touches a resource,
       * which made it the single largest blocker in the corpus -- 58 of
       * 334 shaders -- for what amounts to a rename.
       */
      NEED(1);
      def_result(ctx, inst, get_def(ctx, p[0]));
      return true;
   }

   case D12_DXOP_CREATE_HANDLE_FROM_BINDING: {
      /* (bind, index, nonUniformIndex). `bind` is a constant ResBind
       * aggregate { rangeLowerBound, rangeUpperBound, spaceID,
       * resourceClass }; the class must come from it, because the
       * register alone does not identify a resource -- t2 and u2 are
       * different bindings with the same number.
       */
      NEED(2);
      uint64_t index = 0;
      if (!const_uint(ctx, p[1], &index)) {
         trans_fail(ctx, "dynamic resource binding index unsupported");
         return false;
      }

      unsigned klass, lower, space;
      if (!decode_res_bind(ctx, p[0], &lower, &space, &klass))
         return false;

      /* `index` is the absolute shader register. */
      unsigned reg = (unsigned)index;
      if (reg < lower)
         reg = lower + (unsigned)index;

      if (klass >= D12_RES_CLASS_COUNT) {
         trans_fail(ctx, "ResBind names an unknown resource class");
         return false;
      }

      unsigned slot = d12_binding_map_find(ctx->map, klass, space, reg);
      if (slot == ~0u) {
         trans_fail(ctx, "shader uses a resource the root signature does "
                         "not bind");
         return false;
      }

      if (getenv("D3D12AGX_DEBUG_BINDINGS")) {
         fprintf(stderr,
                 "binding: fromBinding class %u lower %u index %llu -> "
                 "register %u space %u, slot %u\n",
                 klass, lower, (unsigned long long)index, reg, space, slot);
      }

      def_result(ctx, inst, nir_imm_int(b, slot));
      return true;
   }

   case D12_DXOP_CBUFFER_LOAD_LEGACY: {
      /* (handle, regIndex) -> one 16-byte constant-buffer row, which DXIL
       * consumes with extractvalue. A row is a vec4.
       */
      NEED(2);

      nir_def *handle = get_def(ctx, p[0]);
      if (!nir_src_is_const(nir_src_for_ssa(handle))) {
         trans_fail(ctx, "non-constant cbuffer handle unsupported");
         return false;
      }
      unsigned binding = nir_src_as_uint(nir_src_for_ssa(handle));

      /* Rows are 16 bytes; the row index scales, no second coordinate. */
      nir_def *offset = nir_imul_imm(b, get_def(ctx, p[1]), 16);
      nir_def *addr = resource_address(ctx, binding, offset,
                                       nir_imm_int(b, 0), true);

      def_result(ctx, inst, nir_load_global(b, 4, 32, addr, .align_mul = 16));
      return true;
   }

   case D12_DXOP_BUFFER_STORE:
   case D12_DXOP_RAW_BUFFER_STORE: {
      /* (handle, coord0, coord1, val0..val3, mask [, align]) */
      NEED(8);

      nir_def *handle = get_def(ctx, p[0]);
      if (!nir_src_is_const(nir_src_for_ssa(handle))) {
         trans_fail(ctx, "non-constant resource handle unsupported");
         return false;
      }
      unsigned binding = nir_src_as_uint(nir_src_for_ssa(handle));

      uint64_t mask = 0;
      if (!const_uint(ctx, p[7], &mask))
         mask = 0xf;

      nir_def *addr = resource_address(ctx, binding, get_def(ctx, p[1]),
                                       get_def(ctx, p[2]),
                                       opcode == D12_DXOP_RAW_BUFFER_STORE);

      /* Contiguous low components only; DXIL emits the common case. */
      unsigned comps = util_last_bit((unsigned)mask & 0xf);
      if (!comps || ((unsigned)mask & 0xf) != BITFIELD_MASK(comps)) {
         trans_fail(ctx, "sparse buffer store write mask unsupported");
         return false;
      }

      nir_def *vals[4];
      for (unsigned i = 0; i < comps; i++)
         vals[i] = get_def(ctx, p[3 + i]);

      nir_store_global(b, nir_vec(b, vals, comps), addr, .align_mul = 4);
      return true;
   }

   case D12_DXOP_BUFFER_LOAD:
   case D12_DXOP_RAW_BUFFER_LOAD: {
      /* (handle, coord0, coord1 [, mask, align]) */
      NEED(3);

      nir_def *handle = get_def(ctx, p[0]);
      if (!nir_src_is_const(nir_src_for_ssa(handle))) {
         trans_fail(ctx, "non-constant resource handle unsupported");
         return false;
      }
      unsigned binding = nir_src_as_uint(nir_src_for_ssa(handle));

      nir_def *addr = resource_address(ctx, binding, get_def(ctx, p[1]),
                                       get_def(ctx, p[2]),
                                       opcode == D12_DXOP_RAW_BUFFER_LOAD);

      /* DXIL buffer loads return a 4-vector; extractvalue picks lanes. */
      def_result(ctx, inst, nir_load_global(b, 4, 32, addr, .align_mul = 4));
      return true;
   }

   default:
      /* Name the intrinsic, not just its number: the name is right there
       * in the module and is what makes a corpus run actionable.
       */
      mesa_loge("dxil: unhandled dx.op opcode %llu (%s)",
                (unsigned long long)opcode, callee ? callee : "?");
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

/* LLVM compare predicates (float 0-15, integer 32-41) */
static bool
translate_cmp(struct trans_ctx *ctx, const struct d12_inst *inst)
{
   nir_builder *b = ctx->b;

   if (inst->num_ops < 2 || inst->num_extra < 1) {
      trans_fail(ctx, "malformed compare");
      return false;
   }

   nir_def *x = get_def(ctx, inst->ops[0]);
   nir_def *y = get_def(ctx, inst->ops[1]);
   nir_def *res = NULL;

   switch (inst->extra[0]) {
   /* Float predicates. Ordered and unordered forms are mapped to the
    * same NIR comparison; AGX has no NaN-signalling distinction here.
    */
   case 1: case 9:  res = nir_feq(b, x, y); break;  /* OEQ / UEQ */
   case 2: case 10: res = nir_flt(b, y, x); break;  /* OGT / UGT */
   case 3: case 11: res = nir_fge(b, x, y); break;  /* OGE / UGE */
   case 4: case 12: res = nir_flt(b, x, y); break;  /* OLT / ULT */
   case 5: case 13: res = nir_fge(b, y, x); break;  /* OLE / ULE */
   case 6: case 14: res = nir_fneu(b, x, y); break; /* ONE / UNE */

   /* Integer predicates */
   case 32: res = nir_ieq(b, x, y); break;
   case 33: res = nir_ine(b, x, y); break;
   case 34: res = nir_ult(b, y, x); break;
   case 35: res = nir_uge(b, x, y); break;
   case 36: res = nir_ult(b, x, y); break;
   case 37: res = nir_uge(b, y, x); break;
   case 38: res = nir_ilt(b, y, x); break;
   case 39: res = nir_ige(b, x, y); break;
   case 40: res = nir_ilt(b, x, y); break;
   case 41: res = nir_ige(b, y, x); break;

   default:
      trans_fail(ctx, "unhandled compare predicate");
      return false;
   }

   def_result(ctx, inst, res);
   return true;
}

/* select: LLVM orders operands (true, false, cond) */
static bool
translate_select(struct trans_ctx *ctx, const struct d12_inst *inst)
{
   if (inst->num_ops < 3) {
      trans_fail(ctx, "malformed select");
      return false;
   }

   nir_def *t = get_def(ctx, inst->ops[0]);
   nir_def *f = get_def(ctx, inst->ops[1]);
   nir_def *cond = get_def(ctx, inst->ops[2]);

   /* NIR wants a 1-bit boolean condition. */
   if (cond->bit_size != 1)
      cond = nir_ine_imm(ctx->b, cond, 0);

   def_result(ctx, inst, nir_bcsel(ctx->b, cond, t, f));
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
                 uint64_t *opcode, const char **name)
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

   *name = v->name;
   return const_uint(ctx, inst->ops[1], opcode);
}

nir_shader *
d12_dxil_to_nir(const struct d12_module *mod, mesa_shader_stage stage,
                const struct d12_binding_map *map, const char **error)
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
      .map = map,
      .func = entry,
      .b = &b,
      .num_defs = mod->num_values,
   };

   ctx.defs = calloc(mod->num_values, sizeof(*ctx.defs));
   ctx.phi_reg = calloc(mod->num_values, sizeof(*ctx.phi_reg));
   if (!ctx.defs || !ctx.phi_reg) {
      *error = "out of memory";
      free(ctx.defs);
      free(ctx.phi_reg);
      ralloc_free(b.shader);
      return NULL;
   }

   /* Index the declared strides by slot so the access sites do not have to
    * search the resource list again.
    */
   for (unsigned i = 0; i < mod->num_resources; i++) {
      const struct d12_resource_binding *rb = &mod->resources[i];
      if (rb->klass >= D12_RES_CLASS_COUNT)
         continue;

      unsigned count = d12_range_count(rb);

      for (unsigned r = 0; r < count; r++) {
         unsigned slot = d12_binding_map_find(map, rb->klass, rb->space,
                                              rb->lower_bound + r);
         if (slot == ~0u)
            continue; /* not bound by the signature; caught at first use */

         ctx.slot_stride[slot] = rb->stride_B;
         ctx.slot_kind[slot] = rb->kind;
      }
   }

   if (stage == MESA_SHADER_FRAGMENT) {
      b.shader->info.fs.untyped_color_outputs = false;
   } else if (stage == MESA_SHADER_COMPUTE) {
      /* [numthreads(x,y,z)]; without it the launch grid is empty. */
      for (unsigned i = 0; i < 3; i++) {
         b.shader->info.workgroup_size[i] =
            mod->num_threads[i] ? mod->num_threads[i] : 1;
      }
   }

   /*
    * Control flow.
    *
    * DXIL is LLVM: an unstructured CFG of basic blocks joined by branches.
    * NIR wants structured control flow. Rather than reconstruct if/loop
    * shapes ourselves, the function is built UNSTRUCTURED -- one NIR block
    * per DXIL block, joined by nir_goto/nir_goto_if -- and handed to
    * nir_lower_goto_ifs, the structurizer Mesa already uses for SPIR-V
    * kernels. Doing it any other way means writing a structurizer.
    */
   unsigned num_blocks = 1;
   for (unsigned i = 0; i < entry->num_insts; i++)
      num_blocks = MAX2(num_blocks, entry->insts[i].block + 1);

   if (num_blocks > D12_MAX_BLOCKS) {
      *error = "function has too many basic blocks";
      free(ctx.defs);
      ralloc_free(b.shader);
      return NULL;
   }

   ctx.num_blocks = num_blocks;

   if (num_blocks > 1) {
      b.impl->structured = false;

      ctx.blocks[0] = nir_start_block(b.impl);
      for (unsigned i = 1; i < num_blocks; i++) {
         nir_block *blk = nir_block_create(b.shader);
         exec_list_push_tail(&b.impl->body, &blk->cf_node.node);
         blk->cf_node.parent = &b.impl->cf_node;
         ctx.blocks[i] = blk;
      }

      /*
       * Phis become registers rather than NIR phi nodes: each predecessor
       * stores its incoming value just before branching, and the phi reads
       * it back. nir_lower_reg_intrinsics_to_ssa then rebuilds real phis
       * with correct placement. Constructing them here instead would mean
       * doing SSA construction by hand, for no gain.
       */
      b.cursor = nir_before_block(ctx.blocks[0]);
      for (unsigned i = 0; i < entry->num_insts; i++) {
         const struct d12_inst *inst = &entry->insts[i];
         if (inst->code != D12_FUNC_INST_PHI || inst->result_value == ~0u)
            continue;

         unsigned bits = type_bit_size(mod, inst->result_type);
         ctx.phi_reg[inst->result_value] = nir_decl_reg(&b, 1, bits, 0);
      }
   }

   unsigned cur_block = ~0u;

   for (unsigned i = 0; i < entry->num_insts && !ctx.error; i++) {
      const struct d12_inst *inst = &entry->insts[i];

      /* Move to this instruction's block when it changes. */
      if (num_blocks > 1 && inst->block != cur_block) {
         cur_block = inst->block;
         if (cur_block >= num_blocks) {
            trans_fail(&ctx, "instruction in a block that does not exist");
            break;
         }
         b.cursor = nir_after_block(ctx.blocks[cur_block]);
      }

      switch (inst->code) {
      case D12_FUNC_INST_CALL: {
         uint64_t opcode;
         const char *callee = NULL;
         if (call_dxop_opcode(&ctx, inst, &opcode, &callee)) {
            translate_dxop(&ctx, inst, opcode, callee);
         } else {
            /* Name it: "some call failed" is not actionable, and the
             * callee is right there in the module.
             */
            /* Report what the callee operand actually resolved to.
             * "some call failed" is not actionable, and the answer is
             * usually that the value is not a function at all.
             */
            uint32_t cv = inst->num_ops >= 1 ? inst->ops[0] : ~0u;
            const struct d12_value *cval =
               cv < mod->num_values ? &mod->values[cv] : NULL;
            mesa_loge("dxil: call to non-dx.op function: callee value %u "
                      "kind %d name '%s' (module has %u values)",
                      cv, cval ? (int)cval->kind : -1,
                      cval && cval->name ? cval->name : "(none)",
                      mod->num_values);
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

      case D12_FUNC_INST_CMP:
      case D12_FUNC_INST_CMP2:
         translate_cmp(&ctx, inst);
         break;

      case D12_FUNC_INST_EXTRACTVAL:
         /* Struct-returning dx.op results are vectors in NIR, so an
          * extractvalue of member N is channel N.
          */
         if (inst->num_ops >= 1 && inst->num_extra >= 1) {
            nir_def *agg = get_def(&ctx, inst->ops[0]);
            unsigned idx = (unsigned)inst->extra[0];
            if (idx < agg->num_components) {
               def_result(&ctx, inst, nir_channel(&b, agg, idx));
            } else {
               trans_fail(&ctx, "extractvalue index past aggregate");
            }
         } else {
            trans_fail(&ctx, "malformed extractvalue");
         }
         break;

      case D12_FUNC_INST_SELECT:
      case D12_FUNC_INST_VSELECT:
         translate_select(&ctx, inst);
         break;

      case D12_FUNC_INST_LOAD:
         /* SM 6.x reaches a resource by loading the global that stands
          * for it and converting the result with createHandleForLib. The
          * load itself carries no data: its value IS the resource, so it
          * resolves to that resource's binding slot exactly as
          * createHandle does. A load of anything else is real memory
          * traffic we do not implement, and says so.
          */
         if (inst->num_ops >= 1) {
            unsigned slot = resource_slot_for_global(&ctx, inst->ops[0]);
            if (slot != ~0u) {
               def_result(&ctx, inst, nir_imm_int(&b, slot));
               break;
            }
         }
         trans_fail(&ctx, "load from memory (groupshared or local) "
                          "unsupported");
         break;

      case D12_FUNC_INST_PHI:
         /* Read the register its predecessors wrote. */
         if (inst->result_value != ~0u && ctx.phi_reg[inst->result_value]) {
            def_result(&ctx, inst,
                       nir_load_reg(&b, ctx.phi_reg[inst->result_value]));
         } else {
            trans_fail(&ctx, "phi outside a multi-block function");
         }
         break;

      case D12_FUNC_INST_BR:
         translate_branch(&ctx, entry, inst);
         break;

      case D12_FUNC_INST_RET:
      case D12_FUNC_INST_UNREACHABLE:
         /* In an unstructured function EVERY block needs a terminator,
          * including the last: the structurizer walks successors and
          * follows a null pointer off a block that has none. A return is
          * a branch to the function's end block, not an absence of one.
          */
         if (num_blocks > 1)
            nir_goto(&b, b.impl->end_block);
         break;

      case D12_FUNC_DECLAREBLOCKS:
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
   free(ctx.phi_reg);

   if (ctx.error) {
      *error = ctx.error;
      ralloc_free(b.shader);
      return NULL;
   }

   /*
    * Turn the unstructured CFG into structured NIR, then turn the phi
    * registers back into real SSA phis. Both passes are Mesa's own; the
    * translator's job was only to present the CFG in the shape they
    * expect.
    */
   if (num_blocks > 1) {
      nir_lower_goto_ifs(b.shader);
      nir_lower_reg_intrinsics_to_ssa(b.shader);
   }

   nir_validate_shader(b.shader, "after DXIL translation");
   return b.shader;
}

static bool
lower_root(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_root_agx)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *rep = nir_load_preamble(b, 1, 64, .base = D12_UNIFORM_ROOT);
   nir_def_replace(&intr->def, rep);
   return true;
}

bool
d12_dxil_lower_root(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_root,
                                     nir_metadata_control_flow, NULL);
}
