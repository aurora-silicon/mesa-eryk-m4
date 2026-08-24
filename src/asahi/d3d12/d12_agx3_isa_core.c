/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_isa_core.h"

#include "asahi/compiler/agx3_pack.h"

#include <string.h>

_Static_assert(AGX3_SLL_U32_REG_REG_MAX_LENGTH <=
                  D12_AGX3_BINARY_ALU_MAX_LENGTH,
               "D3D AGX3 ALU storage must hold the canonical sll form");
_Static_assert(AGX3_ASR_I32_REG_REG_MAX_LENGTH <=
                  D12_AGX3_BINARY_ALU_MAX_LENGTH,
               "D3D AGX3 ALU storage must hold the canonical sra form");
_Static_assert(AGX3_LSR_U32_REG_REG_MAX_LENGTH <=
                  D12_AGX3_BINARY_ALU_MAX_LENGTH,
               "D3D AGX3 ALU storage must hold the canonical srl form");
_Static_assert(AGX3_IMUL_U32_REG_REG_MAX_LENGTH <=
                  D12_AGX3_BINARY_ALU_MAX_LENGTH,
               "D3D AGX3 ALU storage must hold the canonical imul form");
_Static_assert(AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH <=
                  D12_AGX3_TERNARY_ALU_MAX_LENGTH,
               "D3D AGX3 ALU storage must hold the canonical imadd form");
_Static_assert(AGX3_FADD_F32_REG_REG_LENGTH ==
                  D12_AGX3_FADD_F32_REG_REG_LENGTH,
               "D3D AGX3 f32add storage must match the canonical form");
_Static_assert(AGX3_FMUL_F32_REG_REG_LENGTH ==
                  D12_AGX3_FMUL_F32_REG_REG_LENGTH,
               "D3D AGX3 f32mul storage must match the canonical form");
_Static_assert(AGX3_FMUL_F32_REG_REG_LONG_LENGTH ==
                  D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH,
               "D3D AGX3 long f32mul storage must match the canonical form");
_Static_assert(AGX3_FMAX_F32_REG_REG_MAX_LENGTH ==
                  D12_AGX3_FMAX_F32_REG_REG_MAX_LENGTH,
               "D3D AGX3 f32max storage must match the canonical form");
_Static_assert(AGX3_I32_TO_F32_REG_MAX_LENGTH ==
                  D12_AGX3_I32_TO_F32_REG_MAX_LENGTH,
               "D3D AGX3 i32-to-f32 storage must match the canonical form");
_Static_assert(AGX3_U32_TO_F32_REG_MAX_LENGTH ==
                  D12_AGX3_U32_TO_F32_REG_MAX_LENGTH,
               "D3D AGX3 u32-to-f32 storage must match the canonical form");
_Static_assert(AGX3_F32_RINT_REG_LENGTH == D12_AGX3_F32_RINT_REG_LENGTH,
               "D3D AGX3 f32-rint storage must match the canonical form");
_Static_assert(AGX3_F32_RCP_REG_LENGTH == D12_AGX3_F32_RCP_REG_LENGTH,
               "D3D AGX3 f32-rcp storage must match the canonical form");
_Static_assert(AGX3_F32_RSQRT_REG_LENGTH == D12_AGX3_F32_RSQRT_REG_LENGTH,
               "D3D AGX3 f32-rsqrt storage must match the canonical form");
_Static_assert(AGX3_I32_COMPARE_SELECT_IMM_5_7_REG_REG_MAX_LENGTH ==
                  D12_AGX3_I32_COMPARE_SELECT_IMM_5_7_REG_REG_MAX_LENGTH,
               "D3D AGX3 i32 compare/select storage must match canonical form");
_Static_assert(AGX3_F32_TO_INT_RTZ_REG_LENGTH ==
                  D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH,
               "D3D AGX3 f32-to-int storage must match the canonical form");
_Static_assert(AGX3_YIELD_I16_REG_LENGTH == D12_AGX3_YIELD_I16_REG_LENGTH,
               "D3D AGX3 yield storage must match the canonical form");
_Static_assert(AGX3_END_LENGTH ==
                  D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH,
               "D3D AGX3 terminal wait storage must match canonical end");

static bool
d12_agx3_compiler_target_encoding(enum agx_chip chip,
                                  enum agx_isa_encoding *encoding)
{
   switch (chip) {
   /* Exact AGX3b Apple compiler targets present in the source corpus. */
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
      *encoding = AGX_ISA_ENCODING_AGX3B;
      return true;

   /* Exact AGX3c G17 Apple compiler targets in the source corpus. */
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      *encoding = AGX_ISA_ENCODING_AGX3C;
      return true;

   default:
      return false;
   }
}

static bool
d12_agx3_fmul_compiler_target_encoding(enum agx_chip chip,
                                       enum agx_isa_encoding *encoding)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
      *encoding = AGX_ISA_ENCODING_AGX3B;
      return true;

   case AGX_CHIP_G17S:
      *encoding = AGX_ISA_ENCODING_AGX3C;
      return true;

   default:
      return false;
   }
}

static bool
d12_agx3_fmul_long_compiler_target_encoding(
   enum agx_chip chip, enum agx_isa_encoding *encoding)
{
   switch (chip) {
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      *encoding = AGX_ISA_ENCODING_AGX3C;
      return true;

   default:
      return false;
   }
}

static bool
d12_agx3_fma_compiler_target_encoding(enum agx_chip chip,
                                      enum agx_isa_encoding *encoding)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
      *encoding = AGX_ISA_ENCODING_AGX3B;
      return true;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      *encoding = AGX_ISA_ENCODING_AGX3C;
      return true;

   default:
      return false;
   }
}

static bool
d12_agx3_fmin_compiler_target_encoding(enum agx_chip chip,
                                       enum agx_isa_encoding *encoding)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
      *encoding = AGX_ISA_ENCODING_AGX3B;
      return true;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      *encoding = AGX_ISA_ENCODING_AGX3C;
      return true;

   default:
      return false;
   }
}

static bool
d12_agx3_fmax_compiler_target_encoding(enum agx_chip chip,
                                       enum agx_isa_encoding *encoding)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
      *encoding = AGX_ISA_ENCODING_AGX3B;
      return true;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      *encoding = AGX_ISA_ENCODING_AGX3C;
      return true;

   default:
      return false;
   }
}

bool
d12_agx3_encode_binary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_binary_alu_op op,
   uint16_t dest, uint16_t src0, uint16_t src1,
   struct d12_agx3_binary_alu_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   switch (op) {
   case D12_AGX3_BINARY_ALU_IADD_U32: {
      struct agx3_encoded_iadd_u32_reg_reg encoded;
      if (!agx3_pack_iadd_u32_reg_reg(encoding, dest, src0, src1, &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_IAND_U32: {
      struct agx3_encoded_bit_and_u32_reg_reg encoded;
      if (!agx3_pack_bit_and_u32_reg_reg(encoding, dest, src0, src1,
                                         &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_IOR_U32: {
      struct agx3_encoded_bit_or_u32_reg_reg encoded;
      if (!agx3_pack_bit_or_u32_reg_reg(encoding, dest, src0, src1,
                                        &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_IXOR_U32: {
      struct agx3_encoded_bit_xor_u32_reg_reg encoded;
      if (!agx3_pack_bit_xor_u32_reg_reg(encoding, dest, src0, src1,
                                         &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_ISUB_U32: {
      struct agx3_encoded_isub_u32_reg_reg encoded;
      if (!agx3_pack_isub_u32_reg_reg(encoding, dest, src0, src1,
                                      &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_ISHL_U32: {
      struct agx3_encoded_sll_u32_reg_reg encoded;
      if (!agx3_pack_sll_u32_reg_reg(encoding, dest, src0, src1, &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_IASHR_I32: {
      struct agx3_encoded_asr_i32_reg_reg encoded;
      if (!agx3_pack_asr_i32_reg_reg(encoding, dest, src0, src1, &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_ILSHR_U32: {
      struct agx3_encoded_lsr_u32_reg_reg encoded;
      if (!agx3_pack_lsr_u32_reg_reg(encoding, dest, src0, src1, &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   case D12_AGX3_BINARY_ALU_IMUL_U32: {
      struct agx3_encoded_imul_u32_reg_reg encoded;
      if (!agx3_pack_imul_u32_reg_reg(encoding, dest, src0, src1, &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   default:
      return false;
   }
}

bool
d12_agx3_encode_integer_multiply(
   enum agx_chip chip, enum d12_agx3_integer_multiply_selection selection,
   uint16_t dest, uint16_t src0, uint16_t src1,
   struct d12_agx3_binary_alu_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   if (selection != D12_AGX3_INTEGER_MULTIPLY_LOW_U32)
      return false;

   enum agx_isa_encoding encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &encoding))
      return false;

   return d12_agx3_encode_binary_alu(
      encoding, D12_AGX3_BINARY_ALU_IMUL_U32, dest, src0, src1, out);
}

bool
d12_agx3_encode_ternary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_ternary_alu_op op,
   uint16_t dest, uint16_t src0, uint16_t src1, uint16_t src2,
   struct d12_agx3_ternary_alu_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   switch (op) {
   case D12_AGX3_TERNARY_ALU_IMADD_U32: {
      struct agx3_encoded_imadd_u32_reg_reg_reg encoded;
      if (!agx3_pack_imadd_u32_reg_reg_reg(encoding, dest, src0, src1, src2,
                                           &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   default:
      return false;
   }
}

bool
d12_agx3_encode_integer_multiply_add(
   enum agx_chip chip,
   enum d12_agx3_integer_multiply_add_selection selection, uint16_t dest,
   uint16_t mul0, uint16_t mul1, uint16_t addend,
   struct d12_agx3_ternary_alu_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   if (selection != D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32)
      return false;

   enum agx_isa_encoding encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &encoding))
      return false;

   return d12_agx3_encode_ternary_alu(
      encoding, D12_AGX3_TERNARY_ALU_IMADD_U32, dest, mul0, mul1, addend,
      out);
}

bool
d12_agx3_encode_fadd_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fadd_f32_reg_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_fadd_f32_reg_reg encoded;
   if (!agx3_pack_fadd_f32_reg_reg(encoding, dest, src0, src1, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_fadd_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1)
{
   if (dest)
      *dest = 0;
   if (src0)
      *src0 = 0;
   if (src1)
      *src1 = 0;

   if (!instruction || !dest || !src0 || !src1 ||
       instruction_size != D12_AGX3_FADD_F32_REG_REG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src0 = 0, decoded_src1 = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_fadd_f32_reg_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src0, &decoded_src1, &decoded_length) ||
       decoded_length != D12_AGX3_FADD_F32_REG_REG_LENGTH)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   return true;
}

bool
d12_agx3_encode_fmul_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmul_f32_reg_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmul_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_fmul_f32_reg_reg encoded;
   if (!agx3_pack_fmul_f32_reg_reg(encoding, dest, src0, src1, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_fmul_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1)
{
   if (dest)
      *dest = 0;
   if (src0)
      *src0 = 0;
   if (src1)
      *src1 = 0;

   if (!instruction || !dest || !src0 || !src1 ||
       instruction_size != D12_AGX3_FMUL_F32_REG_REG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmul_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src0 = 0, decoded_src1 = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_fmul_f32_reg_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src0, &decoded_src1, &decoded_length) ||
       decoded_length != D12_AGX3_FMUL_F32_REG_REG_LENGTH)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   return true;
}

bool
d12_agx3_encode_fmul_f32_reg_reg_long(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmul_f32_reg_reg_long_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmul_long_compiler_target_encoding(
          chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_fmul_f32_reg_reg_long encoded;
   if (!agx3_pack_fmul_f32_reg_reg_long(
          encoding, dest, src0, src1, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_fmul_f32_reg_reg_long(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1)
{
   if (dest)
      *dest = 0;
   if (src0)
      *src0 = 0;
   if (src1)
      *src1 = 0;

   if (!instruction || !dest || !src0 || !src1 ||
       instruction_size != D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmul_long_compiler_target_encoding(
          chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src0 = 0, decoded_src1 = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_fmul_f32_reg_reg_long(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src0, &decoded_src1, &decoded_length) ||
       decoded_length != D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   return true;
}

bool
d12_agx3_encode_fma_f32_reg_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t mul0, uint16_t mul1, uint16_t addend,
   struct d12_agx3_fma_f32_reg_reg_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fma_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_fma_f32_reg_reg_reg encoded;
   if (!agx3_pack_fma_f32_reg_reg_reg(
          encoding, dest, mul0, mul1, addend, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_fma_f32_reg_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *mul0, uint16_t *mul1, uint16_t *addend)
{
   if (dest)
      *dest = 0;
   if (mul0)
      *mul0 = 0;
   if (mul1)
      *mul1 = 0;
   if (addend)
      *addend = 0;

   const uint32_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 8 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 16 : 0;
   if (!instruction || !dest || !mul0 || !mul1 || !addend ||
       expected_length == 0 || instruction_size != expected_length)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fma_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_mul0 = 0, decoded_mul1 = 0;
   uint16_t decoded_addend = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_fma_f32_reg_reg_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_mul0, &decoded_mul1, &decoded_addend, &decoded_length) ||
       decoded_length != expected_length)
      return false;

   *dest = decoded_dest;
   *mul0 = decoded_mul0;
   *mul1 = decoded_mul1;
   *addend = decoded_addend;
   return true;
}

bool
d12_agx3_encode_fmin_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmin_f32_reg_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmin_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_fmin_f32_reg_reg encoded;
   if (!agx3_pack_fmin_f32_reg_reg(
          encoding, dest, src0, src1, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_fmin_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1)
{
   if (dest)
      *dest = 0;
   if (src0)
      *src0 = 0;
   if (src1)
      *src1 = 0;

   const uint32_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 6 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 14 : 0;
   if (!instruction || !dest || !src0 || !src1 || expected_length == 0 ||
       instruction_size != expected_length)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmin_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src0 = 0, decoded_src1 = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_fmin_f32_reg_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src0, &decoded_src1, &decoded_length) ||
       decoded_length != expected_length)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   return true;
}

bool
d12_agx3_encode_fmax_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmax_f32_reg_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmax_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_fmax_f32_reg_reg encoded;
   if (!agx3_pack_fmax_f32_reg_reg(
          encoding, dest, src0, src1, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_fmax_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1)
{
   if (dest)
      *dest = 0;
   if (src0)
      *src0 = 0;
   if (src1)
      *src1 = 0;

   const uint32_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 6 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 14 : 0;
   if (!instruction || !dest || !src0 || !src1 || expected_length == 0 ||
       instruction_size != expected_length)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_fmax_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src0 = 0, decoded_src1 = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_fmax_f32_reg_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src0, &decoded_src1, &decoded_length) ||
       decoded_length != expected_length)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   return true;
}

bool
d12_agx3_encode_i32_to_f32_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src, struct d12_agx3_i32_to_f32_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_i32_to_f32_reg encoded;
   if (!agx3_pack_i32_to_f32_reg(encoding, dest, src, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_i32_to_f32_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src)
{
   if (dest)
      *dest = 0;
   if (src)
      *src = 0;

   const uint32_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 8 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 10 : 0;
   if (!instruction || !dest || !src || expected_length == 0 ||
       instruction_size != expected_length)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_i32_to_f32_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src, &decoded_length) ||
       decoded_length != expected_length)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   return true;
}

bool
d12_agx3_encode_u32_to_f32_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src, struct d12_agx3_u32_to_f32_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_u32_to_f32_reg encoded;
   if (!agx3_pack_u32_to_f32_reg(encoding, dest, src, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_u32_to_f32_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src)
{
   if (dest)
      *dest = 0;
   if (src)
      *src = 0;

   const uint32_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 8 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 10 : 0;
   if (!instruction || !dest || !src || expected_length == 0 ||
       instruction_size != expected_length)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_u32_to_f32_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src, &decoded_length) ||
       decoded_length != expected_length)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   return true;
}

bool
d12_agx3_encode_f32_rint_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src, struct d12_agx3_f32_rint_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_f32_rint_reg encoded;
   if (!agx3_pack_f32_rint_reg(encoding, dest, src, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_f32_rint_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src)
{
   if (dest)
      *dest = 0;
   if (src)
      *src = 0;

   if (!instruction || !dest || !src ||
       instruction_size != D12_AGX3_F32_RINT_REG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_f32_rint_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src, &decoded_length) ||
       decoded_length != D12_AGX3_F32_RINT_REG_LENGTH)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   return true;
}

bool
d12_agx3_encode_f32_rcp_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src, struct d12_agx3_f32_rcp_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_f32_rcp_reg encoded;
   if (!agx3_pack_f32_rcp_reg(encoding, dest, src, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_f32_rcp_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src)
{
   if (dest)
      *dest = 0;
   if (src)
      *src = 0;

   if (!instruction || !dest || !src ||
       instruction_size != D12_AGX3_F32_RCP_REG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_f32_rcp_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src, &decoded_length) ||
       decoded_length != D12_AGX3_F32_RCP_REG_LENGTH)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   return true;
}

bool
d12_agx3_encode_f32_rsqrt_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src, struct d12_agx3_f32_rsqrt_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   struct agx3_encoded_f32_rsqrt_reg encoded;
   if (!agx3_pack_f32_rsqrt_reg(encoding, dest, src, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_f32_rsqrt_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src)
{
   if (dest)
      *dest = 0;
   if (src)
      *src = 0;

   if (!instruction || !dest || !src ||
       instruction_size != D12_AGX3_F32_RSQRT_REG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   uint16_t decoded_dest = 0, decoded_src = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_f32_rsqrt_reg(
          encoding, instruction, instruction_size, &decoded_dest,
          &decoded_src, &decoded_length) ||
       decoded_length != D12_AGX3_F32_RSQRT_REG_LENGTH)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   return true;
}

static bool
d12_agx3_i32_compare_predicate_to_shared(
   enum d12_agx3_i32_compare_predicate predicate,
   enum agx3_i32_compare_predicate *shared)
{
   switch (predicate) {
   case D12_AGX3_I32_COMPARE_EQ:
      *shared = AGX3_I32_COMPARE_EQ;
      return true;
   case D12_AGX3_I32_COMPARE_SLT:
      *shared = AGX3_I32_COMPARE_SLT;
      return true;
   default:
      return false;
   }
}

bool
d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   enum d12_agx3_i32_compare_predicate predicate, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   enum agx3_i32_compare_predicate shared_predicate;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding ||
       !d12_agx3_i32_compare_predicate_to_shared(predicate,
                                                  &shared_predicate))
      return false;

   struct agx3_encoded_i32_compare_select_imm_5_7_reg_reg encoded;
   if (!agx3_pack_i32_compare_select_imm_5_7_reg_reg(
          encoding, shared_predicate, dest, src0, src1, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size,
   enum d12_agx3_i32_compare_predicate *predicate, uint16_t *dest,
   uint16_t *src0, uint16_t *src1)
{
   if (predicate)
      *predicate = D12_AGX3_I32_COMPARE_EQ;
   if (dest)
      *dest = 0;
   if (src0)
      *src0 = 0;
   if (src1)
      *src1 = 0;

   const uint32_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 10 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 14 : 0;
   if (!instruction || !predicate || !dest || !src0 || !src1 ||
       expected_length == 0 || instruction_size != expected_length)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   enum agx3_i32_compare_predicate shared_predicate;
   uint16_t decoded_dest = 0, decoded_src0 = 0, decoded_src1 = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_i32_compare_select_imm_5_7_reg_reg(
          encoding, instruction, instruction_size, &shared_predicate,
          &decoded_dest, &decoded_src0, &decoded_src1, &decoded_length) ||
       decoded_length != expected_length)
      return false;

   switch (shared_predicate) {
   case AGX3_I32_COMPARE_EQ:
      *predicate = D12_AGX3_I32_COMPARE_EQ;
      break;
   case AGX3_I32_COMPARE_SLT:
      *predicate = D12_AGX3_I32_COMPARE_SLT;
      break;
   default:
      return false;
   }

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   return true;
}

static bool
d12_agx3_f32_to_int_rtz_mode_to_shared(
   enum d12_agx3_f32_to_int_rtz_mode mode,
   enum agx3_f32_to_int_rtz_mode *shared)
{
   switch (mode) {
   case D12_AGX3_F32_TO_I32_RTZ:
      *shared = AGX3_F32_TO_I32_RTZ;
      return true;
   case D12_AGX3_F32_TO_U32_RTZ:
      *shared = AGX3_F32_TO_U32_RTZ;
      return true;
   default:
      return false;
   }
}

bool
d12_agx3_encode_f32_to_int_rtz_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   enum d12_agx3_f32_to_int_rtz_mode mode, uint16_t dest, uint16_t src,
   struct d12_agx3_f32_to_int_rtz_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding target_encoding;
   enum agx3_f32_to_int_rtz_mode shared_mode;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding ||
       !d12_agx3_f32_to_int_rtz_mode_to_shared(mode, &shared_mode))
      return false;

   struct agx3_encoded_f32_to_int_rtz_reg encoded;
   if (!agx3_pack_f32_to_int_rtz_reg(
          encoding, shared_mode, dest, src, &encoded) ||
       encoded.length > sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_f32_to_int_rtz_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size,
   enum d12_agx3_f32_to_int_rtz_mode *mode, uint16_t *dest, uint16_t *src)
{
   if (mode)
      *mode = D12_AGX3_F32_TO_I32_RTZ;
   if (dest)
      *dest = 0;
   if (src)
      *src = 0;

   if (!instruction || !mode || !dest || !src ||
       instruction_size != D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH)
      return false;

   enum agx_isa_encoding target_encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &target_encoding) ||
       encoding != target_encoding)
      return false;

   enum agx3_f32_to_int_rtz_mode shared_mode;
   uint16_t decoded_dest = 0, decoded_src = 0;
   uint8_t decoded_length = 0;
   if (!agx3_unpack_f32_to_int_rtz_reg(
          encoding, instruction, instruction_size, &shared_mode,
          &decoded_dest, &decoded_src, &decoded_length) ||
       decoded_length != D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH)
      return false;

   switch (shared_mode) {
   case AGX3_F32_TO_I32_RTZ:
      *mode = D12_AGX3_F32_TO_I32_RTZ;
      break;
   case AGX3_F32_TO_U32_RTZ:
      *mode = D12_AGX3_F32_TO_U32_RTZ;
      break;
   default:
      return false;
   }

   *dest = decoded_dest;
   *src = decoded_src;
   return true;
}

bool
d12_agx3_encode_yield_i16_reg(
   enum agx_chip chip, uint16_t src,
   struct d12_agx3_yield_i16_reg_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &encoding))
      return false;

   struct agx3_encoded_yield_i16_reg encoded;
   if (!agx3_pack_yield_i16_reg(encoding, src, &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_yield_i16_reg(enum agx_chip chip, const void *instruction,
                              uint32_t instruction_size, uint16_t *src)
{
   if (src)
      *src = 0;
   if (!instruction || !src ||
       instruction_size != D12_AGX3_YIELD_I16_REG_LENGTH)
      return false;

   enum agx_isa_encoding encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &encoding))
      return false;

   uint16_t decoded_src = 0;
   uint8_t decoded_length = 0;
   struct agx3_encoded_yield_i16_reg canonical;
   if (!agx3_unpack_yield_i16_reg(encoding, instruction, instruction_size,
                                  &decoded_src, &decoded_length) ||
       decoded_length != D12_AGX3_YIELD_I16_REG_LENGTH ||
       !agx3_pack_yield_i16_reg(encoding, decoded_src, &canonical) ||
       canonical.length != decoded_length ||
       memcmp(instruction, canonical.bytes, decoded_length) != 0)
      return false;

   *src = decoded_src;
   return true;
}

bool
d12_agx3_encode_end_with_wait_fence_mask(
   enum agx_chip chip, uint8_t wait_fence_mask,
   struct d12_agx3_end_with_wait_fence_mask_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   enum agx_isa_encoding encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &encoding))
      return false;

   struct agx3_encoded_end encoded;
   if (!agx3_pack_end_with_wait_fence_mask(encoding, wait_fence_mask,
                                           &encoded) ||
       encoded.length != sizeof(out->bytes))
      return false;

   memcpy(out->bytes, encoded.bytes, encoded.length);
   out->length = encoded.length;
   return true;
}

bool
d12_agx3_decode_end_with_wait_fence_mask(enum agx_chip chip,
                                         const void *instruction,
                                         uint32_t instruction_size,
                                         uint8_t *wait_fence_mask)
{
   if (wait_fence_mask)
      *wait_fence_mask = 0;
   if (!instruction || !wait_fence_mask ||
       instruction_size != D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH)
      return false;

   enum agx_isa_encoding encoding;
   if (!d12_agx3_compiler_target_encoding(chip, &encoding))
      return false;

   uint8_t decoded_mask = 0;
   uint8_t decoded_length = 0;
   struct agx3_encoded_end canonical;
   if (!agx3_unpack_end_with_wait_fence_mask(
          encoding, instruction, instruction_size, &decoded_mask,
          &decoded_length) ||
       decoded_length != D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH ||
       !agx3_pack_end_with_wait_fence_mask(encoding, decoded_mask,
                                           &canonical) ||
       canonical.length != decoded_length ||
       memcmp(instruction, canonical.bytes, decoded_length) != 0)
      return false;

   *wait_fence_mask = decoded_mask;
   return true;
}

bool
d12_agx3_encode_unary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_unary_alu_op op,
   uint16_t dest, uint16_t src, struct d12_agx3_unary_alu_encoding *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));

   switch (op) {
   case D12_AGX3_UNARY_ALU_INOT_U32: {
      struct agx3_encoded_bit_not_u32_reg encoded;
      if (!agx3_pack_bit_not_u32_reg(encoding, dest, src, &encoded))
         return false;

      memcpy(out->bytes, encoded.bytes, encoded.length);
      out->length = encoded.length;
      return true;
   }

   default:
      return false;
   }
}
