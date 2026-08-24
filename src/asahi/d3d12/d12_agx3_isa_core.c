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
   switch (chip) {
   /* Exact AGX3b Apple compiler targets present in the source corpus. */
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
      encoding = AGX_ISA_ENCODING_AGX3B;
      break;

   /* Exact AGX3c G17 Apple compiler targets in the source corpus. */
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      encoding = AGX_ISA_ENCODING_AGX3C;
      break;

   default:
      return false;
   }

   return d12_agx3_encode_binary_alu(
      encoding, D12_AGX3_BINARY_ALU_IMUL_U32, dest, src0, src1, out);
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
