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

   default:
      return false;
   }
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
