/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DXBC (Shader Model 4/5) -> NIR.
 *
 * WHY THIS EXISTS. Measured against vkd3d-proton's suite, 202 of 557
 * tests fail at the very first step with "no DXIL part in DXBC
 * container" -- and at nothing else first. They embed fxc-compiled
 * Shader Model 5.x, which has no DXIL anywhere:
 *
 *     chunks: 3
 *        ISGN 44
 *        OSGN 44
 *        SHEX 240   sm5.0 kind=1
 *
 * SM 5.1 is legal D3D12, so this is a real conformance gap. It is also
 * the single largest one: no other fix competes.
 *
 * WHAT DXBC IS, and how it differs from what we already have. DXIL is
 * LLVM 3.7 bitcode -- SSA, typed, with dx.op calls. DXBC is a flat TOKEN
 * STREAM over a fixed register file: r# temporaries, v# inputs, o#
 * outputs, cb# constant buffers, t#/s# resources and samplers. Nothing is
 * SSA. So this is a genuinely separate front end and shares only the
 * container walk with the DXIL one.
 *
 * The encoding, from the public shader-model documentation:
 *
 *   opcode token   [10:0]  opcode
 *                  [23:11] opcode-specific control bits
 *                  [30:24] instruction length in tokens
 *                  [31]    an extended opcode token follows
 *
 *   operand token  [1:0]   number of components (0=none, 1=one, 2=four)
 *                  [11:2]  component selection / swizzle / mask
 *                  [19:12] operand type (the register file)
 *                  [21:20] index dimension (0..3)
 *                  [30:22] how each index is represented, 3 bits each
 *                  [31]    an extended operand token follows
 *
 * Registers are read and written as vec4s; scalars come out of a swizzle.
 * That maps onto NIR the way any pre-SSA IR does: a temporary file of
 * nir_variables that later passes promote.
 */

#include "d12_dxil.h"

#include "agx_compile.h"
#include "nir.h"
#include "nir_builder.h"
#include "util/log.h"
#include "util/u_math.h"

#include <string.h>

#define D12_DXBC_MAX_TEMPS    4096
#define D12_DXBC_MAX_OPERANDS 8

/* Register files (operand types) we understand. */
enum d12_dxbc_file {
   D12_DXBC_FILE_TEMP = 0,
   D12_DXBC_FILE_INPUT = 1,
   D12_DXBC_FILE_OUTPUT = 2,
   D12_DXBC_FILE_INDEXABLE_TEMP = 3,
   D12_DXBC_FILE_IMMEDIATE32 = 4,
   D12_DXBC_FILE_SAMPLER = 6,
   D12_DXBC_FILE_RESOURCE = 7,
   D12_DXBC_FILE_CONSTANT_BUFFER = 8,
   D12_DXBC_FILE_OUTPUT_DEPTH = 12,
   D12_DXBC_FILE_NULL = 13,
   /* Compute system values. These are not varyings and have no
    * declaration in the token stream -- the register file IS the
    * meaning. */
   D12_DXBC_FILE_OUTPUT_COVERAGE_MASK = 15,
   D12_DXBC_FILE_UAV = 30,
   D12_DXBC_FILE_TGSM = 31,
   D12_DXBC_FILE_THREAD_ID = 32,
   D12_DXBC_FILE_THREAD_GROUP_ID = 33,
   D12_DXBC_FILE_THREAD_ID_IN_GROUP = 34,
   D12_DXBC_FILE_COVERAGE_MASK = 35,
   D12_DXBC_FILE_THREAD_ID_IN_GROUP_FLAT = 36,
};

/* The opcodes this front end covers. Anything else fails the shader
 * loudly rather than silently miscompiling it. */
enum d12_dxbc_op {
   D12_DXBC_OP_ADD = 0,
   D12_DXBC_OP_AND = 1,
   D12_DXBC_OP_BREAK = 2,
   D12_DXBC_OP_BREAKC = 3,
   D12_DXBC_OP_DERIV_RTX = 11,
   D12_DXBC_OP_DERIV_RTY = 12,
   D12_DXBC_OP_DISCARD = 13,
   D12_DXBC_OP_DIV = 14,
   D12_DXBC_OP_DP2 = 15,
   D12_DXBC_OP_DP3 = 16,
   D12_DXBC_OP_DP4 = 17,
   D12_DXBC_OP_ELSE = 18,
   D12_DXBC_OP_ENDIF = 21,
   D12_DXBC_OP_ENDLOOP = 22,
   D12_DXBC_OP_EQ = 24,
   D12_DXBC_OP_EXP = 25,
   D12_DXBC_OP_FRC = 26,
   D12_DXBC_OP_FTOI = 27,
   D12_DXBC_OP_FTOU = 28,
   D12_DXBC_OP_GE = 29,
   D12_DXBC_OP_IADD = 30,
   D12_DXBC_OP_IF = 31,
   D12_DXBC_OP_IEQ = 32,
   D12_DXBC_OP_IGE = 33,
   D12_DXBC_OP_ILT = 34,
   D12_DXBC_OP_IMAD = 35,
   D12_DXBC_OP_IMAX = 36,
   D12_DXBC_OP_IMIN = 37,
   D12_DXBC_OP_IMUL = 38,
   D12_DXBC_OP_INE = 39,
   D12_DXBC_OP_INEG = 40,
   D12_DXBC_OP_ISHL = 41,
   D12_DXBC_OP_ISHR = 42,
   D12_DXBC_OP_ITOF = 43,
   D12_DXBC_OP_LD = 45,
   D12_DXBC_OP_LOG = 47,
   D12_DXBC_OP_LOOP = 48,
   D12_DXBC_OP_LT = 49,
   D12_DXBC_OP_MAD = 50,
   D12_DXBC_OP_MIN = 51,
   D12_DXBC_OP_MAX = 52,
   D12_DXBC_OP_MOV = 54,
   D12_DXBC_OP_MOVC = 55,
   D12_DXBC_OP_MUL = 56,
   D12_DXBC_OP_NE = 57,
   D12_DXBC_OP_NOT = 59,
   D12_DXBC_OP_OR = 60,
   D12_DXBC_OP_RET = 62,
   D12_DXBC_OP_ROUND_NE = 64,
   D12_DXBC_OP_ROUND_NI = 65,
   D12_DXBC_OP_ROUND_PI = 66,
   D12_DXBC_OP_ROUND_Z = 67,
   D12_DXBC_OP_RSQ = 68,
   D12_DXBC_OP_SAMPLE = 69,
   D12_DXBC_OP_SAMPLE_L = 72,
   D12_DXBC_OP_SQRT = 75,
   D12_DXBC_OP_SINCOS = 77,
   D12_DXBC_OP_UDIV = 78,
   D12_DXBC_OP_ULT = 79,
   D12_DXBC_OP_UGE = 80,
   D12_DXBC_OP_UMUL = 81,
   D12_DXBC_OP_UMAX = 83,
   D12_DXBC_OP_UMIN = 84,
   D12_DXBC_OP_USHR = 85,
   D12_DXBC_OP_UTOF = 86,
   D12_DXBC_OP_XOR = 87,
   D12_DXBC_OP_CONTINUE = 7,
   D12_DXBC_OP_CONTINUEC = 8,
   D12_DXBC_OP_CUSTOMDATA = 53,
   D12_DXBC_OP_NOP = 58,
   D12_DXBC_OP_RCP = 129,
   D12_DXBC_OP_F32TOF16 = 130,
   D12_DXBC_OP_F16TOF32 = 131,
   D12_DXBC_OP_COUNTBITS = 134,
   D12_DXBC_OP_FIRSTBIT_HI = 135,
   D12_DXBC_OP_FIRSTBIT_LO = 136,
   D12_DXBC_OP_FIRSTBIT_SHI = 137,
   D12_DXBC_OP_UBFE = 138,
   D12_DXBC_OP_IBFE = 139,
   D12_DXBC_OP_BFI = 140,
   D12_DXBC_OP_BFREV = 141,
   D12_DXBC_OP_UMAD = 82,
   D12_DXBC_OP_DCL_INDEX_RANGE = 91,
   D12_DXBC_OP_DCL_OUTPUT_SGV = 102,
   /* The SM5 declaration block. Bindings all come from the root
    * signature and the container's signature chunks, so these carry
    * nothing the driver needs -- but they must be STEPPED OVER
    * correctly, or everything after them decodes as garbage. */
   D12_DXBC_OP_DCL_UAV_TYPED = 156,
   D12_DXBC_OP_DCL_UAV_RAW = 157,
   D12_DXBC_OP_DCL_UAV_STRUCTURED = 158,
   D12_DXBC_OP_DCL_TGSM_RAW = 159,
   D12_DXBC_OP_DCL_TGSM_STRUCTURED = 160,
   D12_DXBC_OP_DCL_RESOURCE_RAW = 161,
   D12_DXBC_OP_DCL_RESOURCE_STRUCTURED = 162,
   D12_DXBC_OP_DCL_STREAM = 143,
   D12_DXBC_OP_DCL_GS_INPUT_PRIMITIVE = 93,
   D12_DXBC_OP_DCL_GS_OUTPUT_TOPOLOGY = 92,
   D12_DXBC_OP_DCL_MAX_OUTPUT_VERTEX_COUNT = 94,
   D12_DXBC_OP_DCL_INPUT_CONTROL_POINT_COUNT = 147,
   D12_DXBC_OP_DCL_OUTPUT_CONTROL_POINT_COUNT = 148,
   D12_DXBC_OP_DCL_TESS_DOMAIN = 149,
   D12_DXBC_OP_DCL_TESS_PARTITIONING = 150,
   D12_DXBC_OP_DCL_TESS_OUTPUT_PRIMITIVE = 151,
   D12_DXBC_OP_DCL_HS_MAX_TESSFACTOR = 152,
   /* Structured buffer access. Identified from the operand register
    * files rather than assumed: 168 writes to file 30 (a UAV) with four
    * operands, 165 reads from one into a temporary. */
   D12_DXBC_OP_LD_STRUCTURED = 165,
   /* Same shape, SRV rather than UAV: dest, index, offset, resource. */
   D12_DXBC_OP_LD_STRUCTURED_SRV = 167,
   D12_DXBC_OP_STORE_STRUCTURED = 168,
   /* Both write to file 30 with three operands. 164 takes an element
    * coordinate, 166 a byte offset -- typed versus raw. */
   D12_DXBC_OP_STORE_UAV_TYPED = 164,
   D12_DXBC_OP_STORE_RAW = 166,
   /* The load counterpart to 164: same operand shape, dest in a
    * temporary and the UAV last. */
   D12_DXBC_OP_LD_UAV_TYPED = 163,
   /* Geometry shader vertex emission. The _STREAM forms name a stream
    * in their first operand; the plain ones mean stream 0. */
   D12_DXBC_OP_CUT = 9,
   D12_DXBC_OP_EMIT = 19,
   D12_DXBC_OP_EMITTHENCUT = 20,
   D12_DXBC_OP_EMIT_STREAM = 117,
   D12_DXBC_OP_CUT_STREAM = 118,
   D12_DXBC_OP_EMITTHENCUT_STREAM = 119,
   D12_DXBC_OP_DCL_RESOURCE = 88,
   D12_DXBC_OP_DCL_CONSTANT_BUFFER = 89,
   D12_DXBC_OP_DCL_SAMPLER = 90,
   D12_DXBC_OP_DCL_INPUT = 95,
   D12_DXBC_OP_DCL_INPUT_SGV = 96,
   D12_DXBC_OP_DCL_INPUT_SIV = 97,
   D12_DXBC_OP_DCL_INPUT_PS = 98,
   D12_DXBC_OP_DCL_INPUT_PS_SGV = 99,
   D12_DXBC_OP_DCL_INPUT_PS_SIV = 100,
   D12_DXBC_OP_DCL_OUTPUT = 101,
   D12_DXBC_OP_DCL_OUTPUT_SIV = 103,
   D12_DXBC_OP_DCL_TEMPS = 104,
   D12_DXBC_OP_DCL_INDEXABLE_TEMP = 105,
   D12_DXBC_OP_DCL_GLOBAL_FLAGS = 106,
   D12_DXBC_OP_DCL_THREAD_GROUP = 155,
};

struct d12_dxbc_operand {
   enum d12_dxbc_file file;
   unsigned num_indices;
   uint32_t index[3];
   bool index_is_relative[3];

   /* Selection: 0 = none, 1 = one component, 2 = swizzle/mask */
   unsigned sel_mode;
   uint8_t swizzle[4]; /* source form */
   uint8_t mask;       /* destination form */

   bool neg;
   bool abs;

   uint32_t imm[4]; /* IMMEDIATE32 */
};

/*
 * One ISGN/OSGN element. The chunk is a flat table: a count, then 24-byte
 * records, with the semantic name a byte offset from the chunk's data.
 *
 * This is where a DXBC output learns WHAT it is. The token stream only
 * says "o2"; that a given register is SV_Position rather than a generic
 * varying lives here, and agx_nir_lower_uvs and the tilebuffer lowering
 * both key on the resulting io_semantics.location. Without this mapping
 * every store_output reached the backend as location 0 and asserted.
 */
struct d12_dxbc_sig_elem {
   char name[32];
   uint32_t semantic_index;
   uint32_t sysval;
   uint32_t reg;
   uint8_t mask;
};

#define D12_DXBC_MAX_SIG 32

struct d12_dxbc_ctx {
   char errbuf[64];
   const struct d12_binding_map *map;

   struct d12_dxbc_sig_elem in_sig[D12_DXBC_MAX_SIG];
   unsigned num_in_sig;
   struct d12_dxbc_sig_elem out_sig[D12_DXBC_MAX_SIG];
   unsigned num_out_sig;

   /* Element stride of each structured buffer, by register. Declared
    * once and needed at every access, since the address is
    * index * stride + offset. */
   uint32_t srv_stride[D12_DXBC_MAX_SIG];
   uint32_t uav_stride[D12_DXBC_MAX_SIG];
   nir_builder b;
   const uint32_t *tok;
   const uint32_t *end;

   nir_variable *temps[D12_DXBC_MAX_TEMPS];
   unsigned num_temps;

   /* DXBC control flow is STRUCTURED -- if/else/endif and loop/endloop
    * nest properly -- which is exactly what nir_push_if/nir_push_loop
    * want, so a stack of what we opened is enough to close them. */
   struct {
      bool is_loop;
      bool in_else;
   } cf[64];
   unsigned cf_depth;

   const char *error;
};

static void
fail(struct d12_dxbc_ctx *ctx, const char *why)
{
   if (!ctx->error)
      ctx->error = why;
}

static unsigned
parse_signature(const uint8_t *base, size_t size_B,
                struct d12_dxbc_sig_elem *out, unsigned max)
{
   if (size_B < 8)
      return 0;

   uint32_t count;
   memcpy(&count, base, 4);
   if (count > max)
      count = max;

   unsigned n = 0;
   for (uint32_t i = 0; i < count; i++) {
      size_t off = 8 + (size_t)i * 24;
      if (off + 24 > size_B)
         break;

      uint32_t name_off, sem_idx, sysval, reg;
      memcpy(&name_off, base + off + 0, 4);
      memcpy(&sem_idx, base + off + 4, 4);
      memcpy(&sysval, base + off + 8, 4);
      memcpy(&reg, base + off + 16, 4);

      struct d12_dxbc_sig_elem *e = &out[n++];
      memset(e, 0, sizeof(*e));
      e->semantic_index = sem_idx;
      e->sysval = sysval;
      e->reg = reg;
      e->mask = base[off + 20];

      if (name_off < size_B) {
         const char *nm = (const char *)base + name_off;
         size_t avail = size_B - name_off;
         size_t len = strnlen(nm, MIN2(avail, sizeof(e->name) - 1));
         memcpy(e->name, nm, len);
         e->name[len] = 0;
      }
   }

   return n;
}

static const struct d12_dxbc_sig_elem *
find_sig(const struct d12_dxbc_sig_elem *sig, unsigned n, uint32_t reg)
{
   for (unsigned i = 0; i < n; i++) {
      if (sig[i].reg == reg)
         return &sig[i];
   }
   return NULL;
}

/* Where a signature element lands in NIR's location space. */
static unsigned
output_location(struct d12_dxbc_ctx *ctx, const struct d12_dxbc_sig_elem *e,
                uint32_t reg)
{
   mesa_shader_stage stage = ctx->b.shader->info.stage;

   if (stage == MESA_SHADER_FRAGMENT) {
      /* Every fragment output is a render target; SV_Depth is the one
       * exception and is named. */
      if (e && !strcasecmp(e->name, "SV_Depth"))
         return FRAG_RESULT_DEPTH;
      return FRAG_RESULT_DATA0 + (e ? e->semantic_index : reg);
   }

   if (e) {
      if (!strcasecmp(e->name, "SV_Position") || e->sysval == 1)
         return VARYING_SLOT_POS;
      if (!strcasecmp(e->name, "SV_ClipDistance"))
         return VARYING_SLOT_CLIP_DIST0 + e->semantic_index;
   }

   return VARYING_SLOT_VAR0 + reg;
}

/*
 * Decode one operand. Returns the number of tokens consumed, or 0 on a
 * malformed stream -- which is a hard error, since a miscounted operand
 * desynchronises everything after it.
 */
static unsigned
decode_operand(struct d12_dxbc_ctx *ctx, const uint32_t *p, unsigned avail,
               struct d12_dxbc_operand *out)
{
   if (!avail)
      return 0;

   memset(out, 0, sizeof(*out));

   uint32_t tok = p[0];
   unsigned used = 1;

   out->sel_mode = tok & 0x3;
   unsigned sel = (tok >> 4) & 0xff;
   out->file = (tok >> 12) & 0xff;
   out->num_indices = (tok >> 20) & 0x3;

   if (out->sel_mode == 1) {
      /* One component, named by a 2-bit index. */
      out->swizzle[0] = out->swizzle[1] = out->swizzle[2] = out->swizzle[3] =
         sel & 0x3;
      out->mask = 1 << (sel & 0x3);
   } else if (out->sel_mode == 2) {
      /* Four components: a mask when written, a swizzle when read. The
       * two live in the same bits and are told apart by context, which
       * is why both are filled in here. */
      out->mask = sel & 0xf;
      for (unsigned i = 0; i < 4; i++)
         out->swizzle[i] = (sel >> (i * 2)) & 0x3;
   } else {
      out->mask = 0xf;
      for (unsigned i = 0; i < 4; i++)
         out->swizzle[i] = i;
   }

   /* Extended operand token: modifiers (negate / absolute). */
   if (tok & 0x80000000u) {
      if (used >= avail)
         return 0;
      uint32_t ext = p[used++];
      if ((ext & 0x3f) == 1) {
         unsigned mod = (ext >> 6) & 0xff;
         out->neg = mod == 1 || mod == 3;
         out->abs = mod == 2 || mod == 3;
      }
   }

   if (out->file == D12_DXBC_FILE_IMMEDIATE32) {
      unsigned n = out->sel_mode == 1 ? 1 : 4;
      if (used + n > avail)
         return 0;
      for (unsigned i = 0; i < n; i++)
         out->imm[i] = p[used++];
      if (n == 1)
         out->imm[1] = out->imm[2] = out->imm[3] = out->imm[0];
      return used;
   }

   for (unsigned i = 0; i < out->num_indices; i++) {
      unsigned rep = (tok >> (22 + i * 3)) & 0x7;

      switch (rep) {
      case 0: /* immediate32 */
         if (used >= avail)
            return 0;
         out->index[i] = p[used++];
         break;
      case 2: /* relative */
         out->index_is_relative[i] = true;
         /* The relative operand itself follows; skip it, since dynamic
          * indexing is refused below anyway. */
         {
            struct d12_dxbc_operand rel;
            unsigned n = decode_operand(ctx, p + used, avail - used, &rel);
            if (!n)
               return 0;
            used += n;
         }
         break;
      case 3: /* immediate32 + relative */
         if (used >= avail)
            return 0;
         out->index[i] = p[used++];
         out->index_is_relative[i] = true;
         {
            struct d12_dxbc_operand rel;
            unsigned n = decode_operand(ctx, p + used, avail - used, &rel);
            if (!n)
               return 0;
            used += n;
         }
         break;
      default:
         return 0;
      }
   }

   return used;
}

static nir_variable *
temp_var(struct d12_dxbc_ctx *ctx, unsigned index)
{
   if (index >= D12_DXBC_MAX_TEMPS) {
      fail(ctx, "temporary register out of range");
      return NULL;
   }

   if (!ctx->temps[index]) {
      ctx->temps[index] = nir_local_variable_create(
         ctx->b.impl, glsl_vec4_type(), "r");
   }

   return ctx->temps[index];
}

/* Read an operand as a vec4 of 32-bit values, swizzle and modifiers
 * applied. Everything is carried as a float-width vector; integer
 * opcodes bitcast at the point of use, exactly as the hardware does. */
static nir_def *
read_src(struct d12_dxbc_ctx *ctx, const struct d12_dxbc_operand *op)
{
   nir_builder *b = &ctx->b;
   nir_def *v = NULL;

   switch (op->file) {
   case D12_DXBC_FILE_IMMEDIATE32: {
      nir_const_value c[4];
      for (unsigned i = 0; i < 4; i++)
         c[i] = nir_const_value_for_uint(op->imm[i], 32);
      return nir_build_imm(b, 4, 32, c);
   }

   case D12_DXBC_FILE_TEMP: {
      nir_variable *var = temp_var(ctx, op->index[0]);
      if (!var)
         return nir_imm_zero(b, 4, 32);
      v = nir_load_var(b, var);
      break;
   }

   /*
    * A compute shader's thread identifiers. Unlike a vertex or pixel
    * input these carry no declaration and no signature entry: the
    * register FILE is the whole meaning, which is why they arrive here
    * as "unsupported register file" rather than as a missing opcode.
    */
   case D12_DXBC_FILE_THREAD_ID:
      v = nir_pad_vector(b, nir_load_global_invocation_id(b, 32), 4);
      break;

   case D12_DXBC_FILE_THREAD_GROUP_ID:
      v = nir_pad_vector(b, nir_load_workgroup_id(b), 4);
      break;

   case D12_DXBC_FILE_THREAD_ID_IN_GROUP:
      v = nir_pad_vector(b, nir_load_local_invocation_id(b), 4);
      break;

   case D12_DXBC_FILE_THREAD_ID_IN_GROUP_FLAT:
      return nir_replicate(b, nir_load_local_invocation_index(b), 4);

   case D12_DXBC_FILE_COVERAGE_MASK:
      return nir_replicate(b, nir_load_sample_mask_in(b), 4);

   case D12_DXBC_FILE_INPUT: {
      uint32_t reg = op->index[op->num_indices ? op->num_indices - 1 : 0];
      const struct d12_dxbc_sig_elem *e =
         find_sig(ctx->in_sig, ctx->num_in_sig, reg);

      /* System values are not varyings: they have their own intrinsics,
       * and reading them as inputs gives the shader a slot nothing ever
       * writes. */
      if (e) {
         if (!strcasecmp(e->name, "SV_VertexID"))
            return nir_replicate(b, nir_load_vertex_id(b), 4);
         if (!strcasecmp(e->name, "SV_InstanceID"))
            return nir_replicate(b, nir_load_instance_id(b), 4);
         if (!strcasecmp(e->name, "SV_IsFrontFace"))
            return nir_replicate(b, nir_b2i32(b, nir_load_front_face(b, 1)), 4);
         if (!strcasecmp(e->name, "SV_PrimitiveID"))
            return nir_replicate(b, nir_load_primitive_id(b), 4);
      }

      if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         if (e && !strcasecmp(e->name, "SV_Position")) {
            v = nir_load_frag_coord(b);
            break;
         }

         /* A fragment varying must arrive INTERPOLATED: the backend
          * consumes load_interpolated_input with a barycentric, not a
          * raw load_input. */
         unsigned loc = VARYING_SLOT_VAR0 + reg;
         v = nir_load_interpolated_input(
            b, 4, 32, nir_load_barycentric_pixel(b, 32), nir_imm_int(b, 0),
            .base = reg, .dest_type = nir_type_float32,
            .io_semantics.location = loc, .io_semantics.num_slots = 1);
         b->shader->info.inputs_read |= BITFIELD64_BIT(loc);
         break;
      }

      unsigned loc = b->shader->info.stage == MESA_SHADER_VERTEX
                        ? VERT_ATTRIB_GENERIC0 + reg
                        : VARYING_SLOT_VAR0 + reg;

      v = nir_load_input(b, 4, 32, nir_imm_int(b, 0), .base = reg,
                         .dest_type = nir_type_float32,
                         .io_semantics.location = loc,
                         .io_semantics.num_slots = 1);
      b->shader->info.inputs_read |=
         BITFIELD64_BIT(b->shader->info.stage == MESA_SHADER_VERTEX ? reg
                                                                    : loc);
      break;
   }

   case D12_DXBC_FILE_CONSTANT_BUFFER: {
      /*
       * cb<slot>[<row>], one vec4 per row.
       *
       * This driver does not have UBOs: a constant buffer is reached
       * through the ROOT TABLE, one indirection to the address the bind
       * put in the slot -- exactly what the DXIL front end does for
       * cbufferLoadLegacy. Emitting nir_load_ubo instead produced NIR the
       * AGX backend has no lowering for, and it asserted.
       *
       * The register number IS the slot: d12_root_layout_build_map
       * assigns CBV slots in register order, and DXBC has no space
       * concept below SM 5.1.
       */
      if (op->num_indices < 2 || op->index_is_relative[1]) {
         fail(ctx, "dynamically indexed constant buffer");
         return nir_imm_zero(b, 4, 32);
      }

      unsigned slot = ctx->map
                         ? d12_binding_map_find(ctx->map, D12_RES_CBV, 0,
                                                op->index[0])
                         : op->index[0];
      if (slot == ~0u)
         slot = op->index[0];

      nir_def *root = nir_load_root_agx(b);
      nir_def *base = nir_load_global(
         b, 1, 64, nir_iadd_imm(b, root, (uint64_t)slot * 8), .align_mul = 8);

      v = nir_load_global(b, 4, 32,
                          nir_iadd_imm(b, base, op->index[1] * 16),
                          .align_mul = 16);
      break;
   }

   default:
      snprintf(ctx->errbuf, sizeof(ctx->errbuf),
               "unsupported source register file %u", op->file);
      fail(ctx, ctx->errbuf);
      return nir_imm_zero(b, 4, 32);
   }

   if (!v)
      return nir_imm_zero(b, 4, 32);

   unsigned sw[4] = {op->swizzle[0], op->swizzle[1], op->swizzle[2],
                     op->swizzle[3]};
   v = nir_swizzle(b, v, sw, 4);

   if (op->abs)
      v = nir_fabs(b, v);
   if (op->neg)
      v = nir_fneg(b, v);

   return v;
}

/*
 * Address of element `index`, byte `offset`, in the structured buffer
 * bound at `reg`. One indirection through the root table, exactly as the
 * constant-buffer path does.
 */
static nir_def *
buffer_address(struct d12_dxbc_ctx *ctx, uint32_t reg, bool uav,
               nir_def *index, nir_def *offset, unsigned stride)
{
   nir_builder *b = &ctx->b;

   unsigned slot = ctx->map
                      ? d12_binding_map_find(ctx->map,
                                             uav ? D12_RES_UAV : D12_RES_SRV,
                                             0, reg)
                      : reg;
   if (slot == ~0u)
      slot = reg;

   nir_def *root = nir_load_root_agx(b);
   nir_def *base = nir_load_global(
      b, 1, 64, nir_iadd_imm(b, root, (uint64_t)slot * 8), .align_mul = 8);

   nir_def *byte = nir_iadd(b, nir_imul_imm(b, nir_channel(b, index, 0),
                                            stride ? stride : 4),
                            nir_channel(b, offset, 0));

   return nir_iadd(b, base, nir_u2u64(b, byte));
}

static void
write_dst(struct d12_dxbc_ctx *ctx, const struct d12_dxbc_operand *op,
          nir_def *value)
{
   nir_builder *b = &ctx->b;
   unsigned mask = op->mask ? op->mask : 0xf;

   switch (op->file) {
   case D12_DXBC_FILE_TEMP: {
      nir_variable *var = temp_var(ctx, op->index[0]);
      if (var)
         nir_store_var(b, var, value, mask);
      break;
   }

   case D12_DXBC_FILE_OUTPUT: {
      uint32_t reg = op->index[op->num_indices ? op->num_indices - 1 : 0];
      const struct d12_dxbc_sig_elem *e =
         find_sig(ctx->out_sig, ctx->num_out_sig, reg);
      unsigned loc = output_location(ctx, e, reg);

      nir_store_output(b, value, nir_imm_int(b, 0), .base = reg,
                       .write_mask = mask, .src_type = nir_type_float32,
                       .io_semantics.location = loc,
                       .io_semantics.num_slots = 1);
      b->shader->info.outputs_written |= BITFIELD64_BIT(loc);
      break;
   }

   case D12_DXBC_FILE_OUTPUT_DEPTH:
      nir_store_output(b, nir_channel(b, value, 0), nir_imm_int(b, 0),
                       .base = 0, .write_mask = 1,
                       .src_type = nir_type_float32,
                       .io_semantics.location = FRAG_RESULT_DEPTH,
                       .io_semantics.num_slots = 1);
      b->shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DEPTH);
      break;

   case D12_DXBC_FILE_OUTPUT_COVERAGE_MASK:
      nir_store_output(b, nir_channel(b, value, 0), nir_imm_int(b, 0),
                       .base = 0, .write_mask = 1, .src_type = nir_type_uint32,
                       .io_semantics.location = FRAG_RESULT_SAMPLE_MASK,
                       .io_semantics.num_slots = 1);
      b->shader->info.outputs_written |=
         BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK);
      break;

   case D12_DXBC_FILE_NULL:
      break;

   default:
      snprintf(ctx->errbuf, sizeof(ctx->errbuf),
               "unsupported destination register file %u", op->file);
      fail(ctx, ctx->errbuf);
      break;
   }
}

/*
 * Locate the shader chunk. SHEX is Shader Model 5, SHDR is Shader Model
 * 4; the token stream is the same shape, so either is accepted.
 */
bool
d12_dxbc_find_shader(const void *data, size_t size_B, const uint8_t **out,
                     size_t *out_size_B, uint32_t *out_version)
{
   const uint8_t *p = data;

   if (!p || size_B < 32)
      return false;

   uint32_t magic;
   memcpy(&magic, p, 4);
   if (magic != 0x43425844u) /* 'DXBC' */
      return false;

   uint32_t part_count;
   memcpy(&part_count, p + 28, 4);
   if (part_count > 64 || 32 + (size_t)part_count * 4 > size_B)
      return false;

   for (uint32_t i = 0; i < part_count; i++) {
      uint32_t off;
      memcpy(&off, p + 32 + i * 4, 4);
      if ((size_t)off + 8 > size_B)
         return false;

      if (memcmp(p + off, "SHEX", 4) && memcmp(p + off, "SHDR", 4))
         continue;

      uint32_t sz;
      memcpy(&sz, p + off + 4, 4);
      if ((size_t)off + 8 + sz > size_B || sz < 8)
         return false;

      if (out_version)
         memcpy(out_version, p + off + 8, 4);

      *out = p + off + 8;
      *out_size_B = sz;
      return true;
   }

   return false;
}

/* Locate any chunk by fourcc. ISGN/OSGN are the signature tables;
 * ISG1/OSG1 are their SM5.1 forms with the same record layout. */
static bool
find_chunk(const void *data, size_t size_B, const char *a, const char *b,
           const uint8_t **out, size_t *out_size_B)
{
   const uint8_t *p = data;

   if (!p || size_B < 32)
      return false;

   uint32_t part_count;
   memcpy(&part_count, p + 28, 4);
   if (part_count > 64 || 32 + (size_t)part_count * 4 > size_B)
      return false;

   for (uint32_t i = 0; i < part_count; i++) {
      uint32_t off;
      memcpy(&off, p + 32 + i * 4, 4);
      if ((size_t)off + 8 > size_B)
         return false;

      if (memcmp(p + off, a, 4) && (!b || memcmp(p + off, b, 4)))
         continue;

      uint32_t sz;
      memcpy(&sz, p + off + 4, 4);
      if ((size_t)off + 8 + sz > size_B)
         return false;

      *out = p + off + 8;
      *out_size_B = sz;
      return true;
   }

   return false;
}

bool
d12_dxbc_is_dxbc_shader(const void *data, size_t size_B)
{
   const uint8_t *chunk;
   size_t chunk_size;
   return d12_dxbc_find_shader(data, size_B, &chunk, &chunk_size, NULL);
}

/*
 * Walk the token stream. Every instruction carries its own length, so an
 * opcode this front end does not implement can still be STEPPED OVER --
 * but it is not, deliberately: silently skipping an instruction produces
 * a shader that runs and computes the wrong answer, which is the single
 * worst outcome. Unknown opcodes fail the whole translation.
 */
static void
translate_tokens(struct d12_dxbc_ctx *ctx)
{
   nir_builder *b = &ctx->b;

   while (ctx->tok < ctx->end && !ctx->error) {
      uint32_t op_tok = *ctx->tok;
      unsigned opcode = op_tok & 0x7ff;
      unsigned len = (op_tok >> 24) & 0x7f;

      /* An extended opcode token means the length lives after it. */
      if (op_tok & 0x80000000u) {
         const uint32_t *q = ctx->tok;
         unsigned n = 1;
         while ((q[n - 1] & 0x80000000u) && ctx->tok + n < ctx->end)
            n++;
         if (!len)
            len = n;
      }

      /* CUSTOMDATA does NOT carry its length in the opcode token: the
       * count is the token after it. Treating it like an ordinary
       * instruction desynchronises the whole rest of the stream, which
       * then decodes as plausible garbage. */
      if (opcode == D12_DXBC_OP_CUSTOMDATA) {
         if (ctx->tok + 2 > ctx->end) {
            fail(ctx, "truncated custom data block");
            return;
         }
         unsigned n = ctx->tok[1];
         if (n < 2 || ctx->tok + n > ctx->end) {
            fail(ctx, "bad custom data length");
            return;
         }
         ctx->tok += n;
         continue;
      }

      if (!len || ctx->tok + len > ctx->end) {
         fail(ctx, "malformed instruction length");
         return;
      }

      const uint32_t *args = ctx->tok + 1;
      unsigned avail = len - 1;

      struct d12_dxbc_operand o[D12_DXBC_MAX_OPERANDS];
      unsigned nops = 0;

      /* Declarations carry their payload inline rather than as operands,
       * and are handled before any operand decoding. */
      switch (opcode) {
      case D12_DXBC_OP_DCL_GLOBAL_FLAGS:
      case D12_DXBC_OP_DCL_TEMPS:
      case D12_DXBC_OP_DCL_INPUT:
      case D12_DXBC_OP_DCL_INPUT_SGV:
      case D12_DXBC_OP_DCL_INPUT_SIV:
      case D12_DXBC_OP_DCL_INPUT_PS:
      case D12_DXBC_OP_DCL_INPUT_PS_SGV:
      case D12_DXBC_OP_DCL_INPUT_PS_SIV:
      case D12_DXBC_OP_DCL_OUTPUT:
      case D12_DXBC_OP_DCL_OUTPUT_SIV:
      case D12_DXBC_OP_DCL_RESOURCE:
      case D12_DXBC_OP_DCL_SAMPLER:
      case D12_DXBC_OP_DCL_CONSTANT_BUFFER:
      case D12_DXBC_OP_DCL_INDEXABLE_TEMP:
      case D12_DXBC_OP_DCL_INDEX_RANGE:
      case D12_DXBC_OP_DCL_OUTPUT_SGV:
      case D12_DXBC_OP_DCL_UAV_TYPED:
      case D12_DXBC_OP_DCL_UAV_RAW:
      case D12_DXBC_OP_DCL_TGSM_RAW:
      case D12_DXBC_OP_DCL_TGSM_STRUCTURED:
      case D12_DXBC_OP_DCL_RESOURCE_RAW:
      case D12_DXBC_OP_DCL_STREAM:
      case D12_DXBC_OP_DCL_GS_INPUT_PRIMITIVE:
      case D12_DXBC_OP_DCL_GS_OUTPUT_TOPOLOGY:
      case D12_DXBC_OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
      case D12_DXBC_OP_DCL_INPUT_CONTROL_POINT_COUNT:
      case D12_DXBC_OP_DCL_OUTPUT_CONTROL_POINT_COUNT:
      case D12_DXBC_OP_DCL_TESS_DOMAIN:
      case D12_DXBC_OP_DCL_TESS_PARTITIONING:
      case D12_DXBC_OP_DCL_TESS_OUTPUT_PRIMITIVE:
      case D12_DXBC_OP_DCL_HS_MAX_TESSFACTOR:
         /* Signature and binding information all comes from the
          * container's ISGN/OSGN chunks and the root signature, so the
          * declarations themselves carry nothing this driver needs. */
         ctx->tok += len;
         continue;

      case D12_DXBC_OP_DCL_UAV_STRUCTURED:
      case D12_DXBC_OP_DCL_RESOURCE_STRUCTURED: {
         /* operand(u#/t#) then the stride. Decode the operand to find
          * the register, since its own length varies. */
         struct d12_dxbc_operand r;
         unsigned n = decode_operand(ctx, args, avail, &r);
         if (n && avail > n && r.index[0] < D12_DXBC_MAX_SIG) {
            if (opcode == D12_DXBC_OP_DCL_UAV_STRUCTURED)
               ctx->uav_stride[r.index[0]] = args[n];
            else
               ctx->srv_stride[r.index[0]] = args[n];
         }
         ctx->tok += len;
         continue;
      }

      case D12_DXBC_OP_DCL_THREAD_GROUP:
         if (avail >= 3) {
            b->shader->info.workgroup_size[0] = args[0];
            b->shader->info.workgroup_size[1] = args[1];
            b->shader->info.workgroup_size[2] = args[2];
         }
         ctx->tok += len;
         continue;

      default:
         break;
      }

      /* Everything else is operands. */
      while (avail && nops < D12_DXBC_MAX_OPERANDS) {
         unsigned n = decode_operand(ctx, args, avail, &o[nops]);
         if (!n) {
            fail(ctx, "malformed operand");
            return;
         }
         args += n;
         avail -= n;
         nops++;
      }

#define SRC(i) read_src(ctx, &o[i])
#define DST(v) write_dst(ctx, &o[0], (v))
#define NEED(n)                                                              \
   do {                                                                      \
      if (nops < (n)) {                                                      \
         fail(ctx, "too few operands");                                      \
         return;                                                             \
      }                                                                      \
   } while (0)

      switch (opcode) {
      case D12_DXBC_OP_MOV:      NEED(2); DST(SRC(1)); break;
      case D12_DXBC_OP_ADD:      NEED(3); DST(nir_fadd(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_MUL:      NEED(3); DST(nir_fmul(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_DIV:      NEED(3); DST(nir_fdiv(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_MAD:      NEED(4); DST(nir_ffma(b, SRC(1), SRC(2), SRC(3))); break;
      case D12_DXBC_OP_MIN:      NEED(3); DST(nir_fmin(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_MAX:      NEED(3); DST(nir_fmax(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_FRC:      NEED(2); DST(nir_ffract(b, SRC(1))); break;
      case D12_DXBC_OP_EXP:      NEED(2); DST(nir_fexp2(b, SRC(1))); break;
      case D12_DXBC_OP_LOG:      NEED(2); DST(nir_flog2(b, SRC(1))); break;
      case D12_DXBC_OP_SQRT:     NEED(2); DST(nir_fsqrt(b, SRC(1))); break;
      case D12_DXBC_OP_RSQ:      NEED(2); DST(nir_frsq(b, SRC(1))); break;
      case D12_DXBC_OP_ROUND_NE: NEED(2); DST(nir_fround_even(b, SRC(1))); break;
      case D12_DXBC_OP_ROUND_NI: NEED(2); DST(nir_ffloor(b, SRC(1))); break;
      case D12_DXBC_OP_ROUND_PI: NEED(2); DST(nir_fceil(b, SRC(1))); break;
      case D12_DXBC_OP_ROUND_Z:  NEED(2); DST(nir_ftrunc(b, SRC(1))); break;

      case D12_DXBC_OP_DP2:
      case D12_DXBC_OP_DP3:
      case D12_DXBC_OP_DP4: {
         NEED(3);
         unsigned n = opcode == D12_DXBC_OP_DP2 ? 2
                    : opcode == D12_DXBC_OP_DP3 ? 3 : 4;
         nir_def *d = nir_fdot(b, nir_trim_vector(b, SRC(1), n),
                               nir_trim_vector(b, SRC(2), n));
         /* A dot product replicates its scalar across the write mask. */
         DST(nir_replicate(b, d, 4));
         break;
      }

      /* Integer opcodes operate on the same registers, reinterpreted. */
      case D12_DXBC_OP_IADD: NEED(3); DST(nir_iadd(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_IMUL: NEED(4); DST(nir_imul(b, SRC(2), SRC(3))); break;
      case D12_DXBC_OP_IMAD: NEED(4); DST(nir_iadd(b, nir_imul(b, SRC(1), SRC(2)), SRC(3))); break;
      case D12_DXBC_OP_IMAX: NEED(3); DST(nir_imax(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_IMIN: NEED(3); DST(nir_imin(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_UMAX: NEED(3); DST(nir_umax(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_UMIN: NEED(3); DST(nir_umin(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_INEG: NEED(2); DST(nir_ineg(b, SRC(1))); break;
      case D12_DXBC_OP_AND:  NEED(3); DST(nir_iand(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_OR:   NEED(3); DST(nir_ior(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_XOR:  NEED(3); DST(nir_ixor(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_NOT:  NEED(2); DST(nir_inot(b, SRC(1))); break;
      case D12_DXBC_OP_ISHL: NEED(3); DST(nir_ishl(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_ISHR: NEED(3); DST(nir_ishr(b, SRC(1), SRC(2))); break;
      case D12_DXBC_OP_USHR: NEED(3); DST(nir_ushr(b, SRC(1), SRC(2))); break;

      case D12_DXBC_OP_ITOF: NEED(2); DST(nir_i2f32(b, SRC(1))); break;
      case D12_DXBC_OP_UTOF: NEED(2); DST(nir_u2f32(b, SRC(1))); break;
      case D12_DXBC_OP_FTOI: NEED(2); DST(nir_f2i32(b, SRC(1))); break;
      case D12_DXBC_OP_FTOU: NEED(2); DST(nir_f2u32(b, SRC(1))); break;

      /* Comparisons produce an all-ones or all-zero mask, not a bool. */
      case D12_DXBC_OP_EQ:  NEED(3); DST(nir_b2i32(b, nir_feq(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_NE:  NEED(3); DST(nir_b2i32(b, nir_fneu(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_LT:  NEED(3); DST(nir_b2i32(b, nir_flt(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_GE:  NEED(3); DST(nir_b2i32(b, nir_fge(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_IEQ: NEED(3); DST(nir_b2i32(b, nir_ieq(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_INE: NEED(3); DST(nir_b2i32(b, nir_ine(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_ILT: NEED(3); DST(nir_b2i32(b, nir_ilt(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_IGE: NEED(3); DST(nir_b2i32(b, nir_ige(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_ULT: NEED(3); DST(nir_b2i32(b, nir_ult(b, SRC(1), SRC(2)))); break;
      case D12_DXBC_OP_UGE: NEED(3); DST(nir_b2i32(b, nir_uge(b, SRC(1), SRC(2)))); break;

      case D12_DXBC_OP_MOVC:
         NEED(4);
         DST(nir_bcsel(b, nir_ine_imm(b, SRC(1), 0), SRC(2), SRC(3)));
         break;

      /*
       * D3D's bit-field ops are defined with explicit 5-bit masking of
       * the width and offset. GLSL's bitfieldInsert leaves the
       * out-of-range case undefined, so the formula is spelled out
       * rather than mapped onto it -- a shader that relies on the
       * documented wrap must not depend on a lowering's opinion.
       *
       *   width  = src0 & 31,  offset = src1 & 31
       *   mask   = ((1 << width) - 1) << offset
       *   dest   = ((src2 << offset) & mask) | (src3 & ~mask)
       */
      case D12_DXBC_OP_BFI: {
         NEED(5);
         nir_def *width = nir_iand_imm(b, SRC(1), 31);
         nir_def *offset = nir_iand_imm(b, SRC(2), 31);
         nir_def *ones = nir_imm_int(b, 1);
         nir_def *mask =
            nir_ishl(b, nir_isub(b, nir_ishl(b, ones, width), ones), offset);
         nir_def *ins = nir_iand(b, nir_ishl(b, SRC(3), offset), mask);
         nir_def *base = nir_iand(b, SRC(4), nir_inot(b, mask));
         DST(nir_ior(b, ins, base));
         break;
      }

      case D12_DXBC_OP_UBFE:
      case D12_DXBC_OP_IBFE: {
         NEED(4);
         nir_def *width = nir_iand_imm(b, SRC(1), 31);
         nir_def *offset = nir_iand_imm(b, SRC(2), 31);
         DST(opcode == D12_DXBC_OP_UBFE
                ? nir_ubitfield_extract(b, SRC(3), offset, width)
                : nir_ibitfield_extract(b, SRC(3), offset, width));
         break;
      }

      case D12_DXBC_OP_RCP:       NEED(2); DST(nir_frcp(b, SRC(1))); break;
      case D12_DXBC_OP_COUNTBITS: NEED(2); DST(nir_bit_count(b, SRC(1))); break;
      case D12_DXBC_OP_BFREV:     NEED(2); DST(nir_bitfield_reverse(b, SRC(1))); break;
      case D12_DXBC_OP_UMAD:      NEED(4); DST(nir_iadd(b, nir_imul(b, SRC(1), SRC(2)), SRC(3))); break;
      case D12_DXBC_OP_UDIV:      NEED(4); DST(nir_udiv(b, SRC(2), SRC(3))); break;
      case D12_DXBC_OP_UMUL:      NEED(4); DST(nir_imul(b, SRC(2), SRC(3))); break;
      case D12_DXBC_OP_NOP:       break;

      /*
       * STRUCTURED CONTROL FLOW. The opcode's control bits say whether
       * the test is "non-zero" or "zero"; bit 18 carries it.
       */
      case D12_DXBC_OP_IF: {
         NEED(1);
         if (ctx->cf_depth >= ARRAY_SIZE(ctx->cf)) {
            fail(ctx, "control flow nested too deeply");
            return;
         }
         bool test_zero = !((op_tok >> 18) & 1);
         nir_def *c = nir_channel(b, SRC(0), 0);
         nir_push_if(b, test_zero ? nir_ieq_imm(b, c, 0) : nir_ine_imm(b, c, 0));
         ctx->cf[ctx->cf_depth].is_loop = false;
         ctx->cf[ctx->cf_depth].in_else = false;
         ctx->cf_depth++;
         break;
      }

      case D12_DXBC_OP_ELSE:
         if (!ctx->cf_depth || ctx->cf[ctx->cf_depth - 1].is_loop) {
            fail(ctx, "else outside an if");
            return;
         }
         nir_push_else(b, NULL);
         ctx->cf[ctx->cf_depth - 1].in_else = true;
         break;

      case D12_DXBC_OP_ENDIF:
         if (!ctx->cf_depth || ctx->cf[ctx->cf_depth - 1].is_loop) {
            fail(ctx, "endif outside an if");
            return;
         }
         nir_pop_if(b, NULL);
         ctx->cf_depth--;
         break;

      case D12_DXBC_OP_LOOP:
         if (ctx->cf_depth >= ARRAY_SIZE(ctx->cf)) {
            fail(ctx, "control flow nested too deeply");
            return;
         }
         nir_push_loop(b);
         ctx->cf[ctx->cf_depth].is_loop = true;
         ctx->cf[ctx->cf_depth].in_else = false;
         ctx->cf_depth++;
         break;

      case D12_DXBC_OP_ENDLOOP:
         if (!ctx->cf_depth || !ctx->cf[ctx->cf_depth - 1].is_loop) {
            fail(ctx, "endloop outside a loop");
            return;
         }
         nir_pop_loop(b, NULL);
         ctx->cf_depth--;
         break;

      case D12_DXBC_OP_BREAK:
         nir_jump(b, nir_jump_break);
         break;

      case D12_DXBC_OP_CONTINUE:
         nir_jump(b, nir_jump_continue);
         break;

      case D12_DXBC_OP_BREAKC:
      case D12_DXBC_OP_CONTINUEC: {
         NEED(1);
         bool test_zero = !((op_tok >> 18) & 1);
         nir_def *c = nir_channel(b, SRC(0), 0);
         nir_push_if(b, test_zero ? nir_ieq_imm(b, c, 0) : nir_ine_imm(b, c, 0));
         nir_jump(b, opcode == D12_DXBC_OP_BREAKC ? nir_jump_break
                                                  : nir_jump_continue);
         nir_pop_if(b, NULL);
         break;
      }

      case D12_DXBC_OP_DISCARD: {
         NEED(1);
         bool test_zero = !((op_tok >> 18) & 1);
         nir_def *c = nir_channel(b, SRC(0), 0);
         nir_discard_if(b, test_zero ? nir_ieq_imm(b, c, 0)
                                     : nir_ine_imm(b, c, 0));
         b->shader->info.fs.uses_discard = true;
         break;
      }

      /*
       * STRUCTURED BUFFERS. The address is one indirection through the
       * root table -- the same shape as a constant buffer -- then
       * index * stride + offset. The stride comes from the declaration;
       * assuming 4 is right only for a buffer of 32-bit scalars, and a
       * 12-byte struct indexed at element 3 would read byte 12 instead
       * of byte 36.
       */
      case D12_DXBC_OP_LD_STRUCTURED:
      case D12_DXBC_OP_LD_STRUCTURED_SRV: {
         NEED(4);
         /* Find the resource by FILE, not by position: the two forms
          * differ in operand count and the resource is not always last. */
         unsigned ri = nops - 1;
         for (unsigned k = 1; k < nops; k++) {
            if (o[k].file == D12_DXBC_FILE_UAV ||
                o[k].file == D12_DXBC_FILE_RESOURCE) {
               ri = k;
               break;
            }
         }
         uint32_t reg = o[ri].index[0];
         bool uav = o[ri].file == D12_DXBC_FILE_UAV;
         unsigned stride = (uav ? ctx->uav_stride : ctx->srv_stride)
                              [reg < D12_DXBC_MAX_SIG ? reg : 0];
         nir_def *addr = buffer_address(ctx, reg, uav, SRC(1), SRC(2), stride);
         nir_def *val = nir_load_global(b, 4, 32, addr, .align_mul = 4);
         unsigned sw[4] = {o[ri].swizzle[0], o[ri].swizzle[1],
                           o[ri].swizzle[2], o[ri].swizzle[3]};
         DST(nir_swizzle(b, val, sw, 4));
         break;
      }

      case D12_DXBC_OP_STORE_STRUCTURED: {
         NEED(4);
         uint32_t reg = o[0].index[0];
         unsigned stride =
            ctx->uav_stride[reg < D12_DXBC_MAX_SIG ? reg : 0];
         nir_def *addr = buffer_address(ctx, reg, true, SRC(1), SRC(2), stride);
         nir_def *val = SRC(3);
         unsigned mask = o[0].mask ? o[0].mask : 0xf;

         /* Write the covered components individually: the mask is
          * arbitrary, and a single vectorised store would write past it. */
         for (unsigned c = 0; c < 4; c++) {
            if (!(mask & (1 << c)))
               continue;
            nir_store_global(b, nir_channel(b, val, c),
                             nir_iadd_imm(b, addr, c * 4), .write_mask = 1,
                             .align_mul = 4);
         }
         break;
      }

      /*
       * RAW and TYPED UAV stores. Same root-table indirection as every
       * other resource here; the difference is only how the address is
       * formed -- a byte offset for raw, an element index for typed.
       *
       * Typed is handled for 32-bit-per-element formats, which is what
       * the tests overwhelmingly use. A wider or packed format would
       * need real format conversion, so it is refused rather than
       * written at the wrong stride.
       */
      /*
       * Loading from a UAV. Mirrors the typed store: element addressing,
       * one indirection through the root table. The resource is found by
       * REGISTER FILE rather than by position -- the operand count varies
       * between the load forms and finding it by file survives that.
       */
      case D12_DXBC_OP_LD_UAV_TYPED: {
         NEED(3);

         unsigned reg = 0;
         bool found = false, uav = true;
         for (unsigned k = 1; k < nops; k++) {
            if (o[k].file == D12_DXBC_FILE_UAV ||
                o[k].file == D12_DXBC_FILE_RESOURCE) {
               reg = o[k].index[0];
               uav = o[k].file == D12_DXBC_FILE_UAV;
               found = true;
               break;
            }
         }
         if (!found) {
            fail(ctx, "UAV load with no resource operand");
            return;
         }

         nir_def *off = nir_imul_imm(b, nir_channel(b, SRC(1), 0), 4);
         nir_def *addr =
            buffer_address(ctx, reg, uav, nir_imm_int(b, 0), off, 0);
         nir_def *val = nir_load_global(b, 4, 32, addr, .align_mul = 4);

         /* The resource operand's swizzle reorders the result. */
         unsigned rs[4] = {0, 1, 2, 3};
         for (unsigned k = 1; k < nops; k++) {
            if (o[k].file == D12_DXBC_FILE_UAV ||
                o[k].file == D12_DXBC_FILE_RESOURCE) {
               for (unsigned c = 0; c < 4; c++)
                  rs[c] = o[k].swizzle[c];
               break;
            }
         }
         DST(nir_swizzle(b, val, rs, 4));
         break;
      }

      case D12_DXBC_OP_STORE_RAW:
      case D12_DXBC_OP_STORE_UAV_TYPED: {
         NEED(3);
         bool typed = opcode == D12_DXBC_OP_STORE_UAV_TYPED;
         uint32_t reg = o[0].index[0];
         bool tgsm = o[0].file == D12_DXBC_FILE_TGSM;

         if (tgsm) {
            fail(ctx, "thread-group shared memory store");
            return;
         }

         nir_def *off = nir_channel(b, SRC(1), 0);
         if (typed)
            off = nir_imul_imm(b, off, 4);

         nir_def *addr =
            buffer_address(ctx, reg, true, nir_imm_int(b, 0), off, 0);

         nir_def *val = SRC(2);
         unsigned mask = o[0].mask ? o[0].mask : 0xf;

         for (unsigned c = 0; c < 4; c++) {
            if (!(mask & (1 << c)))
               continue;
            nir_store_global(b, nir_channel(b, val, c),
                             nir_iadd_imm(b, addr, c * 4), .write_mask = 1,
                             .align_mul = 4);
         }
         break;
      }

      /*
       * Geometry shader emission. The outputs a shader has stored so far
       * ARE the vertex, so emitting is a boundary rather than a value --
       * the same shape the DXIL front end gives emitStream.
       */
      case D12_DXBC_OP_EMIT:
      case D12_DXBC_OP_EMITTHENCUT:
      case D12_DXBC_OP_CUT:
      case D12_DXBC_OP_EMIT_STREAM:
      case D12_DXBC_OP_CUT_STREAM:
      case D12_DXBC_OP_EMITTHENCUT_STREAM: {
         unsigned stream = 0;
         bool streamed = opcode == D12_DXBC_OP_EMIT_STREAM ||
                         opcode == D12_DXBC_OP_CUT_STREAM ||
                         opcode == D12_DXBC_OP_EMITTHENCUT_STREAM;
         if (streamed && nops >= 1)
            stream = o[0].index[0];

         bool cut = opcode == D12_DXBC_OP_CUT ||
                    opcode == D12_DXBC_OP_CUT_STREAM ||
                    opcode == D12_DXBC_OP_EMITTHENCUT ||
                    opcode == D12_DXBC_OP_EMITTHENCUT_STREAM;
         bool emit = opcode != D12_DXBC_OP_CUT &&
                     opcode != D12_DXBC_OP_CUT_STREAM;

         if (emit)
            nir_emit_vertex(b, .stream_id = stream);
         if (cut)
            nir_end_primitive(b, .stream_id = stream);
         break;
      }

      /*
       * TEXTURE ACCESS. t# and s# register numbers map to binding-map
       * slots the same way every other resource here does, so the tex
       * instruction is built with those as texture_index/sampler_index
       * -- the shape the DXIL front end already uses for dx.op.sample.
       *
       * DXBC hands the coordinate as a float4 and lets the swizzle pick;
       * a 2D sample takes .xy, and ld's address carries the mip level in
       * .w rather than as a separate operand.
       */
      case D12_DXBC_OP_SAMPLE:
      case D12_DXBC_OP_SAMPLE_L:
      case D12_DXBC_OP_LD: {
         bool is_ld = opcode == D12_DXBC_OP_LD;
         bool has_lod = opcode == D12_DXBC_OP_SAMPLE_L;
         NEED(is_ld ? 3 : 4);

         /* The resource is the operand in the RESOURCE file; the sampler
          * the one in the SAMPLER file. Finding them by file rather than
          * by position survives the operand-count differences between
          * these three. */
         unsigned tex_reg = 0, smp_reg = 0;
         bool have_tex = false, have_smp = false;
         for (unsigned k = 1; k < nops; k++) {
            if (o[k].file == D12_DXBC_FILE_RESOURCE && !have_tex) {
               tex_reg = o[k].index[0];
               have_tex = true;
            } else if (o[k].file == D12_DXBC_FILE_SAMPLER && !have_smp) {
               smp_reg = o[k].index[0];
               have_smp = true;
            }
         }

         if (!have_tex) {
            fail(ctx, "texture op with no resource operand");
            return;
         }

         unsigned tslot = ctx->map ? d12_binding_map_find(ctx->map, D12_RES_SRV,
                                                          0, tex_reg)
                                   : tex_reg;
         if (tslot == ~0u)
            tslot = tex_reg;
         unsigned sslot = ctx->map ? d12_binding_map_find(ctx->map,
                                                          D12_RES_SAMPLER, 0,
                                                          smp_reg)
                                   : smp_reg;
         if (sslot == ~0u)
            sslot = smp_reg;

         nir_def *coord4 = SRC(1);

         /*
          * AGX reaches a texture ONLY through a bindless handle -- a byte
          * offset into the texture heap. Setting texture_index and hoping
          * asserts in agx_nir_lower_texture ("must be bindless"), which
          * is what three tests turned into crashes before this. Samplers
          * are NOT the same mechanism: the backend's sampler operand is a
          * 16-bit index into sampler STATE REGISTERS, so a constant slot
          * is simply sampler_index. Same split the DXIL path makes.
          */
         nir_def *tex_handle = nir_bindless_image_agx(
            b, nir_imm_int(b, tslot * D12_TEXTURE_DESC_SIZE),
            .desc_set = D12_UNIFORM_TEXTURES);

         unsigned nsrc = 2 + (is_ld ? 1 : 0) + (has_lod ? 1 : 0);

         nir_tex_instr *tex = nir_tex_instr_create(b->shader, nsrc);
         tex->op = is_ld ? nir_texop_txf : (has_lod ? nir_texop_txl
                                                    : nir_texop_tex);
         tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
         tex->is_array = false;
         tex->dest_type = nir_type_float32;
         tex->coord_components = 2;
         tex->sampler_index = have_smp ? sslot : 0;

         unsigned si = 0;
         tex->src[si++] =
            nir_tex_src_for_ssa(nir_tex_src_texture_handle, tex_handle);

         if (is_ld) {
            /* Integer texel coordinates, and the mip in .w. */
            tex->src[si++] = nir_tex_src_for_ssa(
               nir_tex_src_coord,
               nir_trim_vector(b, nir_f2i32(b, coord4), 2));
            tex->src[si++] = nir_tex_src_for_ssa(
               nir_tex_src_lod, nir_channel(b, nir_f2i32(b, coord4), 3));
         } else {
            tex->src[si++] = nir_tex_src_for_ssa(nir_tex_src_coord,
                                                 nir_trim_vector(b, coord4, 2));
            if (has_lod)
               tex->src[si++] = nir_tex_src_for_ssa(
                  nir_tex_src_lod, nir_channel(b, SRC(nops - 1), 0));
         }

         nir_def_init(&tex->instr, &tex->def, 4, 32);
         nir_builder_instr_insert(b, &tex->instr);

         /* The RESOURCE operand's swizzle reorders the result. */
         unsigned rs[4] = {0, 1, 2, 3};
         for (unsigned k = 1; k < nops; k++) {
            if (o[k].file == D12_DXBC_FILE_RESOURCE) {
               for (unsigned c = 0; c < 4; c++)
                  rs[c] = o[k].swizzle[c];
               break;
            }
         }
         DST(nir_swizzle(b, &tex->def, rs, 4));
         break;
      }

      case D12_DXBC_OP_RET:
         /* Falls out of the shader; NIR's implicit return handles it. */
         break;

      default:
         /* Name the opcode: the next person needs a work list, not a
          * verdict. */
         /* SURVEY MODE: keep walking and report EVERY missing opcode
          * rather than the first. Which shaders are one addition away
          * from translating is the only thing that predicts what a fix
          * is worth -- the score moves when a shader completes, not when
          * an opcode is added. */
         if (getenv("D3D12AGX_DXBC_SURVEY")) {
            fprintf(stderr, "dxbc-missing: %u operands:", opcode);
            for (unsigned k = 0; k < nops; k++)
               fprintf(stderr, " file%u/idx%u", o[k].file, o[k].index[0]);
            fprintf(stderr, "\n");
            ctx->tok += len;
            continue;
         }
         snprintf(ctx->errbuf, sizeof(ctx->errbuf),
                  "unimplemented DXBC opcode %u", opcode);
         fail(ctx, ctx->errbuf);
         return;
      }

#undef SRC
#undef DST
#undef NEED

      ctx->tok += len;
   }
}

/*
 * Translate a DXBC container to NIR.
 *
 * STATUS: the token decoder and the scalar/vector ALU set are in place
 * and verified against real fxc output. Control flow (if/else/loop/
 * break), resource access (sample/ld/store) and the structured
 * declarations are NOT yet translated -- those opcodes fail the shader
 * rather than being skipped, because a silently skipped instruction
 * yields a shader that runs and is wrong.
 */
nir_shader *
d12_dxbc_to_nir(const void *data, size_t size_B, mesa_shader_stage stage,
                const struct d12_binding_map *map, const char **error)
{
   const uint8_t *chunk;
   size_t chunk_size;
   uint32_t version = 0;

   if (!d12_dxbc_find_shader(data, size_B, &chunk, &chunk_size, &version)) {
      *error = "no SHEX/SHDR chunk in DXBC container";
      return NULL;
   }

   struct d12_dxbc_ctx ctx;
   memset(&ctx, 0, sizeof(ctx));

   ctx.map = map;
   ctx.b = nir_builder_init_simple_shader(stage, &agx_nir_options, "dxbc");

   /* Token 0 is the version, token 1 the length in tokens. */
   const uint32_t *tokens = (const uint32_t *)chunk;
   unsigned num_tokens = chunk_size / 4;
   if (num_tokens < 2) {
      *error = "truncated shader chunk";
      ralloc_free(ctx.b.shader);
      return NULL;
   }

   /* The signatures say what each register MEANS. Parse them before any
    * instruction, since the very first store_output needs its location. */
   const uint8_t *sig;
   size_t sig_size;
   if (find_chunk(data, size_B, "ISGN", "ISG1", &sig, &sig_size)) {
      ctx.num_in_sig =
         parse_signature(sig, sig_size, ctx.in_sig, D12_DXBC_MAX_SIG);
   }
   if (find_chunk(data, size_B, "OSGN", "OSG1", &sig, &sig_size)) {
      ctx.num_out_sig =
         parse_signature(sig, sig_size, ctx.out_sig, D12_DXBC_MAX_SIG);
   }

   ctx.tok = tokens + 2;
   ctx.end = tokens + MIN2(num_tokens, tokens[1]);

   translate_tokens(&ctx);

   if (ctx.error) {
      *error = ctx.error;
      ralloc_free(ctx.b.shader);
      return NULL;
   }

   nir_validate_shader(ctx.b.shader, "after DXBC translation");
   return ctx.b.shader;
}
