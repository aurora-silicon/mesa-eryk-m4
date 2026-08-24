/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_isa_core.h"

#include "asahi/compiler/agx3_pack.h"

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

static int
check_ishl_register_domain(void)
{
   static const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      /* Exercise every modeled even half-register independently in all
       * three operand positions.  Exact corpus vectors below anchor the
       * bytes; this loop proves the D3D bridge preserves every packer field. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0},
            {0, value, 0},
            {0, 0, value},
         };

         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_sll_u32_reg_reg expected;
            struct d12_agx3_binary_alu_encoding encoded;

            CHECK(agx3_pack_sll_u32_reg_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               operands[operand][2], &expected));
            CHECK(d12_agx3_encode_binary_alu(
               encodings[e], D12_AGX3_BINARY_ALU_ISHL_U32,
               operands[operand][0], operands[operand][1],
               operands[operand][2], &encoded));
            CHECK(encoded.length == expected.length);
            CHECK(memcmp(encoded.bytes, expected.bytes,
                         D12_AGX3_BINARY_ALU_MAX_LENGTH) == 0);
         }
      }
   }

   return 0;
}

static int
check_iashr_register_domain(void)
{
   static const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      /* Exercise every modeled even half-register independently in all
       * three operand positions. Exact corpus vectors below anchor the
       * bytes; this loop proves the D3D bridge preserves every packer field. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0},
            {0, value, 0},
            {0, 0, value},
         };

         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_asr_i32_reg_reg expected;
            struct d12_agx3_binary_alu_encoding encoded;
            uint8_t padded[D12_AGX3_BINARY_ALU_MAX_LENGTH] = {0};

            CHECK(agx3_pack_asr_i32_reg_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               operands[operand][2], &expected));
            CHECK(d12_agx3_encode_binary_alu(
               encodings[e], D12_AGX3_BINARY_ALU_IASHR_I32,
               operands[operand][0], operands[operand][1],
               operands[operand][2], &encoded));
            CHECK(encoded.length == expected.length);
            memcpy(padded, expected.bytes, expected.length);
            CHECK(memcmp(encoded.bytes, padded, sizeof(padded)) == 0);
         }
      }
   }

   return 0;
}

static bool
binary_encoding_is_cleared(
   const struct d12_agx3_binary_alu_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_BINARY_ALU_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_binary_rejected_and_cleared(enum agx_isa_encoding encoding,
                                  enum d12_agx3_binary_alu_op op,
                                  uint16_t dest, uint16_t src0,
                                  uint16_t src1)
{
   struct d12_agx3_binary_alu_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_binary_alu(encoding, op, dest, src0, src1,
                                     &encoded));
   CHECK(binary_encoding_is_cleared(&encoded));
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
   static const uint8_t isub_b[] = {
      0x1f, 0x01, 0x56, 0x06, 0x02, 0x08, 0x00, 0xa8, 0x11, 0x05,
   };
   static const uint8_t isub_c[] = {
      0x3f, 0x00, 0x04, 0x3a, 0x21, 0x00,
      0xa2, 0x0a, 0x08, 0x80, 0x01, 0x00,
   };
   static const uint8_t ishl_b[] = {
      0x27, 0x10, 0x54, 0x06, 0x02, 0x14,
      0x00, 0x00, 0xf0, 0x10, 0x05, 0x00,
   };
   static const uint8_t ishl_c[] = {
      0x37, 0x06, 0x00, 0x5a, 0x22, 0x00, 0xa0,
      0x0a, 0x08, 0x80, 0x00, 0x34, 0x00, 0x00,
   };
   static const uint8_t iashr_b[] = {
      0xa7, 0x11, 0x54, 0x06, 0x02, 0x14, 0x00, 0x18, 0xe2, 0x00,
   };
   static const uint8_t iashr_c[] = {
      0x37, 0x06, 0x00, 0x5a, 0x22, 0x00,
      0xa3, 0x0a, 0x08, 0x00, 0x40, 0x00,
   };
   static const uint8_t inot_b[] = {
      0x3b, 0x01, 0x26, 0x00, 0x01, 0x08, 0x00, 0x80, 0x00, 0x00,
   };
   static const uint8_t inot_c[] = {
      0x3b, 0x00, 0x06, 0x1a, 0x29, 0x08, 0xa8, 0x0a,
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
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_ISUB_U32, 6, 4, 0, isub_b,
                        sizeof(isub_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_ISUB_U32, 6, 4, 0, isub_c,
                        sizeof(isub_c)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_ISHL_U32, 6, 10, 0, ishl_b,
                        sizeof(ishl_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_ISHL_U32, 6, 10, 0, ishl_c,
                        sizeof(ishl_c)) == 0);
   CHECK(check_ishl_register_domain() == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_IASHR_I32, 6, 10, 0, iashr_b,
                        sizeof(iashr_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_IASHR_I32, 6, 10, 0, iashr_c,
                        sizeof(iashr_c)) == 0);
   CHECK(check_iashr_register_domain() == 0);

   struct d12_agx3_unary_alu_encoding unary;
   CHECK(d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX3B,
                                   D12_AGX3_UNARY_ALU_INOT_U32, 6, 0,
                                   &unary));
   CHECK(unary.length == sizeof(inot_b));
   CHECK(memcmp(unary.bytes, inot_b, sizeof(inot_b)) == 0);
   CHECK(unary.bytes[10] == 0 && unary.bytes[11] == 0);
   CHECK(d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX3C,
                                   D12_AGX3_UNARY_ALU_INOT_U32, 6, 0,
                                   &unary));
   CHECK(unary.length == sizeof(inot_c));
   CHECK(memcmp(unary.bytes, inot_c, sizeof(inot_c)) == 0);
   CHECK(unary.bytes[8] == 0 && unary.bytes[11] == 0);

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
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISUB_U32, 192, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISUB_U32, 0, 0, 256,
                                     &encoded));
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX2,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 0,
                                     &encoded));
   CHECK(encoded.length == 0 && encoded.bytes[0] == 0);
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_UNKNOWN,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 1, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 1, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 1,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 1, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 1, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 1,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 192, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 192, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3B,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 192,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 256, 0, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 256, 0,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 256,
                                     &encoded));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ISHL_U32, 0, 0, 0,
                                     NULL));
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, (enum d12_agx3_binary_alu_op)99,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX2, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_UNKNOWN, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IASHR_I32,
            1, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 1, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 0, 1) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IASHR_I32,
            1, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 1, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 0, 1) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IASHR_I32,
            192, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 192, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 0, 192) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IASHR_I32,
            256, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 256, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IASHR_I32,
            0, 0, 256) == 0);
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IASHR_I32,
                                     0, 0, 0, NULL));
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IADD_U32, 0, 0, 0,
                                     NULL));
   CHECK(!d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX2,
                                    D12_AGX3_UNARY_ALU_INOT_U32, 0, 0,
                                    &unary));
   CHECK(unary.length == 0 && unary.bytes[0] == 0);
   CHECK(!d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX3C,
                                    (enum d12_agx3_unary_alu_op)99, 0, 0,
                                    &unary));
   CHECK(!d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX3B,
                                    D12_AGX3_UNARY_ALU_INOT_U32, 1, 0,
                                    &unary));
   CHECK(!d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX3C,
                                    D12_AGX3_UNARY_ALU_INOT_U32, 0, 256,
                                    &unary));
   CHECK(!d12_agx3_encode_unary_alu(AGX_ISA_ENCODING_AGX3C,
                                    D12_AGX3_UNARY_ALU_INOT_U32, 0, 0,
                                    NULL));

   printf("d12 AGX3 ISA core: %u checks passed\n", checks);
   return 0;
}
