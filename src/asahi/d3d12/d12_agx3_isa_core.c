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
_Static_assert(AGX3_YIELD_I16_REG_LENGTH == D12_AGX3_YIELD_I16_REG_LENGTH,
               "D3D AGX3 yield storage must match the canonical form");

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
