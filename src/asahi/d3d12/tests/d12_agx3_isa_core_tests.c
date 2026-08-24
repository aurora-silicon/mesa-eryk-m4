/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_isa_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);               \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static int
check_encoding(enum agx_isa_encoding encoding,
               enum d12_agx3_binary_alu_op op, uint16_t dest, uint16_t src0,
               uint16_t src1, const uint8_t *expected, uint8_t length)
{
   struct d12_agx3_binary_alu_encoding encoded;
   CHECK(d12_agx3_encode_binary_alu(encoding, op, dest, src0, src1,
                                    &encoded));
   CHECK(encoded.length == length);
   CHECK(memcmp(encoded.bytes, expected, length) == 0);

   for (uint8_t i = length; i < D12_AGX3_BINARY_ALU_MAX_LENGTH; ++i)
      CHECK(encoded.bytes[i] == 0);

   return 0;
}

int
main(void)
{
   static const uint8_t iadd_b[] = {
      0x9f, 0x01, 0x56, 0x00, 0x02, 0x00, 0x00, 0xa8, 0x13, 0x05,
   };
   static const uint8_t iadd_c[] = {
      0x2f, 0x00, 0x04, 0x1a, 0x21, 0x00, 0xa3, 0x02, 0x28, 0x80, 0x01, 0x00,
   };
   static const uint8_t iand_b[] = {
      0x0b, 0x01, 0x0f, 0x03, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
   };
   static const uint8_t iand_c[] = {
      0x2b, 0x00, 0x07, 0x80, 0x30, 0x30, 0xa0, 0x02, 0x84, 0x80,
   };
   static const uint8_t ior_b[] = {
      0x0b, 0x01, 0x0f, 0x03, 0x02, 0x08, 0x00, 0x80, 0x00, 0x00,
   };
   static const uint8_t ior_c[] = {
      0x2b, 0x00, 0x07, 0x80, 0x32, 0x38, 0xa0, 0x02, 0x84, 0x80,
   };
   static const uint8_t ixor_b[] = {
      0x0b, 0x01, 0x0e, 0x03, 0x02, 0x08, 0x00, 0x80, 0x00, 0x00,
   };
   static const uint8_t ixor_c[] = {
      0x2b, 0x00, 0x06, 0x80, 0x32, 0x38, 0xa0, 0x02, 0x84, 0x80,
   };

   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_IADD_U32, 0, 0, 0, iadd_b,
                        sizeof(iadd_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_IADD_U32, 0, 0, 0, iadd_c,
                        sizeof(iadd_c)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_IAND_U32, 0, 0, 2, iand_b,
                        sizeof(iand_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_IAND_U32, 0, 0, 2, iand_c,
                        sizeof(iand_c)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_IOR_U32, 0, 0, 2, ior_b,
                        sizeof(ior_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_IOR_U32, 0, 0, 2, ior_c,
                        sizeof(ior_c)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_IXOR_U32, 0, 0, 2, ixor_b,
                        sizeof(ixor_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_IXOR_U32, 0, 0, 2, ixor_c,
                        sizeof(ixor_c)) == 0);

   struct d12_agx3_binary_alu_encoding encoded = {
      .bytes = {0xff},
      .length = UINT8_MAX,
   };
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX2,
                                     D12_AGX3_BINARY_ALU_IADD_U32, 0, 0, 0,
                                     &encoded));
   CHECK(encoded.length == 0 && encoded.bytes[0] == 0);
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_UNKNOWN,
                                     D12_AGX3_BINARY_ALU_IAND_U32, 0, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     (enum d12_agx3_binary_alu_op)99, 0, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_IADD_U32, 1, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IAND_U32, 0, 1, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IAND_U32, 0, 0, 256,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IOR_U32, 0, 257, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_IXOR_U32, 0, 0, 257,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IADD_U32, 0, 0, 0,
                                     NULL));

   printf("d12 AGX3 ISA core: %u checks passed\n", checks);
   return 0;
}
