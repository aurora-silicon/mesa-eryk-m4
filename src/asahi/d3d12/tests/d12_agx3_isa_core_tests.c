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
check_ternary_encoding(enum agx_isa_encoding encoding,
                       enum d12_agx3_ternary_alu_op op, uint16_t dest,
                       uint16_t src0, uint16_t src1, uint16_t src2,
                       const uint8_t *expected, uint8_t length)
{
   struct d12_agx3_ternary_alu_encoding encoded;
   CHECK(d12_agx3_encode_ternary_alu(encoding, op, dest, src0, src1, src2,
                                     &encoded));
   CHECK(encoded.length == length);
   CHECK(memcmp(encoded.bytes, expected, length) == 0);

   for (uint8_t i = length; i < D12_AGX3_TERNARY_ALU_MAX_LENGTH; ++i)
      CHECK(encoded.bytes[i] == 0);

   return 0;
}

static int
check_target_imadd_encoding(enum agx_chip chip, uint16_t dest, uint16_t mul0,
                            uint16_t mul1, uint16_t addend,
                            const uint8_t *expected, uint8_t length)
{
   struct d12_agx3_ternary_alu_encoding encoded;
   CHECK(d12_agx3_encode_integer_multiply_add(
      chip, D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32, dest, mul0, mul1, addend,
      &encoded));
   CHECK(encoded.length == length);
   CHECK(memcmp(encoded.bytes, expected, length) == 0);

   for (uint8_t i = length; i < D12_AGX3_TERNARY_ALU_MAX_LENGTH; ++i)
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

static int
check_ilshr_register_domain(void)
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
            struct agx3_encoded_lsr_u32_reg_reg expected;
            struct d12_agx3_binary_alu_encoding encoded;

            CHECK(agx3_pack_lsr_u32_reg_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               operands[operand][2], &expected));
            CHECK(d12_agx3_encode_binary_alu(
               encodings[e], D12_AGX3_BINARY_ALU_ILSHR_U32,
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

struct integer_target {
   enum agx_chip chip;
   enum agx_isa_encoding encoding;
};

static const struct integer_target integer_targets[] = {
   {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G15D, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G16P, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G16G, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G16S, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G17P, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C},
   {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
};

static bool
yield_encoding_is_cleared(
   const struct d12_agx3_yield_i16_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_YIELD_I16_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_yield_rejected_and_cleared(enum agx_chip chip, uint16_t src)
{
   struct d12_agx3_yield_i16_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_yield_i16_reg(chip, src, &encoded));
   CHECK(yield_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_yield_exact_witnesses(void)
{
   static const struct {
      enum agx_chip chip;
      enum agx_isa_encoding encoding;
      uint16_t src;
      uint8_t bytes[D12_AGX3_YIELD_I16_REG_LENGTH];
   } vectors[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, 191,
       {0x87, 0x04, 0x54, 0xbf, 0x04, 0x00}},
      {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, 255,
       {0x0f, 0x7f, 0x10, 0x02, 0x00, 0x00}},
   };

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      struct agx3_encoded_yield_i16_reg shared;
      struct d12_agx3_yield_i16_reg_encoding encoded;
      uint16_t decoded_src = UINT16_MAX;

      CHECK(agx3_pack_yield_i16_reg(vectors[v].encoding, vectors[v].src,
                                    &shared));
      CHECK(shared.length == sizeof(vectors[v].bytes));
      CHECK(memcmp(shared.bytes, vectors[v].bytes,
                   sizeof(vectors[v].bytes)) == 0);
      CHECK(d12_agx3_encode_yield_i16_reg(vectors[v].chip, vectors[v].src,
                                          &encoded));
      CHECK(encoded.length == sizeof(vectors[v].bytes));
      CHECK(memcmp(encoded.bytes, vectors[v].bytes,
                   sizeof(vectors[v].bytes)) == 0);
      CHECK(d12_agx3_decode_yield_i16_reg(
         vectors[v].chip, vectors[v].bytes, sizeof(vectors[v].bytes),
         &decoded_src));
      CHECK(decoded_src == vectors[v].src);
   }

   return 0;
}

static int
check_yield_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint16_t register_limit =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;

      for (uint16_t expected_src = 0; expected_src < register_limit;
           ++expected_src) {
         struct agx3_encoded_yield_i16_reg shared;
         struct d12_agx3_yield_i16_reg_encoding encoded;
         uint16_t shared_src = UINT16_MAX;
         uint16_t decoded_src = UINT16_MAX;
         uint8_t shared_length = 0;

         CHECK(agx3_pack_yield_i16_reg(target->encoding, expected_src,
                                       &shared));
         CHECK(d12_agx3_encode_yield_i16_reg(target->chip, expected_src,
                                              &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
         CHECK(agx3_unpack_yield_i16_reg(
            target->encoding, encoded.bytes, encoded.length, &shared_src,
            &shared_length));
         CHECK(shared_src == expected_src);
         CHECK(shared_length == encoded.length);
         CHECK(d12_agx3_decode_yield_i16_reg(
            target->chip, encoded.bytes, encoded.length, &decoded_src));
         CHECK(decoded_src == expected_src);
      }
   }

   return 0;
}

static int
check_yield_canonical_bit_domain(void)
{
   static const struct {
      enum agx_chip chip;
      enum agx_isa_encoding encoding;
      uint8_t operand_mask[D12_AGX3_YIELD_I16_REG_LENGTH];
   } representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B,
       {0x00, 0x00, 0x00, 0xff, 0x00, 0x00}},
      {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C,
       {0x00, 0x7f, 0x10, 0x00, 0x00, 0x00}},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      struct agx3_encoded_yield_i16_reg canonical;
      CHECK(agx3_pack_yield_i16_reg(representatives[r].encoding, 0,
                                    &canonical));

      for (unsigned bit = 0; bit < D12_AGX3_YIELD_I16_REG_LENGTH * 8;
           ++bit) {
         uint8_t mutated[D12_AGX3_YIELD_I16_REG_LENGTH];
         memcpy(mutated, canonical.bytes, sizeof(mutated));
         mutated[bit / 8] ^= 1u << (bit % 8);

         const bool is_operand =
            (representatives[r].operand_mask[bit / 8] &
             (1u << (bit % 8))) != 0;
         uint16_t decoded_src = UINT16_MAX;
         const bool accepted = d12_agx3_decode_yield_i16_reg(
            representatives[r].chip, mutated, sizeof(mutated), &decoded_src);
         CHECK(accepted == is_operand);

         if (accepted) {
            struct agx3_encoded_yield_i16_reg shared;
            struct d12_agx3_yield_i16_reg_encoding repacked;
            uint16_t shared_src = UINT16_MAX;
            uint8_t shared_length = 0;

            CHECK(agx3_unpack_yield_i16_reg(
               representatives[r].encoding, mutated, sizeof(mutated),
               &shared_src, &shared_length));
            CHECK(decoded_src == shared_src);
            CHECK(d12_agx3_encode_yield_i16_reg(
               representatives[r].chip, decoded_src, &repacked));
            CHECK(repacked.length == shared_length);
            CHECK(memcmp(repacked.bytes, mutated, shared_length) == 0);
            CHECK(agx3_pack_yield_i16_reg(
               representatives[r].encoding, decoded_src, &shared));
            CHECK(memcmp(shared.bytes, repacked.bytes, shared.length) == 0);
         } else {
            CHECK(decoded_src == 0);
         }
      }
   }

   return 0;
}

static int
check_yield_rejection_domain(void)
{
   struct agx3_encoded_yield_i16_reg packed_b, packed_c;
   CHECK(agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3B, 0, &packed_b));
   CHECK(agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3C, 0, &packed_c));

   uint16_t decoded_src = UINT16_MAX;
   for (unsigned raw_src = 192; raw_src < 256; ++raw_src) {
      uint8_t invalid[D12_AGX3_YIELD_I16_REG_LENGTH];
      memcpy(invalid, packed_b.bytes, sizeof(invalid));
      invalid[3] = raw_src;
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         AGX_CHIP_G15G, invalid, sizeof(invalid), &decoded_src));
      CHECK(decoded_src == 0);
   }

   for (unsigned raw_src = 256; raw_src < 512; ++raw_src) {
      uint8_t invalid[D12_AGX3_YIELD_I16_REG_LENGTH];
      memcpy(invalid, packed_c.bytes, sizeof(invalid));
      invalid[1] = raw_src & 0x7f;
      invalid[2] = (raw_src >> 3) & 0x30;
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         AGX_CHIP_G17G, invalid, sizeof(invalid), &decoded_src));
      CHECK(decoded_src == 0);
   }

   for (uint32_t size = 0; size < D12_AGX3_YIELD_I16_REG_LENGTH; ++size) {
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         AGX_CHIP_G15G, packed_b.bytes, size, &decoded_src));
      CHECK(decoded_src == 0);
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         AGX_CHIP_G17G, packed_c.bytes, size, &decoded_src));
      CHECK(decoded_src == 0);
   }

   uint8_t appended[D12_AGX3_YIELD_I16_REG_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   decoded_src = UINT16_MAX;
   CHECK(!d12_agx3_decode_yield_i16_reg(
      AGX_CHIP_G17G, appended, sizeof(appended), &decoded_src));
   CHECK(decoded_src == 0);

   /* These are known neighboring selections/context forms from the compiler
    * corpus.  The D3D bridge represents none of them. */
   static const uint8_t rejected[][D12_AGX3_YIELD_I16_REG_LENGTH] = {
      {0x87, 0x04, 0x54, 0x02, 0x00, 0x00}, /* AGX3b immediate */
      {0x87, 0x04, 0x54, 0x04, 0x02, 0x00}, /* AGX3b field3 */
      {0x87, 0x04, 0x54, 0x02, 0x06, 0x00}, /* AGX3b source kill */
      {0x87, 0x14, 0x54, 0x02, 0x04, 0x00}, /* AGX3b context */
      {0x07, 0x02, 0x00, 0x02, 0x00, 0x00}, /* AGX3c immediate */
      {0x17, 0x04, 0x00, 0x02, 0x00, 0x00}, /* AGX3c field3 */
      {0x0f, 0x02, 0x08, 0x02, 0x00, 0x00}, /* AGX3c source kill */
      {0x0f, 0x02, 0x00, 0x02, 0x00, 0x04}, /* AGX3c context */
      {0x0f, 0x00, 0x20, 0x02, 0x00, 0x00}, /* AGX3c f2[8] */
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         i < 4 ? AGX_CHIP_G15G : AGX_CHIP_G17G, rejected[i],
         sizeof(rejected[i]), &decoded_src));
      CHECK(decoded_src == 0);
   }

   /* A valid byte string from one encoding is not valid on the other. */
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         target->chip,
         target->encoding == AGX_ISA_ENCODING_AGX3B ? packed_c.bytes
                                                     : packed_b.bytes,
         D12_AGX3_YIELD_I16_REG_LENGTH, &decoded_src));
      CHECK(decoded_src == 0);
   }

   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0;
        i < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++i) {
      CHECK(check_yield_rejected_and_cleared(unsupported_targets[i], 0) ==
            0);
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         unsupported_targets[i], packed_b.bytes, packed_b.length,
         &decoded_src));
      CHECK(decoded_src == 0);
      decoded_src = UINT16_MAX;
      CHECK(!d12_agx3_decode_yield_i16_reg(
         unsupported_targets[i], packed_c.bytes, packed_c.length,
         &decoded_src));
      CHECK(decoded_src == 0);
   }

   CHECK(check_yield_rejected_and_cleared((enum agx_chip)99, 0) == 0);
   CHECK(check_yield_rejected_and_cleared(AGX_CHIP_G15G, 192) == 0);
   CHECK(check_yield_rejected_and_cleared(AGX_CHIP_G15G, UINT16_MAX) == 0);
   CHECK(check_yield_rejected_and_cleared(AGX_CHIP_G17G, 256) == 0);
   CHECK(check_yield_rejected_and_cleared(AGX_CHIP_G17G, UINT16_MAX) == 0);

   decoded_src = UINT16_MAX;
   CHECK(!d12_agx3_decode_yield_i16_reg(
      (enum agx_chip)99, packed_c.bytes, packed_c.length, &decoded_src));
   CHECK(decoded_src == 0);
   CHECK(!d12_agx3_encode_yield_i16_reg(AGX_CHIP_G17G, 0, NULL));
   decoded_src = UINT16_MAX;
   CHECK(!d12_agx3_decode_yield_i16_reg(
      AGX_CHIP_G17G, NULL, packed_c.length, &decoded_src));
   CHECK(decoded_src == 0);
   CHECK(!d12_agx3_decode_yield_i16_reg(
      AGX_CHIP_G17G, packed_c.bytes, packed_c.length, NULL));

   return 0;
}

static int
check_imul_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint16_t register_limit =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;

      /* Every compiler-witnessed G15--G17 target must preserve every modeled
       * even half-register independently in all three operand positions. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0},
            {0, value, 0},
            {0, 0, value},
         };

         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_imul_u32_reg_reg expected;
            struct d12_agx3_binary_alu_encoding encoded;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            uint8_t length = 0;

            CHECK(agx3_pack_imul_u32_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], &expected));
            CHECK(d12_agx3_encode_integer_multiply(
               target->chip, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
               operands[operand][0], operands[operand][1],
               operands[operand][2], &encoded));
            CHECK(encoded.length == expected.length);
            CHECK(memcmp(encoded.bytes, expected.bytes,
                         D12_AGX3_BINARY_ALU_MAX_LENGTH) == 0);
            CHECK(agx3_unpack_imul_u32_reg_reg(
               target->encoding, encoded.bytes, encoded.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
            CHECK(length == encoded.length);
         }
      }
   }

   return 0;
}

static int
check_imul_canonical_bit_domain(void)
{
   static const uint8_t operand_masks[][D12_AGX3_BINARY_ALU_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0, 0, 0},
      /* AGX3c: bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0, 0, 0},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };

   for (unsigned t = 0; t < 2; ++t) {
      struct d12_agx3_binary_alu_encoding encoded;
      CHECK(d12_agx3_encode_integer_multiply(
         representatives[t].chip, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
         0, 0, 0, &encoded));

      for (unsigned bit = 0; bit < encoded.length * 8; ++bit) {
         uint8_t corrupted[D12_AGX3_BINARY_ALU_MAX_LENGTH] = {0};
         memcpy(corrupted, encoded.bytes, encoded.length);
         corrupted[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_imul_u32_reg_reg(
            representatives[t].encoding, corrupted, encoded.length, &dest,
            &src0, &src1, &length);
         const bool is_operand =
            (operand_masks[t][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);
      }
   }

   return 0;
}

static int
check_imadd_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint16_t register_limit =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;

      /* Exhaust every modeled even half-register independently as the
       * destination, both multipliers, and the real addend on all nine
       * compiler-witnessed G15--G17 targets. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][4] = {
            {value, 0, 0, 0},
            {0, value, 0, 0},
            {0, 0, value, 0},
            {0, 0, 0, value},
         };

         for (unsigned operand = 0; operand < 4; ++operand) {
            struct agx3_encoded_imadd_u32_reg_reg_reg expected;
            struct d12_agx3_ternary_alu_encoding encoded;
            uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX;
            uint16_t mul1 = UINT16_MAX, addend = UINT16_MAX;
            uint8_t length = 0;

            CHECK(agx3_pack_imadd_u32_reg_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], operands[operand][3], &expected));
            CHECK(d12_agx3_encode_integer_multiply_add(
               target->chip, D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32,
               operands[operand][0], operands[operand][1],
               operands[operand][2], operands[operand][3], &encoded));
            CHECK(encoded.length == expected.length);
            CHECK(memcmp(encoded.bytes, expected.bytes,
                         D12_AGX3_TERNARY_ALU_MAX_LENGTH) == 0);
            CHECK(agx3_unpack_imadd_u32_reg_reg_reg(
               target->encoding, encoded.bytes, encoded.length, &dest, &mul0,
               &mul1, &addend, &length));
            CHECK(dest == operands[operand][0]);
            CHECK(mul0 == operands[operand][1]);
            CHECK(mul1 == operands[operand][2]);
            CHECK(addend == operands[operand][3]);
            CHECK(length == encoded.length);
         }
      }
   }

   return 0;
}

static int
check_imadd_canonical_bit_domain(void)
{
   static const uint8_t operand_masks[][D12_AGX3_TERNARY_ALU_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31/f46 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0xf3, 0x07, 0, 0, 0, 0, 0},
      /* AGX3c: bounded eight-bit forms of all four fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x93, 0x3f, 0x01, 0,
       0xae, 0},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };

   for (unsigned t = 0; t < 2; ++t) {
      struct d12_agx3_ternary_alu_encoding encoded;
      CHECK(d12_agx3_encode_integer_multiply_add(
         representatives[t].chip, D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32,
         0, 0, 0, 0, &encoded));

      /* Every fixed bit is rejected when corrupted. Operand-bit flips are
       * alternate canonical register choices and must re-pack identically. */
      for (unsigned bit = 0; bit < encoded.length * 8; ++bit) {
         uint8_t corrupted[D12_AGX3_TERNARY_ALU_MAX_LENGTH] = {0};
         memcpy(corrupted, encoded.bytes, encoded.length);
         corrupted[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, mul0, mul1, addend;
         uint8_t length;
         const bool accepted = agx3_unpack_imadd_u32_reg_reg_reg(
            representatives[t].encoding, corrupted, encoded.length, &dest,
            &mul0, &mul1, &addend, &length);
         const bool is_operand =
            (operand_masks[t][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            struct agx3_encoded_imadd_u32_reg_reg_reg repacked;
            CHECK(agx3_pack_imadd_u32_reg_reg_reg(
               representatives[t].encoding, dest, mul0, mul1, addend,
               &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, corrupted, length) == 0);
         }
      }
   }

   return 0;
}

static int
check_imadd_related_forms_rejected(void)
{
   static const uint8_t implicit_imul_b[] = {
      0x9f, 0x00, 0x56, 0x00, 0x02, 0x00,
      0x00, 0x00, 0xd0, 0x20, 0x0a, 0x00,
   };
   static const uint8_t alternate_p3_b[] = {
      0x9f, 0x00, 0x56, 0x00, 0x02, 0x08,
      0x00, 0x40, 0xd0, 0x2f, 0x2a, 0x00,
   };
   static const uint8_t alternate_q_b[] = {
      0x9f, 0x00, 0x56, 0x00, 0x02, 0x08,
      0x00, 0x10, 0xd0, 0x2f, 0x2a, 0x00,
   };
   static const uint8_t high_i32_b[] = {
      0x9f, 0x00, 0x56, 0x00, 0x03, 0x00,
      0x08, 0x00, 0xe0, 0x26, 0x1e, 0x00,
   };
   static const uint8_t high_u32_b[] = {
      0x9f, 0x00, 0x56, 0x00, 0x03, 0x00,
      0x08, 0x00, 0xe0, 0x26, 0x0a, 0x00,
   };
   static const uint8_t implicit_imul_c[] = {
      0x2f, 0x00, 0x04, 0x1a, 0x21, 0x00, 0xa1,
      0x02, 0x08, 0x80, 0x00, 0x00, 0x01, 0x02,
   };
   static const uint8_t alternate_p3_c[] = {
      0x2f, 0x00, 0x04, 0x3a, 0x21, 0x00, 0xa1,
      0x02, 0x28, 0x80, 0xe0, 0x00, 0x08, 0x00,
   };
   static const uint8_t alternate_q_c[] = {
      0x2f, 0x00, 0x04, 0x3a, 0x21, 0x00, 0xa1,
      0x02, 0x28, 0x80, 0xe0, 0x00, 0x02, 0x00,
   };
   /* High-half forms are shorter and padded only to exercise canonical
    * rejection at the imadd decoder's required input size. */
   static const uint8_t high_i32_c[D12_AGX3_TERNARY_ALU_MAX_LENGTH] = {
      0x2f, 0x00, 0x04, 0x02, 0x29, 0x08,
      0xa9, 0x02, 0xa8, 0x80, 0x20, 0xb0,
   };
   static const uint8_t high_u32_c[D12_AGX3_TERNARY_ALU_MAX_LENGTH] = {
      0x2f, 0x00, 0x04, 0x02, 0x29, 0x00,
      0xa9, 0x02, 0xa8, 0x80, 0x20, 0x30,
   };
   static const struct {
      enum agx_isa_encoding encoding;
      const uint8_t *bytes;
      size_t length;
   } related[] = {
      {AGX_ISA_ENCODING_AGX3B, implicit_imul_b, sizeof(implicit_imul_b)},
      {AGX_ISA_ENCODING_AGX3B, alternate_p3_b, sizeof(alternate_p3_b)},
      {AGX_ISA_ENCODING_AGX3B, alternate_q_b, sizeof(alternate_q_b)},
      {AGX_ISA_ENCODING_AGX3B, high_i32_b, sizeof(high_i32_b)},
      {AGX_ISA_ENCODING_AGX3B, high_u32_b, sizeof(high_u32_b)},
      {AGX_ISA_ENCODING_AGX3C, implicit_imul_c, sizeof(implicit_imul_c)},
      {AGX_ISA_ENCODING_AGX3C, alternate_p3_c, sizeof(alternate_p3_c)},
      {AGX_ISA_ENCODING_AGX3C, alternate_q_c, sizeof(alternate_q_c)},
      {AGX_ISA_ENCODING_AGX3C, high_i32_c, sizeof(high_i32_c)},
      {AGX_ISA_ENCODING_AGX3C, high_u32_c, sizeof(high_u32_c)},
   };

   for (unsigned i = 0; i < sizeof(related) / sizeof(related[0]); ++i) {
      uint16_t dest, mul0, mul1, addend;
      uint8_t length;
      CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
         related[i].encoding, related[i].bytes, related[i].length, &dest,
         &mul0, &mul1, &addend, &length));
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

static bool
ternary_encoding_is_cleared(
   const struct d12_agx3_ternary_alu_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_TERNARY_ALU_MAX_LENGTH; ++i) {
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

static int
check_imul_rejected_and_cleared(
   enum agx_chip chip, enum d12_agx3_integer_multiply_selection selection,
   uint16_t dest, uint16_t src0, uint16_t src1)
{
   struct d12_agx3_binary_alu_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_integer_multiply(chip, selection, dest, src0, src1,
                                           &encoded));
   CHECK(binary_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_ternary_rejected_and_cleared(enum agx_isa_encoding encoding,
                                   enum d12_agx3_ternary_alu_op op,
                                   uint16_t dest, uint16_t src0,
                                   uint16_t src1, uint16_t src2)
{
   struct d12_agx3_ternary_alu_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_ternary_alu(encoding, op, dest, src0, src1, src2,
                                      &encoded));
   CHECK(ternary_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_imadd_rejected_and_cleared(
   enum agx_chip chip,
   enum d12_agx3_integer_multiply_add_selection selection, uint16_t dest,
   uint16_t mul0, uint16_t mul1, uint16_t addend)
{
   struct d12_agx3_ternary_alu_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_integer_multiply_add(
      chip, selection, dest, mul0, mul1, addend, &encoded));
   CHECK(ternary_encoding_is_cleared(&encoded));
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
   static const uint8_t ilshr_b[] = {
      0xa7, 0x10, 0x54, 0x06, 0x02, 0x14,
      0x00, 0x00, 0xf0, 0x10, 0x05, 0x00,
   };
   static const uint8_t ilshr_c[] = {
      0x37, 0x06, 0x00, 0x5a, 0x22, 0x00, 0xa1,
      0x0a, 0x08, 0x80, 0x00, 0x34, 0x00, 0x00,
   };
   static const uint8_t imul_b[] = {
      0x9f, 0x00, 0x56, 0x06, 0x02, 0x00,
      0x10, 0x00, 0xd0, 0x20, 0x0a, 0x00,
   };
   static const uint8_t imul_c[] = {
      0x3f, 0x00, 0x04, 0x1a, 0x21, 0x00, 0xa1,
      0x0a, 0x08, 0x81, 0x00, 0x00, 0x01, 0x02,
   };
   static const uint8_t imadd_b[] = {
      0x9f, 0x00, 0x56, 0x0a, 0x02, 0x08,
      0x18, 0x00, 0xd0, 0x21, 0x2a, 0x00,
   };
   static const uint8_t imadd_c[] = {
      0x3f, 0x00, 0x04, 0x3a, 0x21, 0x00, 0xa1,
      0x12, 0x88, 0x81, 0x40, 0x00, 0x00, 0x00,
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
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_ILSHR_U32, 6, 10, 0, ilshr_b,
                        sizeof(ilshr_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_ILSHR_U32, 6, 10, 0, ilshr_c,
                        sizeof(ilshr_c)) == 0);
   CHECK(check_ilshr_register_domain() == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3B,
                        D12_AGX3_BINARY_ALU_IMUL_U32, 6, 0, 4, imul_b,
                        sizeof(imul_b)) == 0);
   CHECK(check_encoding(AGX_ISA_ENCODING_AGX3C,
                        D12_AGX3_BINARY_ALU_IMUL_U32, 6, 0, 4, imul_c,
                        sizeof(imul_c)) == 0);
   CHECK(check_imul_register_domain() == 0);
   CHECK(check_imul_canonical_bit_domain() == 0);
   CHECK(check_ternary_encoding(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_TERNARY_ALU_IMADD_U32,
            10, 4, 6, 0, imadd_b, sizeof(imadd_b)) == 0);
   CHECK(check_ternary_encoding(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_TERNARY_ALU_IMADD_U32,
            10, 4, 6, 0, imadd_c, sizeof(imadd_c)) == 0);
   CHECK(check_target_imadd_encoding(AGX_CHIP_G15S, 10, 4, 6, 0, imadd_b,
                                     sizeof(imadd_b)) == 0);
   CHECK(check_target_imadd_encoding(AGX_CHIP_G17S, 10, 4, 6, 0, imadd_c,
                                     sizeof(imadd_c)) == 0);
   CHECK(check_imadd_register_domain() == 0);
   CHECK(check_imadd_canonical_bit_domain() == 0);
   CHECK(check_imadd_related_forms_rejected() == 0);
   CHECK(check_yield_exact_witnesses() == 0);
   CHECK(check_yield_register_domain() == 0);
   CHECK(check_yield_canonical_bit_domain() == 0);
   CHECK(check_yield_rejection_domain() == 0);

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
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX2, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_UNKNOWN, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_ILSHR_U32,
            1, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 1, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 0, 1) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_ILSHR_U32,
            1, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 1, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 0, 1) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_ILSHR_U32,
            192, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 192, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 0, 192) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_ILSHR_U32,
            256, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 256, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_ILSHR_U32,
            0, 0, 256) == 0);
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_ILSHR_U32,
                                     0, 0, 0, NULL));
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX2, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_UNKNOWN, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IMUL_U32,
            1, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 1, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 0, 1) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IMUL_U32,
            1, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 1, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 0, 1) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IMUL_U32,
            192, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 192, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3B, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 0, 192) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IMUL_U32,
            256, 0, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 256, 0) == 0);
   CHECK(check_binary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, D12_AGX3_BINARY_ALU_IMUL_U32,
            0, 0, 256) == 0);
   CHECK(!d12_agx3_encode_binary_alu(AGX_ISA_ENCODING_AGX3C,
                                     D12_AGX3_BINARY_ALU_IMUL_U32,
                                     0, 0, 0, NULL));

   static const enum agx_chip unsupported_integer_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0;
        i < sizeof(unsupported_integer_targets) /
               sizeof(unsupported_integer_targets[0]);
        ++i) {
      CHECK(check_imul_rejected_and_cleared(
               unsupported_integer_targets[i],
               D12_AGX3_INTEGER_MULTIPLY_LOW_U32, 0, 0, 0) == 0);
   }

   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G17S, D12_AGX3_INTEGER_MULTIPLY_WITH_ADDEND_U32,
            0, 0, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G17S, D12_AGX3_INTEGER_MULTIPLY_HIGH_I32,
            0, 0, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G17S, D12_AGX3_INTEGER_MULTIPLY_HIGH_U32,
            0, 0, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G17S,
            (enum d12_agx3_integer_multiply_selection)99,
            0, 0, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G15G, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
            1, 0, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G15G, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
            0, 1, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G15G, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
            0, 0, 1) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G15G, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
            192, 0, 0) == 0);
   CHECK(check_imul_rejected_and_cleared(
            AGX_CHIP_G17S, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
            0, 256, 0) == 0);
   CHECK(!d12_agx3_encode_integer_multiply(
      AGX_CHIP_G17S, D12_AGX3_INTEGER_MULTIPLY_LOW_U32,
      0, 0, 0, NULL));

   CHECK(check_ternary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX2, D12_AGX3_TERNARY_ALU_IMADD_U32,
            0, 0, 0, 0) == 0);
   CHECK(check_ternary_rejected_and_cleared(
            AGX_ISA_ENCODING_UNKNOWN, D12_AGX3_TERNARY_ALU_IMADD_U32,
            0, 0, 0, 0) == 0);
   CHECK(check_ternary_rejected_and_cleared(
            AGX_ISA_ENCODING_AGX3C, (enum d12_agx3_ternary_alu_op)99,
            0, 0, 0, 0) == 0);

   static const enum agx_isa_encoding imadd_encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };
   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t limit = e == 0 ? 192 : 256;
      for (unsigned operand = 0; operand < 4; ++operand) {
         uint16_t odd[4] = {0};
         odd[operand] = 1;
         CHECK(check_ternary_rejected_and_cleared(
                  imadd_encodings[e], D12_AGX3_TERNARY_ALU_IMADD_U32,
                  odd[0], odd[1], odd[2], odd[3]) == 0);

         uint16_t oob[4] = {0};
         oob[operand] = limit;
         CHECK(check_ternary_rejected_and_cleared(
                  imadd_encodings[e], D12_AGX3_TERNARY_ALU_IMADD_U32,
                  oob[0], oob[1], oob[2], oob[3]) == 0);
      }
   }
   CHECK(!d12_agx3_encode_ternary_alu(
      AGX_ISA_ENCODING_AGX3C, D12_AGX3_TERNARY_ALU_IMADD_U32,
      0, 0, 0, 0, NULL));

   for (unsigned i = 0;
        i < sizeof(unsupported_integer_targets) /
               sizeof(unsupported_integer_targets[0]);
        ++i) {
      CHECK(check_imadd_rejected_and_cleared(
               unsupported_integer_targets[i],
               D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32, 0, 0, 0, 0) == 0);
   }

   static const enum d12_agx3_integer_multiply_add_selection
      unsupported_imadd_selections[] = {
         D12_AGX3_INTEGER_MULTIPLY_ADD_IMPLICIT_ZERO_IMUL,
         D12_AGX3_INTEGER_MULTIPLY_ADD_ALTERNATE_P3_LOW_U32,
         D12_AGX3_INTEGER_MULTIPLY_ADD_ALTERNATE_Q_LOW_U32,
         D12_AGX3_INTEGER_MULTIPLY_ADD_HIGH_I32,
         D12_AGX3_INTEGER_MULTIPLY_ADD_HIGH_U32,
         (enum d12_agx3_integer_multiply_add_selection)99,
      };
   for (unsigned i = 0;
        i < sizeof(unsupported_imadd_selections) /
               sizeof(unsupported_imadd_selections[0]);
        ++i) {
      CHECK(check_imadd_rejected_and_cleared(
               AGX_CHIP_G17S, unsupported_imadd_selections[i],
               0, 0, 0, 0) == 0);
   }

   for (unsigned t = 0; t < 2; ++t) {
      const enum agx_chip chip = t == 0 ? AGX_CHIP_G15G : AGX_CHIP_G17S;
      const uint16_t limit = t == 0 ? 192 : 256;
      for (unsigned operand = 0; operand < 4; ++operand) {
         uint16_t odd[4] = {0};
         odd[operand] = 1;
         CHECK(check_imadd_rejected_and_cleared(
                  chip, D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32,
                  odd[0], odd[1], odd[2], odd[3]) == 0);

         uint16_t oob[4] = {0};
         oob[operand] = limit;
         CHECK(check_imadd_rejected_and_cleared(
                  chip, D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32,
                  oob[0], oob[1], oob[2], oob[3]) == 0);
      }
   }
   CHECK(!d12_agx3_encode_integer_multiply_add(
      AGX_CHIP_G17S, D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32,
      0, 0, 0, 0, NULL));
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
