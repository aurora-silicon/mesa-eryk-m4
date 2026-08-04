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
   D12_DXOP_TEXTURE_LOAD = 66,
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
   D12_DXOP_CREATE_HANDLE_FROM_HEAP = 218,
   /* SM 6.6 8-bit packing. The numbering is easy to transpose and doing so
    * is not loud: unpack takes two arguments and pack takes five, so a
    * swap surfaces as "too few arguments" on one of them and as a silently
    * wrong result on the other. These values are the ones the corpus
    * actually carries -- dx.op.unpack4x8.* is 219, dx.op.pack4x8.* is 220.
    */
   D12_DXOP_UNPACK4X8 = 219,
   D12_DXOP_PACK4X8 = 220,
   D12_DXOP_MSAD = 50,
   D12_DXOP_SAMPLE = 60,
   D12_DXOP_SAMPLE_CMP = 64,
   D12_DXOP_SAMPLE_CMP_LEVEL_ZERO = 65,
   D12_DXOP_DERIV_COARSE_X = 83,
   D12_DXOP_DERIV_COARSE_Y = 84,
   D12_DXOP_DERIV_FINE_X = 85,
   D12_DXOP_DERIV_FINE_Y = 86,
   D12_DXOP_SAMPLE_BIAS = 61,
   D12_DXOP_SAMPLE_LEVEL = 62,
   D12_DXOP_SAMPLE_GRAD = 63,
   D12_DXOP_TEXTURE_STORE = 67,
   D12_DXOP_CHECK_ACCESS_FULLY_MAPPED = 71,
   D12_DXOP_GET_DIMENSIONS = 72,
   D12_DXOP_TEXTURE_GATHER = 73,
   D12_DXOP_DISCARD = 82,
   D12_DXOP_BARRIER = 80,
   D12_DXOP_SAMPLE_INDEX = 90,
   D12_DXOP_COVERAGE = 91,
   D12_DXOP_PRIMITIVE_ID = 108,
   D12_DXOP_VIEW_ID = 138,
   D12_DXOP_LEGACY_F32TOF16 = 130,
   D12_DXOP_LEGACY_F16TOF32 = 131,
   D12_DXOP_WAVE_ALL_BIT_COUNT = 135,
   D12_DXOP_CALCULATE_LOD = 81,
   D12_DXOP_QUAD_READ_LANE_AT = 122,
   D12_DXOP_QUAD_OP = 123,
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

/* pack4x8's PackMode and unpack4x8's UnpackMode operands. */
enum {
   D12_PACK_TRUNC = 0, D12_PACK_SINT = 1, D12_PACK_UINT = 2,
};

enum {
   D12_UNPACK_UNSIGNED = 0, D12_UNPACK_SIGNED = 1,
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

   /*
    * Groupshared globals and allocas, modelled as byte offsets into two
    * flat memories. A pointer value is (space, constant base) plus an
    * optional dynamic part in defs[] -- a GEP whose indices are all
    * constant never materializes an instruction, and the constant part
    * rides in the load/store's immediate base where the backend can fold
    * it.
    */
   /* value index -> the base slot of the RANGE a dynamic handle indexes
    * into, for stride/kind lookup (~0 = not a handle). A handle's def is
    * its slot number; for a constant handle the def alone suffices.
    */
   uint32_t *handle_base;

   /* SM 6.6 heap handles: the def is the HEAP INDEX, not a root slot.
    * kind/stride come from annotateHandle's DxilResourceProperties
    * rather than from a declared range.
    */
   uint8_t *handle_heap;      /* 0 none, 1 resource heap, 2 sampler heap */
   uint32_t *handle_kind_v;   /* decoded resource kind, per value */
   uint32_t *handle_stride_v; /* structured stride, per value */

   uint8_t *ptr_space;      /* value index -> D12_PTR_*, 0 = not a pointer */
   uint32_t *ptr_base;      /* value index -> constant byte offset */
   uint32_t *ptr_pointee;   /* value index -> type the pointer points at */
   uint32_t shared_size;    /* bytes of groupshared assigned so far */
   uint32_t scratch_size;   /* bytes of per-thread scratch assigned so far */

   const char *error;
};

#define D12_PTR_NONE    0
#define D12_PTR_SHARED  1
#define D12_PTR_SCRATCH 2

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
/*
 * Natural (LLVM datalayout) size and alignment of a type as laid out in
 * groupshared or scratch memory. Returns false for types that have no
 * memory layout here.
 */
static bool
type_size_align(const struct d12_module *mod, uint32_t type,
                uint32_t *out_size, uint32_t *out_align)
{
   if (type >= mod->num_types)
      return false;

   const struct d12_type *t = &mod->types[type];
   switch (t->kind) {
   case D12_TYPE_INTEGER:
      if (t->int_bits != 8 && t->int_bits != 16 && t->int_bits != 32 &&
          t->int_bits != 64)
         return false;
      *out_size = *out_align = t->int_bits / 8;
      return true;
   case D12_TYPE_HALF:
      *out_size = *out_align = 2;
      return true;
   case D12_TYPE_FLOAT:
      *out_size = *out_align = 4;
      return true;
   case D12_TYPE_DOUBLE:
      *out_size = *out_align = 8;
      return true;
   case D12_TYPE_ARRAY:
   case D12_TYPE_VECTOR: {
      uint32_t es, ea;
      if (!type_size_align(mod, t->seq.elem, &es, &ea))
         return false;
      *out_size = es * (uint32_t)t->seq.count;
      *out_align = ea;
      /* LLVM vectors align to their full size rounded up to a power of
       * two; arrays align to the element.
       */
      if (t->kind == D12_TYPE_VECTOR)
         *out_align = util_next_power_of_two(*out_size);
      return true;
   }
   case D12_TYPE_STRUCT_ANON:
   case D12_TYPE_STRUCT_NAMED: {
      uint32_t off = 0, align = 1;
      for (unsigned i = 0; i < t->strct.num_members; i++) {
         uint32_t ms, ma;
         if (!type_size_align(mod, t->strct.members[i], &ms, &ma))
            return false;
         off = ALIGN_POT(off, ma) + ms;
         align = MAX2(align, ma);
      }
      *out_size = ALIGN_POT(off, align);
      *out_align = align;
      return true;
   }
   default:
      return false;
   }
}

/* Byte offset of struct member `index`, by the same layout rule. */
static bool
struct_member_offset(const struct d12_module *mod, const struct d12_type *t,
                     unsigned index, uint32_t *out_off, uint32_t *out_type)
{
   if (index >= t->strct.num_members)
      return false;

   uint32_t off = 0;
   for (unsigned i = 0; i <= index; i++) {
      uint32_t ms, ma;
      if (!type_size_align(mod, t->strct.members[i], &ms, &ma))
         return false;
      off = ALIGN_POT(off, ma);
      if (i == index) {
         *out_off = off;
         *out_type = t->strct.members[i];
         return true;
      }
      off += ms;
   }
   return false;
}

/*
 * Resolve a value as a pointer, assigning a groupshared global its home on
 * first use. Assignment order is first-use order, which is deterministic
 * for a given module. Only the space/base/pointee tables are written --
 * nothing is emitted, so this is safe under any control flow.
 */
static uint8_t
pointer_for_value(struct trans_ctx *ctx, uint32_t id)
{
   if (id >= ctx->mod->num_values)
      return D12_PTR_NONE;
   if (ctx->ptr_space[id] != D12_PTR_NONE)
      return ctx->ptr_space[id];

   const struct d12_value *v = &ctx->mod->values[id];
   if (v->kind != D12_VALUE_GLOBAL ||
       v->addr_space != D12_ADDR_SPACE_GROUPSHARED)
      return D12_PTR_NONE;

   uint32_t size, align;
   if (!type_size_align(ctx->mod, v->type, &size, &align))
      return D12_PTR_NONE;

   ctx->shared_size = ALIGN_POT(ctx->shared_size, align);
   ctx->ptr_space[id] = D12_PTR_SHARED;
   ctx->ptr_base[id] = ctx->shared_size;
   /* A global's recorded type IS the pointee (see struct d12_value). */
   ctx->ptr_pointee[id] = v->type;
   ctx->shared_size += size;
   return D12_PTR_SHARED;
}

/* NIR component shape for a loaded/stored type. */
static bool
memory_value_shape(const struct d12_module *mod, uint32_t type,
                   unsigned *out_comps, unsigned *out_bits)
{
   if (type >= mod->num_types)
      return false;

   const struct d12_type *t = &mod->types[type];
   if (t->kind == D12_TYPE_VECTOR) {
      unsigned bits = type_bit_size(mod, t->seq.elem);
      if (!bits)
         return false;
      *out_comps = (unsigned)t->seq.count;
      *out_bits = bits;
      return true;
   }

   unsigned bits = type_bit_size(mod, type);
   if (!bits || bits == 1)
      return false;
   *out_comps = 1;
   *out_bits = bits;
   return true;
}

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

/* DXC fills unused trailing operands (offsets, clamp) with undef. */
static bool
operand_is_undef(struct trans_ctx *ctx, uint32_t value_index)
{
   return value_index < ctx->mod->num_values &&
          ctx->mod->values[value_index].kind == D12_VALUE_CONSTANT &&
          ctx->mod->values[value_index].is_undef;
}

static void
def_result(struct trans_ctx *ctx, const struct d12_inst *inst, nir_def *def)
{
   if (inst->result_value < ctx->mod->num_values)
      ctx->defs[inst->result_value] = def;
}

/*
 * A resolved resource handle. `slot` is the index def -- a root-table
 * slot for range handles, a raw heap index for SM 6.6 heap handles --
 * and kind/stride describe how an access through it is addressed: from
 * the declared range for the former, from annotateHandle's
 * DxilResourceProperties for the latter.
 */
struct handle_ref {
   nir_def *slot;
   bool heap;
   uint32_t kind;
   uint32_t stride;
};

static bool
handle_info(struct trans_ctx *ctx, uint32_t value_id,
            struct handle_ref *out)
{
   nir_def *def = get_def(ctx, value_id);
   if (!def)
      return false;

   memset(out, 0, sizeof(*out));
   out->slot = def;

   if (value_id < ctx->mod->num_values && ctx->handle_heap[value_id]) {
      out->heap = true;
      out->kind = ctx->handle_kind_v[value_id];
      out->stride = ctx->handle_stride_v[value_id];
      return true;
   }

   unsigned info_slot;
   if (nir_src_is_const(nir_src_for_ssa(def))) {
      info_slot = nir_src_as_uint(nir_src_for_ssa(def));
   } else if (value_id < ctx->mod->num_values &&
              ctx->handle_base[value_id] != ~0u) {
      info_slot = ctx->handle_base[value_id];
   } else {
      trans_fail(ctx, "handle is dynamic but names no resource range");
      return false;
   }

   if (info_slot >= D12_MAX_RESOURCE_BINDINGS) {
      trans_fail(ctx, "resource binding index out of range");
      return false;
   }

   out->kind = ctx->slot_kind[info_slot];
   out->stride = ctx->slot_stride[info_slot];
   return true;
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
resource_address(struct trans_ctx *ctx, const struct handle_ref *h,
                 nir_def *coord0, nir_def *coord1, bool raw_op)
{
   nir_builder *b = ctx->b;

   /* One indirection to the resource's base address. Range handles go
    * through the root table (8-byte entries); SM 6.6 heap handles index
    * the shader-visible heap's descriptors directly, whose first field
    * is the address. Both bases are placeholders lowered to reserved
    * uniforms after preprocessing; the slot may be a runtime value, and
    * constant folding collapses the multiply for the constant case.
    */
   nir_def *base;
   if (h->heap) {
      nir_def *heap = nir_load_descriptor_heap_agx(b);
      base = nir_load_global(
         b, 1, 64,
         nir_iadd(b, heap,
                  nir_imul_imm(b, nir_u2u64(b, h->slot),
                               D12_HEAP_DESC_SIZE)),
         .align_mul = 8);
   } else {
      nir_def *root = nir_load_root_agx(b);
      base = nir_load_global(
         b, 1, 64,
         nir_iadd(b, root, nir_imul_imm(b, nir_u2u64(b, h->slot), 8)),
         .align_mul = 8);
   }

   const uint32_t kind = h->kind;
   const uint32_t stride = h->stride;

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
       *
       * The load's width follows the CALL's declared result type: an
       * fp16 shader loads half-precision inputs, and producing 32-bit
       * defs for them makes every arithmetic use a bit-size mismatch.
       */
      unsigned bits = type_bit_size(ctx->mod, inst->result_type);
      if (bits != 16 && bits != 32)
         bits = 32;

      nir_def *val;

      if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         /* Fragment varyings must arrive interpolated: the AGX backend
          * consumes load_interpolated_input with a barycentric source,
          * not a raw load_input.
          */
         unsigned location = VARYING_SLOT_VAR0 + sig_id;
         nir_def *bary = nir_load_barycentric_pixel(b, 32);

         val = nir_load_interpolated_input(
            b, 1, bits, bary, nir_imm_int(b, row),
            .base = (unsigned)sig_id, .component = (unsigned)col,
            .dest_type = nir_type_float | bits,
            .io_semantics.location = location,
            .io_semantics.num_slots = 1);

         b->shader->info.inputs_read |= BITFIELD64_BIT(location);
      } else {
         unsigned location = VERT_ATTRIB_GENERIC0 + sig_id;

         val = nir_load_input(b, 1, bits, nir_imm_int(b, row),
                              .base = (unsigned)sig_id,
                              .component = (unsigned)col,
                              .dest_type = nir_type_float | bits,
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

   case D12_DXOP_PACK4X8: {
      /*
       * pack4x8(packMode, x, y, z, w) -> i32
       *
       * The mode decides whether each component is merely truncated or
       * first saturated, and a saturating mode is not expressible as a
       * mask: pack_clamp_s8(200) is 127, not 0xc8 & 0xff. Reading the mode
       * as a constant is therefore mandatory rather than an optimisation,
       * and a dynamic one has to be refused instead of assumed -- guessing
       * Trunc would turn every out-of-range component into wraparound
       * garbage that still looks like a plausible colour.
       */
      NEED(5);

      uint64_t mode = 0;
      if (!const_uint(ctx, p[0], &mode)) {
         trans_fail(ctx, "non-constant pack4x8 mode");
         return false;
      }
      if (mode > D12_PACK_UINT) {
         trans_fail(ctx, "unhandled pack4x8 mode");
         return false;
      }

      nir_def *packed = nir_imm_int(b, 0);

      for (unsigned i = 0; i < 4; i++) {
         nir_def *c = get_def(ctx, p[1 + i]);

         if (mode == D12_PACK_TRUNC) {
            /* Mask first, then widen: the i16 overload's high bits are
             * discarded either way, so the widening cannot be signed or
             * unsigned wrongly.
             */
            c = nir_u2u32(b, nir_iand_imm(b, c, 0xff));
         } else {
            /* Both saturating modes take a SIGNED source -- UInt clamps
             * negatives to 0 rather than reinterpreting them as large
             * positives -- so the i16 overload sign-extends for both.
             */
            c = nir_i2i32(b, c);
            c = mode == D12_PACK_SINT
                   ? nir_imin_imm(b, nir_imax_imm(b, c, -128), 127)
                   : nir_imin_imm(b, nir_imax_imm(b, c, 0), 255);
            c = nir_iand_imm(b, c, 0xff);
         }

         packed = nir_ior(b, packed, nir_ishl_imm(b, c, 8 * i));
      }

      def_result(ctx, inst, packed);
      return true;
   }

   case D12_DXOP_UNPACK4X8: {
      /*
       * unpack4x8(unpackMode, packedVal) -> %dx.types.fouri32
       *
       * Struct-returning dx.op results are represented as NIR vectors and
       * picked apart by extractvalue, so a 4-component vector IS the
       * struct here and needs nothing further.
       *
       * The component width comes from the returned struct rather than
       * being assumed 32: there is an i16 overload, and it is the more
       * common one in the corpus. Handing back 32-bit components for it
       * would mismatch the bit size of every consumer.
       */
      NEED(2);

      uint64_t mode = 0;
      if (!const_uint(ctx, p[0], &mode)) {
         trans_fail(ctx, "non-constant unpack4x8 mode");
         return false;
      }
      if (mode > D12_UNPACK_SIGNED) {
         trans_fail(ctx, "unhandled unpack4x8 mode");
         return false;
      }

      unsigned bits = 32;
      if (inst->result_type < ctx->mod->num_types) {
         const struct d12_type *t = &ctx->mod->types[inst->result_type];
         if ((t->kind == D12_TYPE_STRUCT_ANON ||
              t->kind == D12_TYPE_STRUCT_NAMED) && t->strct.num_members)
            bits = type_bit_size(ctx->mod, t->strct.members[0]);
      }

      nir_def *packed = get_def(ctx, p[1]);
      nir_def *comps[4];

      for (unsigned i = 0; i < 4; i++) {
         if (mode == D12_UNPACK_SIGNED) {
            /* Shift byte i up to the sign bit and arithmetic-shift back:
             * sign extension without a dedicated 8-bit type.
             */
            nir_def *byte =
               nir_ishr_imm(b, nir_ishl_imm(b, packed, 24 - 8 * i), 24);
            comps[i] = nir_i2iN(b, byte, bits);
         } else {
            nir_def *byte =
               nir_iand_imm(b, nir_ushr_imm(b, packed, 8 * i), 0xff);
            comps[i] = nir_u2uN(b, byte, bits);
         }
      }

      def_result(ctx, inst, nir_vec(b, comps, 4));
      return true;
   }

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
         /* A resource array indexed by a computed value. The range's
          * registers map to consecutive slots, so the handle is
          * base_slot + (index - lower_bound); DXC's index operand is the
          * absolute shader register (it emits the add against the array
          * base itself).
          */
         const struct d12_resource_binding *rb = NULL;
         for (unsigned i = 0; i < ctx->mod->num_resources; i++) {
            const struct d12_resource_binding *cand =
               &ctx->mod->resources[i];
            if (cand->klass == klass && cand->id == range_id) {
               rb = cand;
               break;
            }
         }
         if (!rb) {
            trans_fail(ctx, "dynamic index into an undeclared range");
            return false;
         }

         unsigned count = d12_range_count(rb);

         unsigned base_slot = d12_binding_map_find(
            ctx->map, (unsigned)klass, rb->space, rb->lower_bound);
         if (base_slot == ~0u) {
            trans_fail(ctx, "shader uses a resource the root signature "
                            "does not bind");
            return false;
         }

         if (count) {
            unsigned last_slot = d12_binding_map_find(
               ctx->map, (unsigned)klass, rb->space,
               rb->lower_bound + count - 1);
            if (last_slot != base_slot + count - 1) {
               /* The stride/kind shortcut and the slot arithmetic both
                * assume the range is contiguous in slot space. The map
                * assigns ranges densely, so this only trips if that ever
                * changes -- and must trip rather than address the wrong
                * resource.
                */
               trans_fail(ctx, "resource range is not contiguous in "
                               "slots");
               return false;
            }
         }
         /* count == 0 is an UNBOUNDED range: the handle indexes off the
          * base without a compile-time bound, which is the entire point
          * of bindless. The runtime bound is the descriptor heap's, not
          * the shader's.
          */

         nir_def *idx = get_def(ctx, p[2]);
         if (!idx)
            return false;

         nir_def *slot_def = nir_iadd_imm(
            b, nir_iadd_imm(b, nir_u2u32(b, idx), -(int)rb->lower_bound),
            base_slot);

         ctx->handle_base[inst->result_value] = base_slot;
         def_result(ctx, inst, slot_def);
         return true;
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

      struct handle_ref h;
      if (!handle_info(ctx, p[0], &h))
         return false;

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

      nir_def *addr = resource_address(ctx, &h,
                                       get_def(ctx, p[2]),
                                       get_def(ctx, p[3]), false);
      def_result(ctx, inst,
                 nir_global_atomic(b, 32, addr, get_def(ctx, p[5]),
                                   .atomic_op = op));
      return true;
   }

   case D12_DXOP_ATOMIC_CMPXCHG: {
      /* (handle, offset0, offset1, offset2, compareValue, newValue) */
      NEED(6);

      struct handle_ref h;
      if (!handle_info(ctx, p[0], &h))
         return false;

      unsigned bits = type_bit_size(ctx->mod, inst->result_type);
      if (bits != 32 && bits != 64)
         bits = 32;

      nir_def *addr = resource_address(ctx, &h,
                                       get_def(ctx, p[1]),
                                       get_def(ctx, p[2]), false);
      def_result(ctx, inst,
                 nir_global_atomic_swap(b, bits, addr, get_def(ctx, p[4]),
                                        get_def(ctx, p[5]),
                                        .atomic_op = nir_atomic_op_cmpxchg));
      return true;
   }

   case D12_DXOP_SAMPLE:
   case D12_DXOP_SAMPLE_BIAS:
   case D12_DXOP_SAMPLE_LEVEL:
   case D12_DXOP_SAMPLE_GRAD:
   case D12_DXOP_SAMPLE_CMP:
   case D12_DXOP_SAMPLE_CMP_LEVEL_ZERO:
   case D12_DXOP_TEXTURE_GATHER: {
      /* (srv, sampler, c0..c3, offsets..., extras...). Only the 2D
       * layouts the texture heap can describe are accepted; everything
       * else refuses by name rather than sampling something that is not
       * there.
       *
       * AGX samples through a hardware sampler descriptor exactly as it
       * fetches through a texture descriptor: both handles are
       * base-uniform-plus-byte-offset pairs (agx_translate_bindless_handle),
       * so the sampler heap mirrors the texture heap at
       * D12_UNIFORM_SAMPLERS.
       */
      NEED(6);

      struct handle_ref th, sh;
      if (!handle_info(ctx, p[0], &th) || !handle_info(ctx, p[1], &sh))
         return false;

      nir_def *x = get_def(ctx, p[2]);
      nir_def *y = get_def(ctx, p[3]);
      if (!x || !y) {
         trans_fail(ctx, "malformed sample coordinates");
         return false;
      }

      /* Constant-zero or undef offsets are the common case and mean
       * "none". A real offset is representable (nir_tex_src_offset) but
       * has not been needed yet; refuse it loudly.
       */
      unsigned num_offsets = opcode == D12_DXOP_TEXTURE_GATHER ? 2 : 3;
      for (unsigned i = 0; i < num_offsets && 6 + i < np; i++) {
         uint64_t off = 0;
         if (operand_is_undef(ctx, p[6 + i]))
            continue;
         if (!const_uint(ctx, p[6 + i], &off) || off != 0) {
            trans_fail(ctx, "programmable sample offsets unsupported");
            return false;
         }
      }

      nir_texop op;
      unsigned num_srcs = 3;
      switch (opcode) {
      case D12_DXOP_SAMPLE: op = nir_texop_tex; break;
      case D12_DXOP_SAMPLE_BIAS: op = nir_texop_txb; num_srcs = 4; break;
      case D12_DXOP_SAMPLE_LEVEL: op = nir_texop_txl; num_srcs = 4; break;
      case D12_DXOP_SAMPLE_GRAD: op = nir_texop_txd; num_srcs = 5; break;
      case D12_DXOP_SAMPLE_CMP: op = nir_texop_tex; num_srcs = 4; break;
      case D12_DXOP_SAMPLE_CMP_LEVEL_ZERO:
         /* Comparison at exactly LOD 0 is txl with a zero lod. */
         op = nir_texop_txl;
         num_srcs = 5;
         break;
      default: op = nir_texop_tg4; break;
      }

      nir_def *tex_handle = nir_bindless_image_agx(
         b, nir_imul_imm(b, th.slot, D12_TEXTURE_DESC_SIZE),
         .desc_set = D12_UNIFORM_TEXTURES);
      nir_def *samp_handle = nir_bindless_image_agx(
         b, nir_imul_imm(b, sh.slot, D12_SAMPLER_DESC_SIZE),
         .desc_set = D12_UNIFORM_SAMPLERS);

      nir_tex_instr *tex = nir_tex_instr_create(b->shader, num_srcs);
      tex->op = op;
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = false;
      tex->dest_type = nir_type_float32;
      tex->coord_components = 2;

      tex->src[0] =
         nir_tex_src_for_ssa(nir_tex_src_texture_handle, tex_handle);
      tex->src[1] =
         nir_tex_src_for_ssa(nir_tex_src_sampler_handle, samp_handle);
      tex->src[2] =
         nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec2(b, x, y));

      if (opcode == D12_DXOP_SAMPLE_BIAS) {
         NEED(10);
         tex->src[3] =
            nir_tex_src_for_ssa(nir_tex_src_bias, get_def(ctx, p[9]));
      } else if (opcode == D12_DXOP_SAMPLE_LEVEL) {
         NEED(10);
         tex->src[3] =
            nir_tex_src_for_ssa(nir_tex_src_lod, get_def(ctx, p[9]));
      } else if (opcode == D12_DXOP_SAMPLE_GRAD) {
         NEED(15);
         tex->src[3] = nir_tex_src_for_ssa(
            nir_tex_src_ddx,
            nir_vec2(b, get_def(ctx, p[9]), get_def(ctx, p[10])));
         tex->src[4] = nir_tex_src_for_ssa(
            nir_tex_src_ddy,
            nir_vec2(b, get_def(ctx, p[12]), get_def(ctx, p[13])));
      } else if (opcode == D12_DXOP_SAMPLE_CMP ||
                 opcode == D12_DXOP_SAMPLE_CMP_LEVEL_ZERO) {
         NEED(10);
         tex->is_shadow = true;
         tex->src[3] = nir_tex_src_for_ssa(nir_tex_src_comparator,
                                           get_def(ctx, p[9]));
         if (opcode == D12_DXOP_SAMPLE_CMP_LEVEL_ZERO) {
            tex->src[4] = nir_tex_src_for_ssa(nir_tex_src_lod,
                                              nir_imm_int(b, 0));
         }
      } else if (opcode == D12_DXOP_TEXTURE_GATHER) {
         NEED(9);
         uint64_t channel = 0;
         const_uint(ctx, p[8], &channel);
         tex->component = (unsigned)channel & 3;
      }

      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(b, &tex->instr);
      def_result(ctx, inst, &tex->def);
      return true;
   }

   case D12_DXOP_CHECK_ACCESS_FULLY_MAPPED:
      /* (status) -> bool-as-i32. The status channel our buffer loads
       * return is already "fully mapped" (nothing bindable is sparse),
       * so this is the identity on it.
       */
      NEED(1);
      def_result(ctx, inst, get_def(ctx, p[0]));
      return true;

   case D12_DXOP_GET_DIMENSIONS: {
      /* (handle, mipLevel) -> (width, height, depth/layers, mip count),
       * consumed by extractvalue. txs and query_levels both lower to
       * reads of the descriptor in memory (agx_nir_lower_texture), which
       * is exactly where our heap keeps it.
       */
      NEED(2);

      struct handle_ref th;
      if (!handle_info(ctx, p[0], &th))
         return false;

      nir_def *handle = nir_bindless_image_agx(
         b, nir_imul_imm(b, th.slot, D12_TEXTURE_DESC_SIZE),
         .desc_set = D12_UNIFORM_TEXTURES);

      nir_def *lod = get_def(ctx, p[1]);
      if (!lod || operand_is_undef(ctx, p[1]))
         lod = nir_imm_int(b, 0);

      nir_tex_instr *txs = nir_tex_instr_create(b->shader, 2);
      txs->op = nir_texop_txs;
      txs->sampler_dim = GLSL_SAMPLER_DIM_2D;
      txs->is_array = false;
      txs->dest_type = nir_type_uint32;
      txs->src[0] =
         nir_tex_src_for_ssa(nir_tex_src_texture_handle, handle);
      txs->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, lod);
      nir_def_init(&txs->instr, &txs->def, 2, 32);
      nir_builder_instr_insert(b, &txs->instr);

      nir_tex_instr *lv = nir_tex_instr_create(b->shader, 1);
      lv->op = nir_texop_query_levels;
      lv->sampler_dim = GLSL_SAMPLER_DIM_2D;
      lv->is_array = false;
      lv->dest_type = nir_type_uint32;
      lv->src[0] = nir_tex_src_for_ssa(
         nir_tex_src_texture_handle,
         nir_bindless_image_agx(
            b, nir_imul_imm(b, th.slot, D12_TEXTURE_DESC_SIZE),
            .desc_set = D12_UNIFORM_TEXTURES));
      nir_def_init(&lv->instr, &lv->def, 1, 32);
      nir_builder_instr_insert(b, &lv->instr);

      def_result(ctx, inst,
                 nir_vec4(b, nir_channel(b, &txs->def, 0),
                          nir_channel(b, &txs->def, 1), nir_imm_int(b, 1),
                          &lv->def));
      return true;
   }

   case D12_DXOP_TEXTURE_STORE: {
      /* (uav, c0, c1, c2, v0..v3, mask). Compiles against the same heap
       * offsets as loads; a correct RUNTIME needs writable PBE
       * descriptors in a parallel heap, which does not exist yet -- the
       * texture read path has to land first.
       */
      NEED(9);

      struct handle_ref th;
      if (!handle_info(ctx, p[0], &th))
         return false;

      nir_def *cx = get_def(ctx, p[1]);
      nir_def *cy = get_def(ctx, p[2]);
      if (!cx || !cy) {
         trans_fail(ctx, "malformed texture store coordinates");
         return false;
      }

      nir_def *handle = nir_bindless_image_agx(
         b, nir_imul_imm(b, th.slot, D12_TEXTURE_DESC_SIZE),
         .desc_set = D12_UNIFORM_TEXTURES);

      nir_def *vals[4];
      for (unsigned i = 0; i < 4; i++) {
         vals[i] = get_def(ctx, p[4 + i]);
         if (!vals[i] || operand_is_undef(ctx, p[4 + i]))
            vals[i] = nir_imm_int(b, 0);
      }

      nir_def *zero = nir_imm_int(b, 0);
      nir_bindless_image_store(
         b, handle,
         nir_vec4(b, cx, cy, zero, zero), nir_imm_int(b, 0),
         nir_vec(b, vals, 4), nir_imm_int(b, 0),
         .image_dim = GLSL_SAMPLER_DIM_2D, .image_array = false,
         .format = PIPE_FORMAT_NONE, .access = ACCESS_NON_READABLE);
      return true;
   }

   case D12_DXOP_TEXTURE_LOAD: {
      /*
       * textureLoad(handle, mipLevel, x, y, z, ox, oy, oz)
       *
       * The hardware takes a 64-bit uniform BASE plus a 32-bit register
       * OFFSET and adds them, so the handle is a byte offset into the
       * texture heap and `desc_set` names the uniform holding its base --
       * not a Vulkan descriptor set.
       */
      NEED(4);

      struct handle_ref th;
      if (!handle_info(ctx, p[0], &th))
         return false;

      nir_def *x = get_def(ctx, p[2]);
      nir_def *y = get_def(ctx, p[3]);
      if (!x || !y) {
         trans_fail(ctx, "malformed texture coordinates");
         return false;
      }

      nir_def *lod = get_def(ctx, p[1]);
      if (getenv("D3D12AGX_DUMP_TEX")) {
         fprintf(stderr, "textureLoad: num_ops=%u lod=%s\n",
                 inst->num_ops, lod ? "yes" : "no");
      }
      nir_def *tex_handle = nir_bindless_image_agx(
         b, nir_imul_imm(b, th.slot, D12_TEXTURE_DESC_SIZE),
         .desc_set = D12_UNIFORM_TEXTURES);

      nir_tex_instr *tex = nir_tex_instr_create(b->shader, lod ? 3 : 2);
      tex->op = nir_texop_txf;
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = false;
      tex->dest_type = nir_type_float32;
      tex->coord_components = 2;

      tex->src[0] =
         nir_tex_src_for_ssa(nir_tex_src_texture_handle, tex_handle);
      tex->src[1] =
         nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec2(b, x, y));
      if (lod)
         tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_lod, lod);

      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(b, &tex->instr);

      def_result(ctx, inst, &tex->def);
      return true;
   }

   case D12_DXOP_DERIV_COARSE_X:
      NEED(1);
      def_result(ctx, inst, nir_ddx_coarse(b, get_def(ctx, p[0])));
      return true;
   case D12_DXOP_DERIV_COARSE_Y:
      NEED(1);
      def_result(ctx, inst, nir_ddy_coarse(b, get_def(ctx, p[0])));
      return true;
   case D12_DXOP_DERIV_FINE_X:
      NEED(1);
      def_result(ctx, inst, nir_ddx_fine(b, get_def(ctx, p[0])));
      return true;
   case D12_DXOP_DERIV_FINE_Y:
      NEED(1);
      def_result(ctx, inst, nir_ddy_fine(b, get_def(ctx, p[0])));
      return true;

   case D12_DXOP_MSAD: {
      /* dx.op.tertiary.i32(50): masked sum of absolute differences.
       * NIR has the exact operation.
       */
      NEED(3);
      def_result(ctx, inst,
                 nir_msad_4x8(b, get_def(ctx, p[0]), get_def(ctx, p[1]),
                              get_def(ctx, p[2])));
      return true;
   }

   case D12_DXOP_DISCARD: {
      /* (condition i1). demote rather than terminate: helpers stay alive
       * for derivatives, matching D3D's discard semantics. The AGX
       * backend consumes this only after agx_nir_lower_sample_mask.
       */
      NEED(1);
      nir_demote_if(b, to_bool(ctx, get_def(ctx, p[0])));
      return true;
   }

   case D12_DXOP_BARRIER: {
      /* (mode i32 const): bit 0 sync group, bit 1 UAV global fence,
       * bit 2 UAV group fence, bit 3 groupshared fence. From DXIL.rst's
       * BarrierMode, mirrored in DirectXShaderCompiler's DxilConstants.h.
       */
      NEED(1);
      uint64_t mode = 0;
      if (!const_uint(ctx, p[0], &mode)) {
         trans_fail(ctx, "non-constant barrier mode");
         return false;
      }

      nir_variable_mode modes = 0;
      if (mode & 0x2)
         modes |= nir_var_mem_global | nir_var_image;
      if (mode & 0x4)
         modes |= nir_var_mem_global | nir_var_image;
      if (mode & 0x8)
         modes |= nir_var_mem_shared;

      nir_barrier(b,
                  .execution_scope = (mode & 0x1) ? SCOPE_WORKGROUP
                                                  : SCOPE_NONE,
                  .memory_scope = (mode & 0x2) ? SCOPE_DEVICE
                                               : SCOPE_WORKGROUP,
                  .memory_semantics = NIR_MEMORY_ACQ_REL,
                  .memory_modes = modes);
      return true;
   }

   case D12_DXOP_SAMPLE_INDEX:
      def_result(ctx, inst, nir_load_sample_id(b));
      return true;

   case D12_DXOP_COVERAGE:
      def_result(ctx, inst, nir_load_sample_mask_in(b));
      return true;

   case D12_DXOP_PRIMITIVE_ID:
      def_result(ctx, inst, nir_load_primitive_id(b));
      return true;

   case D12_DXOP_VIEW_ID:
      /* View instancing is not exposed (CheckFeatureSupport reports no
       * tiers), so every view a shader can run in is view 0. This is the
       * true value, not a placeholder.
       */
      def_result(ctx, inst, nir_imm_int(b, 0));
      return true;

   case D12_DXOP_LEGACY_F32TOF16: {
      /* f32tof16 returns the half in the LOW 16 bits of a u32. Packing
       * against a zero high half gives exactly that.
       */
      NEED(1);
      def_result(ctx, inst,
                 nir_pack_half_2x16_split(b, get_def(ctx, p[0]),
                                          nir_imm_float(b, 0.0f)));
      return true;
   }

   case D12_DXOP_LEGACY_F16TOF32:
      NEED(1);
      def_result(ctx, inst,
                 nir_channel(b, nir_unpack_half_2x16(b, get_def(ctx, p[0])),
                             0));
      return true;

   case D12_DXOP_WAVE_ALL_BIT_COUNT:
      /* WaveActiveCountBits: how many active lanes hold true. */
      NEED(1);
      def_result(ctx, inst,
                 nir_bit_count(b, nir_ballot(b, 1, 32,
                                             to_bool(ctx, get_def(ctx, p[0])))));
      return true;

   case D12_DXOP_CALCULATE_LOD: {
      /* (srv, sampler, c0, c1, c2, clamped) -> the LOD the sample would
       * use. NIR's lod query returns (clamped, raw); the operand picks.
       */
      NEED(6);

      struct handle_ref th, sh;
      if (!handle_info(ctx, p[0], &th) || !handle_info(ctx, p[1], &sh))
         return false;

      nir_def *x = get_def(ctx, p[2]);
      nir_def *y = get_def(ctx, p[3]);
      if (!x || !y) {
         trans_fail(ctx, "malformed calculateLOD coordinates");
         return false;
      }

      uint64_t clamped = 1;
      const_uint(ctx, p[5], &clamped);

      nir_tex_instr *tex = nir_tex_instr_create(b->shader, 3);
      tex->op = nir_texop_lod;
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = false;
      tex->dest_type = nir_type_float32;
      tex->coord_components = 2;
      tex->src[0] = nir_tex_src_for_ssa(
         nir_tex_src_texture_handle,
         nir_bindless_image_agx(
            b, nir_imul_imm(b, th.slot, D12_TEXTURE_DESC_SIZE),
            .desc_set = D12_UNIFORM_TEXTURES));
      tex->src[1] = nir_tex_src_for_ssa(
         nir_tex_src_sampler_handle,
         nir_bindless_image_agx(
            b, nir_imul_imm(b, sh.slot, D12_SAMPLER_DESC_SIZE),
            .desc_set = D12_UNIFORM_SAMPLERS));
      tex->src[2] =
         nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec2(b, x, y));
      nir_def_init(&tex->instr, &tex->def, 2, 32);
      nir_builder_instr_insert(b, &tex->instr);

      def_result(ctx, inst,
                 nir_channel(b, &tex->def, clamped ? 0 : 1));
      return true;
   }

   case D12_DXOP_QUAD_READ_LANE_AT: {
      NEED(2);
      def_result(ctx, inst,
                 nir_quad_broadcast(b, get_def(ctx, p[0]),
                                    get_def(ctx, p[1])));
      return true;
   }

   case D12_DXOP_QUAD_OP: {
      /* (value, op): 0 across X, 1 across Y, 2 diagonal, from DXIL.rst's
       * QuadOpKind.
       */
      NEED(2);
      uint64_t qop = 0;
      if (!const_uint(ctx, p[1], &qop)) {
         trans_fail(ctx, "non-constant quad op kind");
         return false;
      }

      nir_def *v = get_def(ctx, p[0]);
      switch (qop) {
      case 0: def_result(ctx, inst, nir_quad_swap_horizontal(b, v)); return true;
      case 1: def_result(ctx, inst, nir_quad_swap_vertical(b, v)); return true;
      case 2: def_result(ctx, inst, nir_quad_swap_diagonal(b, v)); return true;
      default:
         trans_fail(ctx, "unknown quad op kind");
         return false;
      }
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
      if (p[0] < ctx->mod->num_values &&
          inst->result_value < ctx->mod->num_values) {
         ctx->handle_base[inst->result_value] = ctx->handle_base[p[0]];
         ctx->handle_heap[inst->result_value] = ctx->handle_heap[p[0]];
         ctx->handle_kind_v[inst->result_value] = ctx->handle_kind_v[p[0]];
         ctx->handle_stride_v[inst->result_value] =
            ctx->handle_stride_v[p[0]];
      }

      /* annotateHandle's second operand is a constant
       * DxilResourceProperties pair: dword0's low byte is the resource
       * kind, dword1 is the element stride for a structured buffer. For
       * a heap handle this is the ONLY place kind and stride exist --
       * there is no declared range behind it.
       */
      if (opcode == D12_DXOP_ANNOTATE_HANDLE && np >= 2 &&
          p[1] < ctx->mod->num_values &&
          inst->result_value < ctx->mod->num_values) {
         const struct d12_value *props = &ctx->mod->values[p[1]];
         if (props->kind == D12_VALUE_CONSTANT &&
             props->num_agg_elems >= 2) {
            uint64_t d0 = 0, d1 = 0;
            if (const_uint(ctx, props->agg_elems[0], &d0) &&
                const_uint(ctx, props->agg_elems[1], &d1)) {
               ctx->handle_kind_v[inst->result_value] = (uint32_t)d0 & 0xff;
               ctx->handle_stride_v[inst->result_value] = (uint32_t)d1;
            }
         }
      }

      def_result(ctx, inst, get_def(ctx, p[0]));
      return true;
   }

   case D12_DXOP_CREATE_HANDLE_FROM_HEAP: {
      /* (index, samplerHeap, nonUniformIndex): SM 6.6's
       * ResourceDescriptorHeap[i] / SamplerDescriptorHeap[i]. The handle
       * is the raw heap index; what it addresses is decided by the
       * annotateHandle that always follows.
       */
      NEED(2);

      nir_def *idx = get_def(ctx, p[0]);
      if (!idx)
         return false;

      uint64_t sampler_heap = 0;
      const_uint(ctx, p[1], &sampler_heap);

      if (inst->result_value < ctx->mod->num_values) {
         ctx->handle_heap[inst->result_value] = sampler_heap ? 2 : 1;
         /* Raw buffer until annotateHandle says otherwise. */
         ctx->handle_kind_v[inst->result_value] = D12_RES_KIND_RAW_BUFFER;
      }
      def_result(ctx, inst, nir_u2u32(b, idx));
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

      struct handle_ref h;
      if (!handle_info(ctx, p[0], &h))
         return false;

      /* Rows are 16 bytes; the row index scales, no second coordinate. */
      nir_def *offset = nir_imul_imm(b, get_def(ctx, p[1]), 16);
      nir_def *addr = resource_address(ctx, &h, offset,
                                       nir_imm_int(b, 0), true);

      def_result(ctx, inst, nir_load_global(b, 4, 32, addr, .align_mul = 16));
      return true;
   }

   case D12_DXOP_CBUFFER_LOAD: {
      /*
       * (handle, byteOffset, alignment) -> one scalar.
       *
       * The non-legacy form is byte-addressed, not row-addressed: the
       * offset is already in bytes and must NOT be scaled by 16 the way
       * cbufferLoadLegacy's register index is. Scaling it would read the
       * right buffer at sixteen times the right offset -- in bounds for a
       * large cbuffer, and therefore silent.
       *
       * The width likewise comes from the result type rather than being
       * assumed 32: DXC emits .i16 and .i64 overloads, and a 32-bit load
       * standing in for a 64-bit one drops half the value.
       */
      NEED(3);

      struct handle_ref h;
      if (!handle_info(ctx, p[0], &h))
         return false;

      unsigned bits = type_bit_size(ctx->mod, inst->result_type);

      /* DXIL states the alignment; trust it only when it is a sane power
       * of two, since align_mul is a promise to the backend and an
       * overstated one miscompiles rather than merely deoptimises.
       */
      uint64_t align = 0;
      if (!const_uint(ctx, p[2], &align) || !align || (align & (align - 1)) ||
          align > 16)
         align = bits / 8;

      nir_def *addr = resource_address(ctx, &h,
                                       get_def(ctx, p[1]),
                                       nir_imm_int(b, 0), true);

      def_result(ctx, inst,
                 nir_load_global(b, 1, bits, addr, .align_mul = align));
      return true;
   }

   case D12_DXOP_BUFFER_STORE:
   case D12_DXOP_RAW_BUFFER_STORE: {
      /* (handle, coord0, coord1, val0..val3, mask [, align]) */
      NEED(8);

      struct handle_ref h;
      if (!handle_info(ctx, p[0], &h))
         return false;

      uint64_t mask = 0;
      if (!const_uint(ctx, p[7], &mask))
         mask = 0xf;

      nir_def *addr = resource_address(ctx, &h,
                                       get_def(ctx, p[1]),
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

      struct handle_ref h;
      if (!handle_info(ctx, p[0], &h))
         return false;

      nir_def *addr = resource_address(ctx, &h,
                                       get_def(ctx, p[1]),
                                       get_def(ctx, p[2]),
                                       opcode == D12_DXOP_RAW_BUFFER_LOAD);

      /* DXIL buffer loads return {vec4, i32 status}; extractvalue picks
       * lanes, and index 4 is the tiled-resource residency status
       * (CheckAccessFullyMapped). Nothing we bind is sparse, so an
       * access that executes is by definition fully mapped: the status
       * is the constant "mapped", not a placeholder.
       */
      nir_def *data = nir_load_global(b, 4, 32, addr, .align_mul = 4);
      nir_def *mapped[5] = {
         nir_channel(b, data, 0), nir_channel(b, data, 1),
         nir_channel(b, data, 2), nir_channel(b, data, 3),
         nir_imm_int(b, ~0u),
      };
      def_result(ctx, inst, nir_vec(b, mapped, 5));
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

   /* get_def RECORDS a failure for an out-of-range index but still
    * returns a dummy def, so translation continues -- and indexing
    * values[] with the index it just rejected reads off the end of a
    * fixed array whose bound comes from unvalidated bitstream data.
    */
   bool is_float = inst->ops[0] < ctx->mod->num_values &&
                   x->bit_size >= 16 &&
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

   /* These are application shaders. The builder marks its shaders
    * internal, and the AGX backend treats internal shaders to a scratch
    * optimization pass pair that leaves small arrays as variable derefs
    * it then cannot emit.
    */
   b.shader->info.internal = false;

   struct trans_ctx ctx = {
      .mod = mod,
      .map = map,
      .func = entry,
      .b = &b,
      .num_defs = mod->num_values,
   };

   ctx.defs = calloc(mod->num_values, sizeof(*ctx.defs));
   ctx.phi_reg = calloc(mod->num_values, sizeof(*ctx.phi_reg));
   ctx.ptr_space = calloc(mod->num_values, sizeof(*ctx.ptr_space));
   ctx.ptr_base = calloc(mod->num_values, sizeof(*ctx.ptr_base));
   ctx.ptr_pointee = calloc(mod->num_values, sizeof(*ctx.ptr_pointee));
   ctx.handle_base = malloc(mod->num_values * sizeof(*ctx.handle_base));
   if (ctx.handle_base)
      memset(ctx.handle_base, 0xff, mod->num_values * sizeof(*ctx.handle_base));
   ctx.handle_heap = calloc(mod->num_values, sizeof(*ctx.handle_heap));
   ctx.handle_kind_v = calloc(mod->num_values, sizeof(*ctx.handle_kind_v));
   ctx.handle_stride_v = calloc(mod->num_values, sizeof(*ctx.handle_stride_v));
   if (!ctx.defs || !ctx.phi_reg || !ctx.ptr_space || !ctx.ptr_base ||
       !ctx.ptr_pointee || !ctx.handle_base || !ctx.handle_heap ||
       !ctx.handle_kind_v || !ctx.handle_stride_v) {
      *error = "out of memory";
      free(ctx.defs);
      free(ctx.phi_reg);
      free(ctx.ptr_space);
      free(ctx.ptr_base);
      free(ctx.ptr_pointee);
      free(ctx.handle_base);
      free(ctx.handle_heap);
      free(ctx.handle_kind_v);
      free(ctx.handle_stride_v);
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
      free(ctx.phi_reg);
      free(ctx.ptr_space);
      free(ctx.ptr_base);
      free(ctx.ptr_pointee);
      free(ctx.handle_base);
      free(ctx.handle_heap);
      free(ctx.handle_kind_v);
      free(ctx.handle_stride_v);
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
            break;
         }

         /* Report what the callee operand actually resolved to.
          * "some call failed" is not actionable, and the answer is
          * usually that the value is not a function at all.
          */
         uint32_t cv = inst->num_ops >= 1 ? inst->ops[0] : ~0u;
         const struct d12_value *cval =
            cv < mod->num_values ? &mod->values[cv] : NULL;

         /*
          * llvm.lifetime.start/end bracket the live range of an alloca so
          * LLVM can reuse the stack slot. They describe storage, not
          * computation, and there is nothing for a GPU backend to emit --
          * dropping them is the whole implementation.
          *
          * Skipping is safe for value numbering because these return
          * void, and the decoder allocates a value index from the
          * callee's return type rather than assuming every call defines
          * one. Emitting nothing therefore cannot shift the indices of
          * later instructions the way it would if a slot had been
          * reserved. Matching the "llvm.lifetime." prefix rather than the
          * two exact names also covers the pointer-suffixed spellings a
          * future DXC could emit.
          */
         if (cval && cval->kind == D12_VALUE_FUNCTION && cval->name &&
             !strncmp(cval->name, "llvm.lifetime.", 14))
            break;

         mesa_loge("dxil: call to non-dx.op function: callee value %u "
                   "kind %d name '%s' (module has %u values)",
                   cv, cval ? (int)cval->kind : -1,
                   cval && cval->name ? cval->name : "(none)",
                   mod->num_values);
         trans_fail(&ctx, "call to a non-dx.op function (inlining "
                          "required)");
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

      case D12_FUNC_INST_LOAD: {
         /* SM 6.x reaches a resource by loading the global that stands
          * for it and converting the result with createHandleForLib. The
          * load itself carries no data: its value IS the resource, so it
          * resolves to that resource's binding slot exactly as
          * createHandle does.
          */
         if (inst->num_ops >= 1) {
            unsigned slot = resource_slot_for_global(&ctx, inst->ops[0]);
            if (slot != ~0u) {
               def_result(&ctx, inst, nir_imm_int(&b, slot));
               break;
            }
         }

         /* Otherwise it is real memory traffic: groupshared or scratch. */
         uint8_t space = inst->num_ops >= 1
                            ? pointer_for_value(&ctx, inst->ops[0])
                            : D12_PTR_NONE;
         if (space == D12_PTR_NONE) {
            trans_fail(&ctx, "load from memory that is neither groupshared "
                             "nor local");
            break;
         }

         unsigned comps, bits;
         if (!memory_value_shape(mod, inst->result_type, &comps, &bits)) {
            trans_fail(&ctx, "loaded type has no memory shape");
            break;
         }

         uint32_t base = ctx.ptr_base[inst->ops[0]];
         nir_def *dyn = ctx.defs[inst->ops[0]];
         if (!dyn)
            dyn = nir_imm_int(&b, 0);

         nir_def *val;
         if (space == D12_PTR_SHARED) {
            val = nir_load_shared(&b, comps, bits, dyn, .base = base,
                                  .align_mul = bits / 8);
         } else {
            val = nir_load_scratch(&b, comps, bits,
                                   nir_iadd_imm(&b, dyn, base),
                                   .align_mul = bits / 8);
         }
         def_result(&ctx, inst, val);
         break;
      }

      case D12_FUNC_INST_STORE: {
         /* [ptr, value] */
         if (inst->num_ops < 2) {
            trans_fail(&ctx, "malformed store");
            break;
         }

         uint8_t space = pointer_for_value(&ctx, inst->ops[0]);
         if (space == D12_PTR_NONE) {
            trans_fail(&ctx, "store to memory that is neither groupshared "
                             "nor local");
            break;
         }

         nir_def *val = get_def(&ctx, inst->ops[1]);
         if (!val) {
            trans_fail(&ctx, "stored value is not available");
            break;
         }

         uint32_t base = ctx.ptr_base[inst->ops[0]];
         nir_def *dyn = ctx.defs[inst->ops[0]];
         if (!dyn)
            dyn = nir_imm_int(&b, 0);

         if (space == D12_PTR_SHARED) {
            nir_store_shared(&b, val, dyn, .base = base,
                             .write_mask = BITFIELD_MASK(val->num_components),
                             .align_mul = val->bit_size / 8);
         } else {
            nir_store_scratch(&b, val, nir_iadd_imm(&b, dyn, base),
                              .write_mask =
                                 BITFIELD_MASK(val->num_components),
                              .align_mul = val->bit_size / 8);
         }
         break;
      }

      case D12_FUNC_INST_ALLOCA: {
         /* LLVM 3.7's ALLOCA record names the ALLOCATED type directly
          * (BitcodeWriter pushes getAllocatedType), so the recorded type
          * is the object itself; carve it out of per-thread scratch. DXC
          * emits allocas only in the entry block with constant sizes, so
          * a running total is a correct allocator. Accept a pointer too,
          * in case a different writer recorded the result type instead.
          */
         if (inst->result_type >= mod->num_types) {
            trans_fail(&ctx, "alloca with no type");
            break;
         }

         uint32_t pointee =
            mod->types[inst->result_type].kind == D12_TYPE_POINTER
               ? mod->types[inst->result_type].ptr.pointee
               : inst->result_type;
         uint32_t size, align;
         if (!type_size_align(mod, pointee, &size, &align)) {
            trans_fail(&ctx, "alloca of a type with no memory layout");
            break;
         }

         ctx.scratch_size = ALIGN_POT(ctx.scratch_size, align);
         ctx.ptr_space[inst->result_value] = D12_PTR_SCRATCH;
         ctx.ptr_base[inst->result_value] = ctx.scratch_size;
         ctx.ptr_pointee[inst->result_value] = pointee;
         ctx.scratch_size += size;
         break;
      }

      case D12_FUNC_INST_GEP:
      case D12_FUNC_INST_GEP_OLD: {
         /* [ptr, idx...]: byte arithmetic over the pointee layout. The
          * result inherits the pointer's space; constant index chains
          * stay constant.
          */
         if (inst->num_ops < 2) {
            trans_fail(&ctx, "malformed getelementptr");
            break;
         }

         uint32_t ptr_id = inst->ops[0];
         uint8_t space = pointer_for_value(&ctx, ptr_id);
         if (space == D12_PTR_NONE) {
            trans_fail(&ctx, "getelementptr on a non-memory pointer");
            break;
         }

         uint32_t base = ctx.ptr_base[ptr_id];
         nir_def *dyn = ctx.defs[ptr_id];
         uint32_t cur = ctx.ptr_pointee[ptr_id];
         bool ok = true;

         for (unsigned i = 1; i < inst->num_ops && ok; i++) {
            /* Index 1 strides over the whole pointee; later indices step
             * into it.
             */
            uint32_t stride, walk_align, next = cur;

            if (i == 1) {
               ok = type_size_align(mod, cur, &stride, &walk_align);
            } else {
               const struct d12_type *t = &mod->types[cur];
               if (t->kind == D12_TYPE_ARRAY || t->kind == D12_TYPE_VECTOR) {
                  next = t->seq.elem;
                  ok = type_size_align(mod, next, &stride, &walk_align);
               } else if (t->kind == D12_TYPE_STRUCT_ANON ||
                          t->kind == D12_TYPE_STRUCT_NAMED) {
                  /* Struct indices must be constant in LLVM. */
                  uint64_t member;
                  uint32_t moff;
                  if (!const_uint(&ctx, inst->ops[i], &member) ||
                      !struct_member_offset(mod, t, (unsigned)member, &moff,
                                            &next)) {
                     ok = false;
                     break;
                  }
                  base += moff;
                  cur = next;
                  continue;
               } else {
                  ok = false;
                  break;
               }
            }
            if (!ok)
               break;

            uint64_t const_idx;
            if (const_uint(&ctx, inst->ops[i], &const_idx)) {
               base += (uint32_t)const_idx * stride;
            } else {
               nir_def *idx = get_def(&ctx, inst->ops[i]);
               if (!idx) {
                  ok = false;
                  break;
               }
               idx = nir_u2u32(&b, idx);
               nir_def *scaled = nir_imul_imm(&b, idx, stride);
               dyn = dyn ? nir_iadd(&b, dyn, scaled) : scaled;
            }
            cur = next;
         }

         if (!ok) {
            trans_fail(&ctx, "getelementptr walk failed");
            break;
         }

         ctx.ptr_space[inst->result_value] = space;
         ctx.ptr_base[inst->result_value] = base;
         ctx.ptr_pointee[inst->result_value] = cur;
         if (dyn)
            ctx.defs[inst->result_value] = dyn;
         break;
      }

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

      /* Varyings leave the shader as 32-bit: that is both what D3D
       * semantics say (interpolants are float unless the target format
       * narrows them) and what the AGX varying store requires. An fp16
       * shader's outputs are widened HERE, explicitly -- stamping the
       * 16-bit value float32 without converting is invalid NIR, and the
       * backend asserts on a 16-bit st_vary source.
       */
      nir_def *comps[4];
      for (unsigned c = 0; c < num_comps; c++) {
         nir_def *v = slot->comps[c];
         if (v && v->bit_size != 32)
            v = nir_f2f32(&b, v);
         comps[c] = v ? v : nir_undef(&b, 1, 32);
      }

      nir_store_output(&b, nir_vec(&b, comps, num_comps), nir_imm_int(&b, 0),
                       .base = slot->base,
                       .write_mask = slot->write_mask,
                       .src_type = nir_type_float32,
                       .io_semantics.location = slot->location,
                       .io_semantics.num_slots = 1);
   }

   /* Sizes discovered while translating: the launch words read shared
    * size from here, and the backend sizes its stack from scratch_size.
    */
   b.shader->info.shared_size = ctx.shared_size;
   b.shader->scratch_size = ctx.scratch_size;

   free(ctx.defs);
   free(ctx.phi_reg);
   free(ctx.ptr_space);
   free(ctx.ptr_base);
   free(ctx.ptr_pointee);
   free(ctx.handle_base);
   free(ctx.handle_heap);
   free(ctx.handle_kind_v);
   free(ctx.handle_stride_v);

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

/*
 * agx_nir_lower_uvs emits load_uvs_index_agx for indexed output stores
 * and leaves it "to be lowered by the driver": Honeykrisp reads the
 * index out of its root descriptor because linking decides the layout at
 * draw time. Ours is decided at compile time by the same
 * agx_nir_lower_uvs call, so the index is a constant from the layout it
 * just produced.
 */
static bool
lower_uvs_index(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const unsigned *slots = data;

   if (intr->intrinsic != nir_intrinsic_load_uvs_index_agx)
      return false;

   gl_varying_slot loc = nir_intrinsic_io_semantics(intr).location;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_intN_t(b, slots[loc], 16));
   return true;
}

bool
d12_dxil_lower_uvs_index(nir_shader *nir, const unsigned *slots)
{
   return nir_shader_intrinsics_pass(nir, lower_uvs_index,
                                     nir_metadata_control_flow,
                                     (void *)slots);
}

static bool
lower_root(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   unsigned base;
   if (intr->intrinsic == nir_intrinsic_load_root_agx)
      base = D12_UNIFORM_ROOT;
   else if (intr->intrinsic == nir_intrinsic_load_descriptor_heap_agx)
      base = D12_UNIFORM_HEAP;
   else
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *rep = nir_load_preamble(b, 1, 64, .base = base);
   nir_def_replace(&intr->def, rep);
   return true;
}

bool
d12_dxil_lower_root(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_root,
                                     nir_metadata_control_flow, NULL);
}
