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
   D12_DXBC_FILE_NULL = 13,
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

struct d12_dxbc_ctx {
   char errbuf[64];
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

   case D12_DXBC_FILE_INPUT: {
      v = nir_load_input(b, 4, 32, nir_imm_int(b, 0),
                         .base = op->index[op->num_indices ? op->num_indices - 1
                                                           : 0],
                         .dest_type = nir_type_float32,
                         .io_semantics.num_slots = 1);
      break;
   }

   case D12_DXBC_FILE_CONSTANT_BUFFER: {
      /* cb<slot>[<row>]: one vec4 per row. */
      if (op->num_indices < 2 || op->index_is_relative[1]) {
         fail(ctx, "dynamically indexed constant buffer");
         return nir_imm_zero(b, 4, 32);
      }
      v = nir_load_ubo(b, 4, 32, nir_imm_int(b, op->index[0]),
                       nir_imm_int(b, op->index[1] * 16),
                       .align_mul = 16, .range = ~0);
      break;
   }

   default:
      fail(ctx, "unsupported source register file");
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

   case D12_DXBC_FILE_OUTPUT:
      nir_store_output(b, value, nir_imm_int(b, 0),
                       .base = op->index[op->num_indices ? op->num_indices - 1
                                                         : 0],
                       .write_mask = mask, .src_type = nir_type_float32,
                       .io_semantics.num_slots = 1);
      break;

   case D12_DXBC_FILE_NULL:
      break;

   default:
      fail(ctx, "unsupported destination register file");
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
      case D12_DXBC_OP_DCL_UAV_STRUCTURED:
      case D12_DXBC_OP_DCL_TGSM_RAW:
      case D12_DXBC_OP_DCL_TGSM_STRUCTURED:
      case D12_DXBC_OP_DCL_RESOURCE_RAW:
      case D12_DXBC_OP_DCL_RESOURCE_STRUCTURED:
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

      case D12_DXBC_OP_RET:
         /* Falls out of the shader; NIR's implicit return handles it. */
         break;

      default:
         /* Name the opcode: the next person needs a work list, not a
          * verdict. */
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

   ctx.b = nir_builder_init_simple_shader(stage, &agx_nir_options, "dxbc");

   /* Token 0 is the version, token 1 the length in tokens. */
   const uint32_t *tokens = (const uint32_t *)chunk;
   unsigned num_tokens = chunk_size / 4;
   if (num_tokens < 2) {
      *error = "truncated shader chunk";
      ralloc_free(ctx.b.shader);
      return NULL;
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
