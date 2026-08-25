/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_isa_core.h"

#include "asahi/compiler/agx3_cf_core.h"
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

static const struct integer_target fmul_targets[] = {
   {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G15D, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G16P, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G16G, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G16S, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G17P, AGX_ISA_ENCODING_AGX3B},
   {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
};

static const struct integer_target fmul_long_targets[] = {
   {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C},
   {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
};

static bool
fadd_encoding_is_cleared(
   const struct d12_agx3_fadd_f32_reg_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_FADD_F32_REG_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_fadd_rejected_and_cleared(enum agx_chip chip,
                                enum agx_isa_encoding encoding,
                                uint16_t dest, uint16_t src0, uint16_t src1)
{
   struct d12_agx3_fadd_f32_reg_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_fadd_f32_reg_reg(
      chip, encoding, dest, src0, src1, &encoded));
   CHECK(fadd_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_fadd_decode_rejected_and_cleared(enum agx_chip chip,
                                       enum agx_isa_encoding encoding,
                                       const void *instruction,
                                       uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fadd_f32_reg_reg(
      chip, encoding, instruction, instruction_size, &dest, &src0, &src1));
   CHECK(dest == 0);
   CHECK(src0 == 0);
   CHECK(src1 == 0);
   return 0;
}

static int
check_fadd_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src0;
      uint16_t src1;
      uint8_t bytes[D12_AGX3_FADD_F32_REG_REG_LENGTH];
   } vectors[] = {
      {4, 2, 0, {0x29, 0x01, 0x04, 0x03, 0x00, 0x20}},
      {16, 10, 2, {0x89, 0x03, 0x04, 0x0b, 0x00, 0x20}},
      {128, 126, 124, {0x09, 0x7d, 0x04, 0x7f, 0x00, 0x30}},
      {132, 130, 128, {0x29, 0x01, 0x04, 0x03, 0x00, 0x35}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];

      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         struct agx3_encoded_fadd_f32_reg_reg shared;
         struct d12_agx3_fadd_f32_reg_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

         CHECK(agx3_pack_fadd_f32_reg_reg(
            target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &shared));
         CHECK(shared.length == sizeof(vectors[v].bytes));
         CHECK(memcmp(shared.bytes, vectors[v].bytes,
                      sizeof(vectors[v].bytes)) == 0);
         CHECK(d12_agx3_encode_fadd_f32_reg_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &encoded));
         CHECK(encoded.length == sizeof(vectors[v].bytes));
         CHECK(memcmp(encoded.bytes, vectors[v].bytes,
                      sizeof(vectors[v].bytes)) == 0);
         CHECK(d12_agx3_decode_fadd_f32_reg_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src0, &src1));
         CHECK(dest == vectors[v].dest);
         CHECK(src0 == vectors[v].src0);
         CHECK(src1 == vectors[v].src1);
      }
   }

   /* The bridge preserves physical source fields; it does not canonicalize
    * commutative source order. */
   struct d12_agx3_fadd_f32_reg_reg_encoding forward, reversed;
   CHECK(d12_agx3_encode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 16, 10, 2, &forward));
   CHECK(d12_agx3_encode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 16, 2, 10, &reversed));
   CHECK(memcmp(forward.bytes, reversed.bytes, forward.length) != 0);

   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(d12_agx3_decode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, reversed.bytes, reversed.length,
      &dest, &src0, &src1));
   CHECK(dest == 16);
   CHECK(src0 == 2);
   CHECK(src1 == 10);
   return 0;
}

static int
check_fadd_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];

      for (uint16_t value = 0; value < 256; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };

         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fadd_f32_reg_reg shared;
            struct d12_agx3_fadd_f32_reg_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

            CHECK(agx3_pack_fadd_f32_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], &shared));
            CHECK(d12_agx3_encode_fadd_f32_reg_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], operands[operand][2], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_fadd_f32_reg_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src0, &src1));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
         }
      }
   }

   return 0;
}

static int
check_fadd_canonical_bit_domain(void)
{
   static const uint8_t operand_mask[D12_AGX3_FADD_F32_REG_REG_LENGTH] = {
      0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x15,
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][3] = {
      {0, 0, 0}, {254, 254, 254},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_fadd_f32_reg_reg_encoding canonical;
         CHECK(d12_agx3_encode_fadd_f32_reg_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], states[state][2],
            &canonical));

         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_FADD_F32_REG_REG_LENGTH];
            memcpy(mutated, canonical.bytes, sizeof(mutated));
            mutated[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            const bool accepted = d12_agx3_decode_fadd_f32_reg_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, sizeof(mutated), &dest, &src0, &src1);
            const bool is_operand =
               (operand_mask[bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               struct d12_agx3_fadd_f32_reg_reg_encoding repacked;
               CHECK(d12_agx3_encode_fadd_f32_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src0, src1, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, sizeof(mutated)) == 0);
            } else {
               CHECK(dest == 0);
               CHECK(src0 == 0);
               CHECK(src1 == 0);
            }
         }
      }
   }

   return 0;
}

static int
check_fadd_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2,
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
      AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical[D12_AGX3_FADD_F32_REG_REG_LENGTH] = {
      0x09, 0x01, 0x04, 0x01, 0x00, 0x20,
   };

   /* Every supported chip rejects the other table and all non-AGX3 values. */
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;

         CHECK(check_fadd_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0, 0) == 0);
         CHECK(check_fadd_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  sizeof(canonical)) == 0);
      }
   }

   /* Unsupported chips remain rejected regardless of the requested table. */
   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_fadd_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0, 0) == 0);
         CHECK(check_fadd_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical,
                  sizeof(canonical)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned operand = 0; operand < 3; ++operand) {
         uint16_t odd[3] = {0};
         odd[operand] = 1;
         CHECK(check_fadd_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  odd[0], odd[1], odd[2]) == 0);

         odd[operand] = 255;
         CHECK(check_fadd_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  odd[0], odd[1], odd[2]) == 0);

         uint16_t oob[3] = {0};
         oob[operand] = 256;
         CHECK(check_fadd_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  oob[0], oob[1], oob[2]) == 0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical); ++size) {
      CHECK(check_fadd_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical, size) == 0);
      CHECK(check_fadd_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical, size) == 0);
   }

   uint8_t appended[D12_AGX3_FADD_F32_REG_REG_LENGTH + 1];
   memcpy(appended, canonical, sizeof(canonical));
   appended[sizeof(canonical)] = 0xff;
   CHECK(check_fadd_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);

   static const uint8_t unsupported_forms[][D12_AGX3_FADD_F32_REG_REG_LENGTH] = {
      {0x89, 0x03, 0x04, 0x0b, 0x00, 0xc0}, /* wait/context selection */
      {0x19, 0x03, 0x0c, 0x01, 0x00, 0x20}, /* alternate f32add */
      {0x09, 0x05, 0x1d, 0x01, 0x00, 0xc0}, /* f32mul */
   };
   for (unsigned i = 0;
        i < sizeof(unsupported_forms) / sizeof(unsupported_forms[0]); ++i) {
      CHECK(check_fadd_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B,
               unsupported_forms[i], sizeof(unsupported_forms[i])) == 0);
      CHECK(check_fadd_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C,
               unsupported_forms[i], sizeof(unsupported_forms[i])) == 0);
   }

   CHECK(!d12_agx3_encode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, 0, NULL));
   CHECK(check_fadd_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical)) == 0);

   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical, sizeof(canonical),
      NULL, &src0, &src1));
   CHECK(src0 == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical, sizeof(canonical),
      &dest, NULL, &src1));
   CHECK(dest == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fadd_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical, sizeof(canonical),
      &dest, &src0, NULL));
   CHECK(dest == 0 && src0 == 0);
   return 0;
}

static bool
fmul_encoding_is_cleared(
   const struct d12_agx3_fmul_f32_reg_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_FMUL_F32_REG_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_fmul_rejected_and_cleared(enum agx_chip chip,
                                enum agx_isa_encoding encoding,
                                uint16_t dest, uint16_t src0, uint16_t src1)
{
   struct d12_agx3_fmul_f32_reg_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_fmul_f32_reg_reg(
      chip, encoding, dest, src0, src1, &encoded));
   CHECK(fmul_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_fmul_decode_rejected_and_cleared(enum agx_chip chip,
                                       enum agx_isa_encoding encoding,
                                       const void *instruction,
                                       uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg(
      chip, encoding, instruction, instruction_size, &dest, &src0, &src1));
   CHECK(dest == 0);
   CHECK(src0 == 0);
   CHECK(src1 == 0);
   return 0;
}

static int
check_fmul_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src0;
      uint16_t src1;
      uint8_t agx3b[D12_AGX3_FMUL_F32_REG_REG_LENGTH];
      uint8_t agx3c[D12_AGX3_FMUL_F32_REG_REG_LENGTH];
   } vectors[] = {
      {0, 0, 2, {0x09, 0x03, 0x1d, 0x01, 0x00, 0xc0},
                {0x09, 0x03, 0x1d, 0x01, 0x00, 0x20}},
      {0, 0, 4, {0x09, 0x05, 0x1d, 0x01, 0x00, 0xc0},
                {0x09, 0x05, 0x1d, 0x01, 0x00, 0x20}},
      {16, 10, 2, {0x89, 0x03, 0x1d, 0x0b, 0x00, 0xc0},
                  {0x89, 0x03, 0x1d, 0x0b, 0x00, 0x20}},
      {126, 126, 126, {0xf9, 0x7f, 0xdd, 0x7f, 0x00, 0xc0},
                      {0xf9, 0x7f, 0xdd, 0x7f, 0x00, 0x20}},
   };

   for (unsigned t = 0;
        t < sizeof(fmul_targets) / sizeof(fmul_targets[0]); ++t) {
      const struct integer_target *target = &fmul_targets[t];

      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         struct agx3_encoded_fmul_f32_reg_reg shared;
         struct d12_agx3_fmul_f32_reg_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

         CHECK(agx3_pack_fmul_f32_reg_reg(
            target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &shared));
         CHECK(shared.length == D12_AGX3_FMUL_F32_REG_REG_LENGTH);
         CHECK(memcmp(shared.bytes, expected, shared.length) == 0);
         CHECK(d12_agx3_encode_fmul_f32_reg_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, encoded.length) == 0);
         CHECK(d12_agx3_decode_fmul_f32_reg_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src0, &src1));
         CHECK(dest == vectors[v].dest);
         CHECK(src0 == vectors[v].src0);
         CHECK(src1 == vectors[v].src1);
      }
   }

   return 0;
}

static int
check_fmul_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(fmul_targets) / sizeof(fmul_targets[0]); ++t) {
      const struct integer_target *target = &fmul_targets[t];

      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };

         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fmul_f32_reg_reg shared;
            struct d12_agx3_fmul_f32_reg_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

            CHECK(agx3_pack_fmul_f32_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], &shared));
            CHECK(d12_agx3_encode_fmul_f32_reg_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], operands[operand][2], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_fmul_f32_reg_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src0, &src1));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
         }
      }
   }

   return 0;
}

static int
check_fmul_canonical_bit_domain(void)
{
   static const uint8_t operand_mask[D12_AGX3_FMUL_F32_REG_REG_LENGTH] = {
      0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x00,
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_fmul_f32_reg_reg_encoding canonical;
         CHECK(d12_agx3_encode_fmul_f32_reg_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], states[state][2],
            &canonical));

         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_FMUL_F32_REG_REG_LENGTH];
            memcpy(mutated, canonical.bytes, sizeof(mutated));
            mutated[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            const bool accepted = d12_agx3_decode_fmul_f32_reg_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, sizeof(mutated), &dest, &src0, &src1);
            const bool is_operand =
               (operand_mask[bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               struct d12_agx3_fmul_f32_reg_reg_encoding repacked;
               CHECK(d12_agx3_encode_fmul_f32_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src0, src1, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, sizeof(mutated)) == 0);
            } else {
               CHECK(dest == 0);
               CHECK(src0 == 0);
               CHECK(src1 == 0);
            }
         }
      }
   }

   return 0;
}

static int
check_fmul_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2,
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
      AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17G, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[D12_AGX3_FMUL_F32_REG_REG_LENGTH] = {
      0x09, 0x03, 0x1d, 0x01, 0x00, 0xc0,
   };
   static const uint8_t canonical_c[D12_AGX3_FMUL_F32_REG_REG_LENGTH] = {
      0x09, 0x03, 0x1d, 0x01, 0x00, 0x20,
   };

   for (unsigned t = 0;
        t < sizeof(fmul_targets) / sizeof(fmul_targets[0]); ++t) {
      const struct integer_target *target = &fmul_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;

      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_fmul_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0, 0) == 0);
         CHECK(check_fmul_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  D12_AGX3_FMUL_F32_REG_REG_LENGTH) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_fmul_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0, 0) == 0);
         CHECK(check_fmul_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         for (unsigned operand = 0; operand < 3; ++operand) {
            uint16_t args[3] = {0};
            args[operand] = invalid_values[value];
            CHECK(check_fmul_rejected_and_cleared(
                     representatives[r].chip, representatives[r].encoding,
                     args[0], args[1], args[2]) == 0);
         }
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size) {
      CHECK(check_fmul_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
      CHECK(check_fmul_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);
   }

   uint8_t appended[D12_AGX3_FMUL_F32_REG_REG_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);

   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t alternate_b[] = {
      0x19, 0x03, 0x0d, 0x01, 0x00, 0xc0,
   };
   static const uint8_t alternate_c[] = {
      0x19, 0x03, 0x0d, 0x01, 0x00, 0x20,
   };
   static const uint8_t long_c[] = {
      0x29, 0x00, 0x05, 0x30, 0x22, 0x80, 0xa0,
      0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10,
   };
   static const uint8_t fadd_c[] = {
      0x09, 0x01, 0x04, 0x01, 0x00, 0x20,
   };
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, alternate_b,
            sizeof(alternate_b)) == 0);
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, alternate_c,
            sizeof(alternate_c)) == 0);
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, long_c,
            sizeof(long_c)) == 0);
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, fadd_c,
            sizeof(fadd_c)) == 0);

   CHECK(!d12_agx3_encode_fmul_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, 0, NULL));
   CHECK(check_fmul_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src0, &src1));
   CHECK(src0 == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL, &src1));
   CHECK(dest == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, &src0, NULL));
   CHECK(dest == 0 && src0 == 0);
   return 0;
}

static bool
fmul_long_encoding_is_cleared(
   const struct d12_agx3_fmul_f32_reg_reg_long_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_fmul_long_rejected_and_cleared(enum agx_chip chip,
                                     enum agx_isa_encoding encoding,
                                     uint16_t dest, uint16_t src0,
                                     uint16_t src1)
{
   struct d12_agx3_fmul_f32_reg_reg_long_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_fmul_f32_reg_reg_long(
      chip, encoding, dest, src0, src1, &encoded));
   CHECK(fmul_long_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_fmul_long_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg_long(
      chip, encoding, instruction, instruction_size, &dest, &src0, &src1));
   CHECK(dest == 0);
   CHECK(src0 == 0);
   CHECK(src1 == 0);
   return 0;
}

static int
check_fmul_long_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src0;
      uint16_t src1;
      uint8_t bytes[D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH];
   } vectors[] = {
      {0, 0, 2,
       {0x29, 0x02, 0x05, 0x10, 0x22, 0x80, 0xa0,
        0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10}},
      {0, 0, 4,
       {0x29, 0x00, 0x05, 0x30, 0x22, 0x80, 0xa0,
        0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10}},
      {16, 10, 2,
       {0xa9, 0x02, 0x05, 0x10, 0x22, 0x80, 0xa0,
        0x02, 0x9c, 0x02, 0x20, 0x00, 0x00, 0x10}},
      {126, 126, 126,
       {0xb9, 0x02, 0x85, 0xf0, 0x22, 0x80, 0xa0,
        0x3a, 0x9d, 0x1f, 0x20, 0x00, 0x02, 0x10}},
   };

   for (unsigned t = 0;
        t < sizeof(fmul_long_targets) / sizeof(fmul_long_targets[0]); ++t) {
      const struct integer_target *target = &fmul_long_targets[t];

      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         struct agx3_encoded_fmul_f32_reg_reg_long shared;
         struct d12_agx3_fmul_f32_reg_reg_long_encoding encoded;
         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

         CHECK(agx3_pack_fmul_f32_reg_reg_long(
            target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &shared));
         CHECK(shared.length == sizeof(vectors[v].bytes));
         CHECK(memcmp(shared.bytes, vectors[v].bytes,
                      sizeof(vectors[v].bytes)) == 0);
         CHECK(d12_agx3_encode_fmul_f32_reg_reg_long(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, vectors[v].bytes,
                      sizeof(vectors[v].bytes)) == 0);
         CHECK(d12_agx3_decode_fmul_f32_reg_reg_long(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src0, &src1));
         CHECK(dest == vectors[v].dest);
         CHECK(src0 == vectors[v].src0);
         CHECK(src1 == vectors[v].src1);
      }
   }

   return 0;
}

static int
check_fmul_long_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(fmul_long_targets) / sizeof(fmul_long_targets[0]); ++t) {
      const struct integer_target *target = &fmul_long_targets[t];

      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };

         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fmul_f32_reg_reg_long shared;
            struct d12_agx3_fmul_f32_reg_reg_long_encoding encoded;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

            CHECK(agx3_pack_fmul_f32_reg_reg_long(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], &shared));
            CHECK(d12_agx3_encode_fmul_f32_reg_reg_long(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], operands[operand][2], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_fmul_f32_reg_reg_long(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src0, &src1));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
         }
      }
   }

   return 0;
}

static int
check_fmul_long_canonical_bit_domain(void)
{
   static const uint8_t operand_mask[D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH] = {
      0x90, 0x02, 0x80, 0xe0, 0x00, 0x00, 0x00,
      0x38, 0x81, 0x1f, 0x00, 0x00, 0x02, 0x00,
   };
   static const uint16_t states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned t = 0;
        t < sizeof(fmul_long_targets) / sizeof(fmul_long_targets[0]); ++t) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_fmul_f32_reg_reg_long_encoding canonical;
         CHECK(d12_agx3_encode_fmul_f32_reg_reg_long(
            fmul_long_targets[t].chip, fmul_long_targets[t].encoding,
            states[state][0], states[state][1], states[state][2],
            &canonical));

         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH];
            memcpy(mutated, canonical.bytes, sizeof(mutated));
            mutated[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            const bool accepted = d12_agx3_decode_fmul_f32_reg_reg_long(
               fmul_long_targets[t].chip, fmul_long_targets[t].encoding,
               mutated, sizeof(mutated), &dest, &src0, &src1);
            const bool is_operand =
               (operand_mask[bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               struct d12_agx3_fmul_f32_reg_reg_long_encoding repacked;
               CHECK(d12_agx3_encode_fmul_f32_reg_reg_long(
                  fmul_long_targets[t].chip, fmul_long_targets[t].encoding,
                  dest, src0, src1, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, sizeof(mutated)) == 0);
            } else {
               CHECK(dest == 0);
               CHECK(src0 == 0);
               CHECK(src1 == 0);
            }
         }
      }
   }

   return 0;
}

static int
check_fmul_long_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2,
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
      AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C,
      AGX_CHIP_G15D, AGX_CHIP_G16P, AGX_CHIP_G16G, AGX_CHIP_G16S,
      AGX_CHIP_G16C, AGX_CHIP_G17P, AGX_CHIP_G17A, AGX_CHIP_G17C,
      AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical[D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH] = {
      0x29, 0x02, 0x05, 0x10, 0x22, 0x80, 0xa0,
      0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10,
   };

   for (unsigned t = 0;
        t < sizeof(fmul_long_targets) / sizeof(fmul_long_targets[0]); ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == AGX_ISA_ENCODING_AGX3C)
            continue;
         CHECK(check_fmul_long_rejected_and_cleared(
                  fmul_long_targets[t].chip, all_encodings[e], 0, 0, 2) == 0);
         CHECK(check_fmul_long_decode_rejected_and_cleared(
                  fmul_long_targets[t].chip, all_encodings[e], canonical,
                  sizeof(canonical)) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_fmul_long_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0, 2) == 0);
         CHECK(check_fmul_long_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical,
                  sizeof(canonical)) == 0);
      }
   }

   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned t = 0;
        t < sizeof(fmul_long_targets) / sizeof(fmul_long_targets[0]); ++t) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         for (unsigned operand = 0; operand < 3; ++operand) {
            uint16_t args[3] = {0};
            args[operand] = invalid_values[value];
            CHECK(check_fmul_long_rejected_and_cleared(
                     fmul_long_targets[t].chip,
                     fmul_long_targets[t].encoding, args[0], args[1],
                     args[2]) == 0);
         }
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical); ++size) {
      CHECK(check_fmul_long_decode_rejected_and_cleared(
               AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, canonical, size) == 0);
      CHECK(check_fmul_long_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical, size) == 0);
   }

   uint8_t appended[D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH + 1];
   memcpy(appended, canonical, sizeof(canonical));
   appended[sizeof(canonical)] = 0xff;
   CHECK(check_fmul_long_decode_rejected_and_cleared(
            AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);

   static const uint8_t alternate_context[] = {
      0x39, 0x02, 0x05, 0x10, 0x22, 0x80, 0xa0,
      0x02, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x10,
   };
   static const uint8_t compact[] = {
      0x09, 0x03, 0x1d, 0x01, 0x00, 0x20,
   };
   CHECK(check_fmul_long_decode_rejected_and_cleared(
            AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, alternate_context,
            sizeof(alternate_context)) == 0);
   CHECK(check_fmul_long_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, compact,
            sizeof(compact)) == 0);

   CHECK(!d12_agx3_encode_fmul_f32_reg_reg_long(
      AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, 0, 0, 2, NULL));
   CHECK(check_fmul_long_decode_rejected_and_cleared(
            AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical)) == 0);

   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg_long(
      AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, canonical, sizeof(canonical),
      NULL, &src0, &src1));
   CHECK(src0 == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg_long(
      AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, canonical, sizeof(canonical),
      &dest, NULL, &src1));
   CHECK(dest == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmul_f32_reg_reg_long(
      AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, canonical, sizeof(canonical),
      &dest, &src0, NULL));
   CHECK(dest == 0 && src0 == 0);
   return 0;
}

static bool
fma_encoding_is_cleared(
   const struct d12_agx3_fma_f32_reg_reg_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_fma_rejected_and_cleared(enum agx_chip chip,
                               enum agx_isa_encoding encoding,
                               uint16_t dest, uint16_t mul0, uint16_t mul1,
                               uint16_t addend)
{
   struct d12_agx3_fma_f32_reg_reg_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_fma_f32_reg_reg_reg(
      chip, encoding, dest, mul0, mul1, addend, &encoded));
   CHECK(fma_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_fma_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
   uint16_t addend = UINT16_MAX;
   CHECK(!d12_agx3_decode_fma_f32_reg_reg_reg(
      chip, encoding, instruction, instruction_size, &dest, &mul0, &mul1,
      &addend));
   CHECK(dest == 0);
   CHECK(mul0 == 0);
   CHECK(mul1 == 0);
   CHECK(addend == 0);
   return 0;
}

static int
check_fma_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t mul0;
      uint16_t mul1;
      uint16_t addend;
      uint8_t agx3b[8];
      uint8_t agx3c[16];
   } vectors[] = {
      {0, 0, 2, 0,
       {0x09, 0x03, 0x1e, 0x01, 0x81, 0x00, 0x02, 0xc0},
       {0x29, 0x02, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
        0x38, 0x00, 0xa0, 0x00, 0x00, 0x08, 0x00, 0x80}},
      {0, 4, 0, 8,
       {0x09, 0x01, 0x1e, 0x05, 0x81, 0x08, 0x02, 0xc0},
       {0x29, 0x00, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
        0x38, 0x01, 0xa0, 0x00, 0x08, 0x08, 0x00, 0x80}},
      {0, 0, 4, 8,
       {0x09, 0x05, 0x1e, 0x01, 0x81, 0x08, 0x02, 0xc0},
       {0x29, 0x00, 0x07, 0x1a, 0x20, 0x20, 0xa3, 0x02,
        0x38, 0x00, 0xa0, 0x00, 0x08, 0x08, 0x00, 0x80}},
      {16, 10, 2, 12,
       {0x89, 0x03, 0x1e, 0x0b, 0x81, 0x0c, 0x02, 0xc0},
       {0xa9, 0x02, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
        0xb8, 0x02, 0xa0, 0x00, 0x0c, 0x08, 0x00, 0x80}},
      {126, 126, 126, 126,
       {0xf9, 0x7f, 0xde, 0x7f, 0x81, 0x7e, 0x02, 0xc0},
       {0xb9, 0x02, 0x87, 0xfa, 0x20, 0x20, 0xa3, 0x3a,
        0xb9, 0x1f, 0xa1, 0x00, 0x3e, 0x08, 0x00, 0x80}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];

      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         const uint8_t expected_length =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? 8 : 16;
         struct agx3_encoded_fma_f32_reg_reg_reg shared;
         struct d12_agx3_fma_f32_reg_reg_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
         uint16_t addend = UINT16_MAX;

         CHECK(agx3_pack_fma_f32_reg_reg_reg(
            target->encoding, vectors[v].dest, vectors[v].mul0,
            vectors[v].mul1, vectors[v].addend, &shared));
         CHECK(shared.length == expected_length);
         CHECK(memcmp(shared.bytes, expected, expected_length) == 0);
         CHECK(d12_agx3_encode_fma_f32_reg_reg_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].mul0,
            vectors[v].mul1, vectors[v].addend, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, expected_length) == 0);
         for (unsigned i = expected_length;
              i < D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH; ++i)
            CHECK(encoded.bytes[i] == 0);
         CHECK(d12_agx3_decode_fma_f32_reg_reg_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &mul0, &mul1, &addend));
         CHECK(dest == vectors[v].dest);
         CHECK(mul0 == vectors[v].mul0);
         CHECK(mul1 == vectors[v].mul1);
         CHECK(addend == vectors[v].addend);
      }
   }

   return 0;
}

static int
check_fma_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];

      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][4] = {
            {value, 0, 0, 0}, {0, value, 0, 0},
            {0, 0, value, 0}, {0, 0, 0, value},
         };

         for (unsigned operand = 0; operand < 4; ++operand) {
            struct agx3_encoded_fma_f32_reg_reg_reg shared;
            struct d12_agx3_fma_f32_reg_reg_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
            uint16_t addend = UINT16_MAX;

            CHECK(agx3_pack_fma_f32_reg_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], operands[operand][3], &shared));
            CHECK(d12_agx3_encode_fma_f32_reg_reg_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], operands[operand][2],
               operands[operand][3], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_fma_f32_reg_reg_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &mul0, &mul1, &addend));
            CHECK(dest == operands[operand][0]);
            CHECK(mul0 == operands[operand][1]);
            CHECK(mul1 == operands[operand][2]);
            CHECK(addend == operands[operand][3]);
         }
      }
   }

   return 0;
}

static int
check_fma_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x7e, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xf0, 0x00, 0x00, 0x00, 0x38,
       0x81, 0x1f, 0x01, 0x00, 0x3e, 0x00, 0x00, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][4] = {
      {0, 0, 0, 0}, {126, 126, 126, 126},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_fma_f32_reg_reg_reg_encoding canonical;
         CHECK(d12_agx3_encode_fma_f32_reg_reg_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], states[state][2],
            states[state][3], &canonical));

         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
            uint16_t addend = UINT16_MAX;
            const bool accepted = d12_agx3_decode_fma_f32_reg_reg_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &mul0, &mul1, &addend);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               struct d12_agx3_fma_f32_reg_reg_reg_encoding repacked;
               CHECK(d12_agx3_encode_fma_f32_reg_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, mul0, mul1, addend, &repacked));
               CHECK(repacked.length == canonical.length);
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && mul0 == 0 && mul1 == 0 && addend == 0);
            }
         }
      }
   }

   return 0;
}

static int
check_fma_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0x09, 0x03, 0x1e, 0x01, 0x81, 0x00, 0x02, 0xc0,
   };
   static const uint8_t canonical_c[] = {
      0x29, 0x02, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
      0x38, 0x00, 0xa0, 0x00, 0x00, 0x08, 0x00, 0x80,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      const uint32_t length =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? sizeof(canonical_b)
                                                    : sizeof(canonical_c);
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_fma_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0, 2, 0) == 0);
         CHECK(check_fma_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical, length) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_fma_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0, 2, 0) == 0);
         CHECK(check_fma_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         for (unsigned operand = 0; operand < 4; ++operand) {
            uint16_t args[4] = {0};
            args[operand] = invalid_values[value];
            CHECK(check_fma_rejected_and_cleared(
                     representatives[r].chip, representatives[r].encoding,
                     args[0], args[1], args[2], args[3]) == 0);
         }
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_fma_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_fma_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);

   uint8_t appended[D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_fma_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_fma_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_fma_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t alternate_b[] = {
      0x19, 0x03, 0x0e, 0x01, 0x01, 0x00, 0x02, 0xc0,
   };
   static const uint8_t alternate_c[] = {
      0x39, 0x02, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
      0x38, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80,
   };
   CHECK(check_fma_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, alternate_b,
            sizeof(alternate_b)) == 0);
   CHECK(check_fma_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, alternate_c,
            sizeof(alternate_c)) == 0);

   CHECK(!d12_agx3_encode_fma_f32_reg_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, 2, 0, NULL));
   CHECK(check_fma_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
   uint16_t addend = UINT16_MAX;
   CHECK(!d12_agx3_decode_fma_f32_reg_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &mul0, &mul1, &addend));
   CHECK(mul0 == 0 && mul1 == 0 && addend == 0);
   dest = mul0 = mul1 = addend = UINT16_MAX;
   CHECK(!d12_agx3_decode_fma_f32_reg_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL, &mul1, &addend));
   CHECK(dest == 0 && mul1 == 0 && addend == 0);
   dest = mul0 = mul1 = addend = UINT16_MAX;
   CHECK(!d12_agx3_decode_fma_f32_reg_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, &mul0, NULL, &addend));
   CHECK(dest == 0 && mul0 == 0 && addend == 0);
   dest = mul0 = mul1 = addend = UINT16_MAX;
   CHECK(!d12_agx3_decode_fma_f32_reg_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, &mul0, &mul1, NULL));
   CHECK(dest == 0 && mul0 == 0 && mul1 == 0);
   return 0;
}

static bool
fmin_encoding_is_cleared(
   const struct d12_agx3_fmin_f32_reg_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_fmin_rejected_and_cleared(enum agx_chip chip,
                                enum agx_isa_encoding encoding,
                                uint16_t dest, uint16_t src0, uint16_t src1)
{
   struct d12_agx3_fmin_f32_reg_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_fmin_f32_reg_reg(
      chip, encoding, dest, src0, src1, &encoded));
   CHECK(fmin_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_fmin_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmin_f32_reg_reg(
      chip, encoding, instruction, instruction_size, &dest, &src0, &src1));
   CHECK(dest == 0 && src0 == 0 && src1 == 0);
   return 0;
}

static int
check_fmin_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src0;
      uint16_t src1;
      uint8_t agx3b[6];
      uint8_t agx3c[14];
   } vectors[] = {
      {0, 2, 0,
       {0x02, 0x03, 0x1e, 0x01, 0x01, 0xc0},
       {0x22, 0x02, 0x47, 0x0a, 0x23, 0x00, 0xa0,
        0x02, 0x14, 0x00, 0xa1, 0x60, 0x02, 0x40}},
      {2, 2, 4,
       {0x12, 0x03, 0x1e, 0x05, 0x01, 0xc0},
       {0x32, 0x02, 0x47, 0x0a, 0x23, 0x00, 0xa0,
        0x02, 0x14, 0x01, 0xa1, 0x60, 0x22, 0x40}},
      {16, 10, 2,
       {0x82, 0x0b, 0x1e, 0x03, 0x01, 0xc0},
       {0xa2, 0x02, 0x47, 0x4b, 0x23, 0x00, 0xa0,
        0x02, 0x94, 0x00, 0xa1, 0x60, 0x0a, 0x40}},
      {126, 126, 126,
       {0xf2, 0x7f, 0xde, 0x7f, 0x01, 0xc0},
       {0xb2, 0x02, 0xc7, 0xeb, 0x23, 0x80, 0xa0,
        0x3a, 0xb7, 0x1f, 0xa1, 0x60, 0x7e, 0x4d}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         const uint8_t expected_length =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? 6 : 14;
         struct agx3_encoded_fmin_f32_reg_reg shared;
         struct d12_agx3_fmin_f32_reg_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

         CHECK(agx3_pack_fmin_f32_reg_reg(
            target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &shared));
         CHECK(shared.length == expected_length);
         CHECK(memcmp(shared.bytes, expected, expected_length) == 0);
         CHECK(d12_agx3_encode_fmin_f32_reg_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, expected_length) == 0);
         for (unsigned i = expected_length;
              i < D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH; ++i)
            CHECK(encoded.bytes[i] == 0);
         CHECK(d12_agx3_decode_fmin_f32_reg_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src0, &src1));
         CHECK(dest == vectors[v].dest);
         CHECK(src0 == vectors[v].src0);
         CHECK(src1 == vectors[v].src1);
      }
   }
   return 0;
}

static int
check_fmin_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fmin_f32_reg_reg shared;
            struct d12_agx3_fmin_f32_reg_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            CHECK(agx3_pack_fmin_f32_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], &shared));
            CHECK(d12_agx3_encode_fmin_f32_reg_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], operands[operand][2], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_fmin_f32_reg_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src0, &src1));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
         }
      }
   }
   return 0;
}

static int
check_fmin_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x00},
      /* AGX3c source bits are duplicated; one-bit inconsistencies reject. */
      {0x90, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
       0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_fmin_f32_reg_reg_encoding canonical;
         CHECK(d12_agx3_encode_fmin_f32_reg_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], states[state][2],
            &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            const bool accepted = d12_agx3_decode_fmin_f32_reg_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src0, &src1);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_fmin_f32_reg_reg_encoding repacked;
               CHECK(d12_agx3_encode_fmin_f32_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src0, src1, &repacked));
               CHECK(repacked.length == canonical.length);
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src0 == 0 && src1 == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_fmin_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x01, 0xc0,
   };
   static const uint8_t canonical_c[] = {
      0x22, 0x02, 0x47, 0x0a, 0x23, 0x00, 0xa0,
      0x02, 0x14, 0x00, 0xa1, 0x60, 0x02, 0x40,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      const uint32_t length =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? sizeof(canonical_b)
                                                    : sizeof(canonical_c);
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_fmin_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 2, 0) == 0);
         CHECK(check_fmin_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical, length) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_fmin_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 2, 0) == 0);
         CHECK(check_fmin_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         for (unsigned operand = 0; operand < 3; ++operand) {
            uint16_t args[3] = {0};
            args[operand] = invalid_values[value];
            CHECK(check_fmin_rejected_and_cleared(
                     representatives[r].chip, representatives[r].encoding,
                     args[0], args[1], args[2]) == 0);
         }
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_fmin_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_fmin_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);

   uint8_t appended[D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_fmin_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_fmin_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_fmin_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t alternate_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x01, 0x20,
   };
   static const uint8_t fmax_c[] = {
      0x22, 0x02, 0x07, 0x0a, 0x23, 0x00, 0xa0,
      0x02, 0x14, 0x00, 0xa1, 0x64, 0x02, 0x40,
   };
   CHECK(check_fmin_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, alternate_b,
            sizeof(alternate_b)) == 0);
   CHECK(check_fmin_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, fmax_c,
            sizeof(fmax_c)) == 0);

   CHECK(!d12_agx3_encode_fmin_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 2, 0, NULL));
   CHECK(check_fmin_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmin_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src0, &src1));
   CHECK(src0 == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmin_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL, &src1));
   CHECK(dest == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmin_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, &src0, NULL));
   CHECK(dest == 0 && src0 == 0);
   return 0;
}

static bool
fmax_encoding_is_cleared(
   const struct d12_agx3_fmax_f32_reg_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_FMAX_F32_REG_REG_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_fmax_rejected_and_cleared(enum agx_chip chip,
                                enum agx_isa_encoding encoding,
                                uint16_t dest, uint16_t src0, uint16_t src1)
{
   struct d12_agx3_fmax_f32_reg_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_fmax_f32_reg_reg(
      chip, encoding, dest, src0, src1, &encoded));
   CHECK(fmax_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_fmax_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmax_f32_reg_reg(
      chip, encoding, instruction, instruction_size, &dest, &src0, &src1));
   CHECK(dest == 0 && src0 == 0 && src1 == 0);
   return 0;
}

static int
check_fmax_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src0;
      uint16_t src1;
      uint8_t agx3b[6];
      uint8_t agx3c[14];
   } vectors[] = {
      {0, 2, 0,
       {0x02, 0x03, 0x1e, 0x01, 0x00, 0xc0},
       {0x22, 0x02, 0x07, 0x0a, 0x23, 0x00, 0xa0,
        0x02, 0x14, 0x00, 0xa1, 0x64, 0x02, 0x40}},
      {2, 2, 4,
       {0x12, 0x03, 0x1e, 0x05, 0x00, 0xc0},
       {0x32, 0x02, 0x07, 0x0a, 0x23, 0x00, 0xa0,
        0x02, 0x14, 0x01, 0xa1, 0x64, 0x22, 0x40}},
      {16, 10, 2,
       {0x82, 0x0b, 0x1e, 0x03, 0x00, 0xc0},
       {0xa2, 0x02, 0x07, 0x4b, 0x23, 0x00, 0xa0,
        0x02, 0x94, 0x00, 0xa1, 0x64, 0x0a, 0x40}},
      {126, 126, 126,
       {0xf2, 0x7f, 0xde, 0x7f, 0x00, 0xc0},
       {0xb2, 0x02, 0x87, 0xeb, 0x23, 0x80, 0xa0,
        0x3a, 0xb7, 0x1f, 0xa1, 0x64, 0x7e, 0x4d}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         const uint8_t expected_length =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? 6 : 14;
         struct agx3_encoded_fmax_f32_reg_reg shared;
         struct d12_agx3_fmax_f32_reg_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

         CHECK(agx3_pack_fmax_f32_reg_reg(
            target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &shared));
         CHECK(shared.length == expected_length);
         CHECK(memcmp(shared.bytes, expected, expected_length) == 0);
         CHECK(d12_agx3_encode_fmax_f32_reg_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src0,
            vectors[v].src1, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, expected_length) == 0);
         for (unsigned i = expected_length;
              i < D12_AGX3_FMAX_F32_REG_REG_MAX_LENGTH; ++i)
            CHECK(encoded.bytes[i] == 0);
         CHECK(d12_agx3_decode_fmax_f32_reg_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src0, &src1));
         CHECK(dest == vectors[v].dest);
         CHECK(src0 == vectors[v].src0);
         CHECK(src1 == vectors[v].src1);
      }
   }
   return 0;
}

static int
check_fmax_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fmax_f32_reg_reg shared;
            struct d12_agx3_fmax_f32_reg_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            CHECK(agx3_pack_fmax_f32_reg_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               operands[operand][2], &shared));
            CHECK(d12_agx3_encode_fmax_f32_reg_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], operands[operand][2], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_fmax_f32_reg_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src0, &src1));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
         }
      }
   }
   return 0;
}

static int
check_fmax_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_FMAX_F32_REG_REG_MAX_LENGTH] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x00},
      /* AGX3c source bits are duplicated; one-bit inconsistencies reject. */
      {0x90, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
       0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_fmax_f32_reg_reg_encoding canonical;
         CHECK(d12_agx3_encode_fmax_f32_reg_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], states[state][2],
            &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_FMAX_F32_REG_REG_MAX_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            const bool accepted = d12_agx3_decode_fmax_f32_reg_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src0, &src1);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_fmax_f32_reg_reg_encoding repacked;
               CHECK(d12_agx3_encode_fmax_f32_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src0, src1, &repacked));
               CHECK(repacked.length == canonical.length);
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src0 == 0 && src1 == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_fmax_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x00, 0xc0,
   };
   static const uint8_t canonical_c[] = {
      0x22, 0x02, 0x07, 0x0a, 0x23, 0x00, 0xa0,
      0x02, 0x14, 0x00, 0xa1, 0x64, 0x02, 0x40,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      const uint32_t length =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? sizeof(canonical_b)
                                                    : sizeof(canonical_c);
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_fmax_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 2, 0) == 0);
         CHECK(check_fmax_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical, length) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_fmax_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 2, 0) == 0);
         CHECK(check_fmax_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         for (unsigned operand = 0; operand < 3; ++operand) {
            uint16_t args[3] = {0};
            args[operand] = invalid_values[value];
            CHECK(check_fmax_rejected_and_cleared(
                     representatives[r].chip, representatives[r].encoding,
                     args[0], args[1], args[2]) == 0);
         }
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_fmax_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_fmax_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);

   uint8_t appended[D12_AGX3_FMAX_F32_REG_REG_MAX_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_fmax_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_fmax_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_fmax_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t alternate_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x00, 0x20,
   };
   static const uint8_t fmin_c[] = {
      0x22, 0x02, 0x47, 0x0a, 0x23, 0x00, 0xa0,
      0x02, 0x14, 0x00, 0xa1, 0x60, 0x02, 0x40,
   };
   CHECK(check_fmax_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, alternate_b,
            sizeof(alternate_b)) == 0);
   CHECK(check_fmax_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, fmin_c,
            sizeof(fmin_c)) == 0);

   CHECK(!d12_agx3_encode_fmax_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 2, 0, NULL));
   CHECK(check_fmax_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmax_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src0, &src1));
   CHECK(src0 == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmax_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL, &src1));
   CHECK(dest == 0 && src1 == 0);
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_fmax_f32_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, &src0, NULL));
   CHECK(dest == 0 && src0 == 0);
   return 0;
}

static bool
i32_to_f32_encoding_is_cleared(
   const struct d12_agx3_i32_to_f32_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_I32_TO_F32_REG_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_i32_to_f32_rejected_and_cleared(enum agx_chip chip,
                                      enum agx_isa_encoding encoding,
                                      uint16_t dest, uint16_t src)
{
   struct d12_agx3_i32_to_f32_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_i32_to_f32_reg(
      chip, encoding, dest, src, &encoded));
   CHECK(i32_to_f32_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_i32_to_f32_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_to_f32_reg(
      chip, encoding, instruction, instruction_size, &dest, &src));
   CHECK(dest == 0 && src == 0);
   return 0;
}

static int
check_i32_to_f32_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src;
      uint8_t agx3b[8];
      uint8_t agx3c[10];
   } vectors[] = {
      {0, 0,
       {0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x60},
       {0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x68, 0x00}},
      {2, 0,
       {0xa7, 0x07, 0x56, 0x02, 0x02, 0x00, 0xac, 0x60},
       {0x3f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x68, 0x00}},
      {0, 2,
       {0xa7, 0x07, 0x56, 0x00, 0x02, 0x04, 0xac, 0x60},
       {0x2f, 0x02, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x68, 0x00}},
      {4, 10,
       {0xa7, 0x07, 0x56, 0x04, 0x02, 0x14, 0xac, 0x60},
       {0x2f, 0x02, 0x00, 0x22, 0x22, 0x80, 0xaf, 0x0a, 0x68, 0x00}},
      {126, 126,
       {0xa7, 0x07, 0x56, 0x7e, 0x02, 0xfc, 0xac, 0x60},
       {0xbf, 0x02, 0x80, 0xf2, 0x22, 0x80, 0xaf, 0x3a, 0x69, 0x00}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         const uint8_t expected_length =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? 8 : 10;
         struct agx3_encoded_i32_to_f32_reg shared;
         struct d12_agx3_i32_to_f32_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src = UINT16_MAX;

         CHECK(agx3_pack_i32_to_f32_reg(
            target->encoding, vectors[v].dest, vectors[v].src, &shared));
         CHECK(shared.length == expected_length);
         CHECK(memcmp(shared.bytes, expected, expected_length) == 0);
         CHECK(d12_agx3_encode_i32_to_f32_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src,
            &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, expected_length) == 0);
         for (unsigned i = expected_length;
              i < D12_AGX3_I32_TO_F32_REG_MAX_LENGTH; ++i)
            CHECK(encoded.bytes[i] == 0);
         CHECK(d12_agx3_decode_i32_to_f32_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src));
         CHECK(dest == vectors[v].dest);
         CHECK(src == vectors[v].src);
      }
   }
   return 0;
}

static int
check_i32_to_f32_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][2] = {{value, 0}, {0, value}};
         for (unsigned operand = 0; operand < 2; ++operand) {
            struct agx3_encoded_i32_to_f32_reg shared;
            struct d12_agx3_i32_to_f32_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            CHECK(agx3_pack_i32_to_f32_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               &shared));
            CHECK(d12_agx3_encode_i32_to_f32_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_i32_to_f32_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src));
            CHECK(dest == operands[operand][0]);
            CHECK(src == operands[operand][1]);
         }
      }
   }
   return 0;
}

static int
check_i32_to_f32_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_I32_TO_F32_REG_MAX_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xf0, 0x00, 0x00, 0x00, 0x38, 0x01, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_i32_to_f32_reg_encoding canonical;
         CHECK(d12_agx3_encode_i32_to_f32_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_I32_TO_F32_REG_MAX_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            const bool accepted = d12_agx3_decode_i32_to_f32_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_i32_to_f32_reg_encoding repacked;
               CHECK(d12_agx3_encode_i32_to_f32_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src, &repacked));
               CHECK(repacked.length == canonical.length);
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_i32_to_f32_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x60,
   };
   static const uint8_t canonical_c[] = {
      0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x68, 0x00,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      const uint32_t length =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? sizeof(canonical_b)
                                                    : sizeof(canonical_c);
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_i32_to_f32_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0) == 0);
         CHECK(check_i32_to_f32_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical, length) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_i32_to_f32_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0) == 0);
         CHECK(check_i32_to_f32_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         CHECK(check_i32_to_f32_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  invalid_values[value], 0) == 0);
         CHECK(check_i32_to_f32_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  0, invalid_values[value]) == 0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_i32_to_f32_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_i32_to_f32_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);

   uint8_t appended[D12_AGX3_I32_TO_F32_REG_MAX_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t unsigned_b[] = {
      0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x20,
   };
   static const uint8_t unsigned_c[] = {
      0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x28, 0x00,
   };
   static const uint8_t alternate_unsigned_b[] = {
      0xa7, 0x07, 0x54, 0x08, 0x00, 0x00, 0xac, 0x20,
   };
   static const uint8_t alternate_unsigned_c[] = {
      0x27, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x12, 0xa8, 0x00,
   };
   static const uint8_t f32_to_i32_b[] = {
      0x27, 0x07, 0x56, 0x00, 0x02, 0x00, 0xb4, 0x48, 0x03, 0x00,
   };
   static const uint8_t f32_to_i32_c[] = {
      0x2f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01,
   };
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, unsigned_b,
            sizeof(unsigned_b)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, unsigned_c,
            sizeof(unsigned_c)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, alternate_unsigned_b,
            sizeof(alternate_unsigned_b)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, alternate_unsigned_c,
            sizeof(alternate_unsigned_c)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, f32_to_i32_b,
            sizeof(f32_to_i32_b)) == 0);
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, f32_to_i32_c,
            sizeof(f32_to_i32_c)) == 0);

   CHECK(!d12_agx3_encode_i32_to_f32_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, NULL));
   CHECK(check_i32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_to_f32_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src));
   CHECK(src == 0);
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_to_f32_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL));
   CHECK(dest == 0);
   return 0;
}

static bool
u32_to_f32_encoding_is_cleared(
   const struct d12_agx3_u32_to_f32_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_U32_TO_F32_REG_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_u32_to_f32_rejected_and_cleared(enum agx_chip chip,
                                      enum agx_isa_encoding encoding,
                                      uint16_t dest, uint16_t src)
{
   struct d12_agx3_u32_to_f32_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_u32_to_f32_reg(
      chip, encoding, dest, src, &encoded));
   CHECK(u32_to_f32_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_u32_to_f32_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_u32_to_f32_reg(
      chip, encoding, instruction, instruction_size, &dest, &src));
   CHECK(dest == 0 && src == 0);
   return 0;
}

static int
check_u32_to_f32_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src;
      uint8_t agx3b[8];
      uint8_t agx3c[10];
   } vectors[] = {
      {0, 0,
       {0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x20},
       {0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x28, 0x00}},
      {2, 0,
       {0xa7, 0x07, 0x56, 0x02, 0x02, 0x00, 0xac, 0x20},
       {0x3f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x28, 0x00}},
      {0, 2,
       {0xa7, 0x07, 0x56, 0x00, 0x02, 0x04, 0xac, 0x20},
       {0x2f, 0x02, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x28, 0x00}},
      {4, 10,
       {0xa7, 0x07, 0x56, 0x04, 0x02, 0x14, 0xac, 0x20},
       {0x2f, 0x02, 0x00, 0x22, 0x22, 0x80, 0xaf, 0x0a, 0x28, 0x00}},
      {126, 126,
       {0xa7, 0x07, 0x56, 0x7e, 0x02, 0xfc, 0xac, 0x20},
       {0xbf, 0x02, 0x80, 0xf2, 0x22, 0x80, 0xaf, 0x3a, 0x29, 0x00}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         const uint8_t expected_length =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? 8 : 10;
         struct agx3_encoded_u32_to_f32_reg shared;
         struct d12_agx3_u32_to_f32_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src = UINT16_MAX;

         CHECK(agx3_pack_u32_to_f32_reg(
            target->encoding, vectors[v].dest, vectors[v].src, &shared));
         CHECK(shared.length == expected_length);
         CHECK(memcmp(shared.bytes, expected, expected_length) == 0);
         CHECK(d12_agx3_encode_u32_to_f32_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src,
            &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, expected_length) == 0);
         for (unsigned i = expected_length;
              i < D12_AGX3_U32_TO_F32_REG_MAX_LENGTH; ++i)
            CHECK(encoded.bytes[i] == 0);
         CHECK(d12_agx3_decode_u32_to_f32_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src));
         CHECK(dest == vectors[v].dest);
         CHECK(src == vectors[v].src);
      }
   }
   return 0;
}

static int
check_u32_to_f32_register_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][2] = {{value, 0}, {0, value}};
         for (unsigned operand = 0; operand < 2; ++operand) {
            struct agx3_encoded_u32_to_f32_reg shared;
            struct d12_agx3_u32_to_f32_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            CHECK(agx3_pack_u32_to_f32_reg(
               target->encoding, operands[operand][0], operands[operand][1],
               &shared));
            CHECK(d12_agx3_encode_u32_to_f32_reg(
               target->chip, target->encoding, operands[operand][0],
               operands[operand][1], &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_u32_to_f32_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src));
            CHECK(dest == operands[operand][0]);
            CHECK(src == operands[operand][1]);
         }
      }
   }
   return 0;
}

static int
check_u32_to_f32_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_U32_TO_F32_REG_MAX_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xf0, 0x00, 0x00, 0x00, 0x38, 0x01, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_u32_to_f32_reg_encoding canonical;
         CHECK(d12_agx3_encode_u32_to_f32_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_U32_TO_F32_REG_MAX_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            const bool accepted = d12_agx3_decode_u32_to_f32_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_u32_to_f32_reg_encoding repacked;
               CHECK(d12_agx3_encode_u32_to_f32_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src, &repacked));
               CHECK(repacked.length == canonical.length);
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_u32_to_f32_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x20,
   };
   static const uint8_t canonical_c[] = {
      0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x28, 0x00,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      const uint32_t length =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? sizeof(canonical_b)
                                                    : sizeof(canonical_c);
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_u32_to_f32_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0) == 0);
         CHECK(check_u32_to_f32_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical, length) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_u32_to_f32_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0) == 0);
         CHECK(check_u32_to_f32_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         CHECK(check_u32_to_f32_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  invalid_values[value], 0) == 0);
         CHECK(check_u32_to_f32_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  0, invalid_values[value]) == 0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_u32_to_f32_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_u32_to_f32_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);

   uint8_t appended[D12_AGX3_U32_TO_F32_REG_MAX_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t signed_b[] = {
      0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x60,
   };
   static const uint8_t signed_c[] = {
      0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x68, 0x00,
   };
   static const uint8_t alternate_b[] = {
      0xa7, 0x07, 0x54, 0x08, 0x00, 0x00, 0xac, 0x20,
   };
   static const uint8_t alternate_c[] = {
      0x27, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x12, 0xa8, 0x00,
   };
   static const uint8_t f32_to_u32_b[] = {
      0x27, 0x07, 0x56, 0x00, 0x02, 0x00, 0xb4, 0x08, 0x02, 0x00,
   };
   static const uint8_t f32_to_u32_c[] = {
      0x2f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0xb0, 0x03,
   };
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, signed_b,
            sizeof(signed_b)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, signed_c,
            sizeof(signed_c)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, alternate_b,
            sizeof(alternate_b)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, alternate_c,
            sizeof(alternate_c)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, f32_to_u32_b,
            sizeof(f32_to_u32_b)) == 0);
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, f32_to_u32_c,
            sizeof(f32_to_u32_c)) == 0);

   CHECK(!d12_agx3_encode_u32_to_f32_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, NULL));
   CHECK(check_u32_to_f32_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_u32_to_f32_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src));
   CHECK(src == 0);
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_u32_to_f32_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL));
   CHECK(dest == 0);
   return 0;
}

static bool
f32_rint_encoding_is_cleared(
   const struct d12_agx3_f32_rint_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_F32_RINT_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_f32_rint_rejected_and_cleared(enum agx_chip chip,
                                    enum agx_isa_encoding encoding,
                                    uint16_t dest, uint16_t src)
{
   struct d12_agx3_f32_rint_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_f32_rint_reg(
      chip, encoding, dest, src, &encoded));
   CHECK(f32_rint_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_f32_rint_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rint_reg(
      chip, encoding, instruction, instruction_size, &dest, &src));
   CHECK(dest == 0 && src == 0);
   return 0;
}

static int
check_f32_rint_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src;
      uint8_t agx3b[D12_AGX3_F32_RINT_REG_LENGTH];
      uint8_t agx3c[D12_AGX3_F32_RINT_REG_LENGTH];
   } vectors[] = {
      {0, 0,
       {0x2f, 0x00, 0x56, 0x00, 0x03, 0x00, 0xb0, 0x40, 0x00, 0x00},
       {0x2f, 0x00, 0x04, 0x18, 0x2a, 0x20, 0xa0, 0x02, 0x30, 0x00}},
      {14, 14,
       {0x2f, 0x00, 0x56, 0x0e, 0x03, 0x1c, 0xb0, 0x40, 0x00, 0x00},
       {0x3f, 0x02, 0x04, 0x78, 0x2a, 0x20, 0xa0, 0x1a, 0x30, 0x00}},
      {2, 4,
       {0x2f, 0x00, 0x56, 0x02, 0x03, 0x08, 0xb0, 0x40, 0x00, 0x00},
       {0x3f, 0x00, 0x04, 0x38, 0x2a, 0x20, 0xa0, 0x02, 0x30, 0x00}},
      {42, 86,
       {0x2f, 0x00, 0x56, 0x2a, 0x03, 0xac, 0xb0, 0x40, 0x00, 0x00},
       {0x3f, 0x02, 0x04, 0xb8, 0x2a, 0x20, 0xa0, 0x32, 0x31, 0x00}},
      {126, 126,
       {0x2f, 0x00, 0x56, 0x7e, 0x03, 0xfc, 0xb0, 0x40, 0x00, 0x00},
       {0xbf, 0x02, 0x84, 0xf8, 0x2a, 0xa0, 0xa0, 0x3a, 0x31, 0x00}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         struct agx3_encoded_f32_rint_reg shared;
         struct d12_agx3_f32_rint_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src = UINT16_MAX;

         CHECK(agx3_pack_f32_rint_reg(
            target->encoding, vectors[v].dest, vectors[v].src, &shared));
         CHECK(shared.length == D12_AGX3_F32_RINT_REG_LENGTH);
         CHECK(memcmp(shared.bytes, expected, shared.length) == 0);
         CHECK(d12_agx3_encode_f32_rint_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src,
            &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, encoded.length) == 0);
         CHECK(d12_agx3_decode_f32_rint_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src));
         CHECK(dest == vectors[v].dest);
         CHECK(src == vectors[v].src);
      }
   }
   return 0;
}

static int
check_f32_rint_operand_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t dest_in = 0; dest_in < 128; dest_in += 2) {
         for (uint16_t src_in = 0; src_in < 128; src_in += 2) {
            struct agx3_encoded_f32_rint_reg shared;
            struct d12_agx3_f32_rint_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;

            CHECK(agx3_pack_f32_rint_reg(
               target->encoding, dest_in, src_in, &shared));
            CHECK(d12_agx3_encode_f32_rint_reg(
               target->chip, target->encoding, dest_in, src_in, &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_f32_rint_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src));
            CHECK(dest == dest_in);
            CHECK(src == src_in);
         }
      }
   }
   return 0;
}

static int
check_f32_rint_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_F32_RINT_REG_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xe0, 0x00, 0x80, 0x00, 0x38, 0x01, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_f32_rint_reg_encoding canonical;
         CHECK(d12_agx3_encode_f32_rint_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_F32_RINT_REG_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            const bool accepted = d12_agx3_decode_f32_rint_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_f32_rint_reg_encoding repacked;
               CHECK(d12_agx3_encode_f32_rint_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_f32_rint_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0x2f, 0x00, 0x56, 0x00, 0x03, 0x00, 0xb0, 0x40, 0x00, 0x00,
   };
   static const uint8_t canonical_c[] = {
      0x2f, 0x00, 0x04, 0x18, 0x2a, 0x20, 0xa0, 0x02, 0x30, 0x00,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_f32_rint_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0) == 0);
         CHECK(check_f32_rint_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  sizeof(canonical_b)) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_f32_rint_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0) == 0);
         CHECK(check_f32_rint_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         CHECK(check_f32_rint_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  invalid_values[value], 0) == 0);
         CHECK(check_f32_rint_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding, 0,
                  invalid_values[value]) == 0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_f32_rint_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) ==
            0);
   uint8_t appended[D12_AGX3_F32_RINT_REG_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_f32_rint_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_f32_rint_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_f32_rint_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t related_b[][D12_AGX3_F32_RINT_REG_LENGTH] = {
      {0x2f, 0x00, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x00, 0x00},
      {0x2f, 0x00, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x06, 0x00},
      {0x2f, 0x00, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x04, 0x00},
      {0x2f, 0x00, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x02, 0x00},
      {0x2f, 0x00, 0x56, 0x16, 0x03, 0x1c, 0x92, 0x40, 0x00, 0x00},
   };
   static const uint8_t related_c[][D12_AGX3_F32_RINT_REG_LENGTH] = {
      {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa0, 0x02, 0x30, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa0, 0x02, 0xf0, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa0, 0x02, 0xb0, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa0, 0x02, 0x70, 0x00},
      {0xbf, 0x82, 0x04, 0x78, 0x2a, 0x20, 0xa0, 0x0a, 0x10, 0x00},
   };
   for (unsigned i = 0; i < sizeof(related_b) / sizeof(related_b[0]); ++i) {
      CHECK(check_f32_rint_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, related_b[i],
               sizeof(related_b[i])) == 0);
      CHECK(check_f32_rint_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, related_c[i],
               sizeof(related_c[i])) == 0);
   }

   static const uint8_t f32_to_i32_b[] = {
      0x27, 0x07, 0x56, 0x00, 0x02, 0x00, 0xb4, 0x48, 0x03, 0x00,
   };
   static const uint8_t f32_to_i32_c[] = {
      0x2f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01,
   };
   CHECK(check_f32_rint_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, f32_to_i32_b,
            sizeof(f32_to_i32_b)) == 0);
   CHECK(check_f32_rint_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, f32_to_i32_c,
            sizeof(f32_to_i32_c)) == 0);

   CHECK(!d12_agx3_encode_f32_rint_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, NULL));
   CHECK(check_f32_rint_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rint_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src));
   CHECK(src == 0);
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rint_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL));
   CHECK(dest == 0);
   return 0;
}

static bool
f32_rcp_encoding_is_cleared(
   const struct d12_agx3_f32_rcp_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_F32_RCP_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_f32_rcp_rejected_and_cleared(enum agx_chip chip,
                                   enum agx_isa_encoding encoding,
                                   uint16_t dest, uint16_t src)
{
   struct d12_agx3_f32_rcp_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_f32_rcp_reg(
      chip, encoding, dest, src, &encoded));
   CHECK(f32_rcp_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_f32_rcp_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rcp_reg(
      chip, encoding, instruction, instruction_size, &dest, &src));
   CHECK(dest == 0 && src == 0);
   return 0;
}

static int
check_f32_rcp_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src;
      uint8_t agx3b[D12_AGX3_F32_RCP_REG_LENGTH];
      uint8_t agx3c[D12_AGX3_F32_RCP_REG_LENGTH];
   } vectors[] = {
      {0, 0,
       {0xaf, 0x00, 0x56, 0x00, 0x02, 0x00, 0x10, 0x48, 0x20, 0x00},
       {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa1, 0x02, 0x30, 0x00}},
      {14, 14,
       {0xaf, 0x00, 0x56, 0x0e, 0x02, 0x1c, 0x10, 0x48, 0x20, 0x00},
       {0x3f, 0x02, 0x04, 0x78, 0x22, 0x20, 0xa1, 0x1a, 0x30, 0x00}},
      {2, 4,
       {0xaf, 0x00, 0x56, 0x02, 0x02, 0x08, 0x10, 0x48, 0x20, 0x00},
       {0x3f, 0x00, 0x04, 0x38, 0x22, 0x20, 0xa1, 0x02, 0x30, 0x00}},
      {42, 86,
       {0xaf, 0x00, 0x56, 0x2a, 0x02, 0xac, 0x10, 0x48, 0x20, 0x00},
       {0x3f, 0x02, 0x04, 0xb8, 0x22, 0x20, 0xa1, 0x32, 0x31, 0x00}},
      {126, 126,
       {0xaf, 0x00, 0x56, 0x7e, 0x02, 0xfc, 0x10, 0x48, 0x20, 0x00},
       {0xbf, 0x02, 0x84, 0xf8, 0x22, 0xa0, 0xa1, 0x3a, 0x31, 0x00}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         struct agx3_encoded_f32_rcp_reg shared;
         struct d12_agx3_f32_rcp_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src = UINT16_MAX;

         CHECK(agx3_pack_f32_rcp_reg(
            target->encoding, vectors[v].dest, vectors[v].src, &shared));
         CHECK(shared.length == D12_AGX3_F32_RCP_REG_LENGTH);
         CHECK(memcmp(shared.bytes, expected, shared.length) == 0);
         CHECK(d12_agx3_encode_f32_rcp_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src,
            &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, encoded.length) == 0);
         CHECK(d12_agx3_decode_f32_rcp_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src));
         CHECK(dest == vectors[v].dest);
         CHECK(src == vectors[v].src);
      }
   }
   return 0;
}

static int
check_f32_rcp_operand_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t dest_in = 0; dest_in < 128; dest_in += 2) {
         for (uint16_t src_in = 0; src_in < 128; src_in += 2) {
            struct agx3_encoded_f32_rcp_reg shared;
            struct d12_agx3_f32_rcp_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;

            CHECK(agx3_pack_f32_rcp_reg(
               target->encoding, dest_in, src_in, &shared));
            CHECK(d12_agx3_encode_f32_rcp_reg(
               target->chip, target->encoding, dest_in, src_in, &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_f32_rcp_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src));
            CHECK(dest == dest_in);
            CHECK(src == src_in);
         }
      }
   }
   return 0;
}

static int
check_f32_rcp_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_F32_RCP_REG_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xe0, 0x00, 0x80, 0x00, 0x38, 0x01, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_f32_rcp_reg_encoding canonical;
         CHECK(d12_agx3_encode_f32_rcp_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_F32_RCP_REG_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            const bool accepted = d12_agx3_decode_f32_rcp_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_f32_rcp_reg_encoding repacked;
               CHECK(d12_agx3_encode_f32_rcp_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_f32_rcp_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0xaf, 0x00, 0x56, 0x00, 0x02, 0x00, 0x10, 0x48, 0x20, 0x00,
   };
   static const uint8_t canonical_c[] = {
      0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa1, 0x02, 0x30, 0x00,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_f32_rcp_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0) == 0);
         CHECK(check_f32_rcp_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  sizeof(canonical_b)) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_f32_rcp_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0) == 0);
         CHECK(check_f32_rcp_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         CHECK(check_f32_rcp_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  invalid_values[value], 0) == 0);
         CHECK(check_f32_rcp_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding, 0,
                  invalid_values[value]) == 0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_f32_rcp_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) ==
            0);
   uint8_t appended[D12_AGX3_F32_RCP_REG_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_f32_rcp_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_f32_rcp_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_f32_rcp_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t related_b[][D12_AGX3_F32_RCP_REG_LENGTH] = {
      {0xaf, 0x00, 0x56, 0x0e, 0x02, 0x18, 0x00, 0x48, 0x20, 0x00},
      {0xaf, 0x00, 0x56, 0x1e, 0x02, 0x1c, 0x00, 0x48, 0x20, 0x00},
      {0xaf, 0x10, 0x54, 0x0c, 0x02, 0x18, 0x10, 0x48, 0x20, 0x00},
      {0xaf, 0x01, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x00, 0x00},
      {0x2f, 0x00, 0x56, 0x00, 0x03, 0x00, 0xb0, 0x40, 0x00, 0x00},
   };
   static const uint8_t related_c[][D12_AGX3_F32_RCP_REG_LENGTH] = {
      {0x3f, 0x00, 0x04, 0x78, 0x22, 0x20, 0xa1, 0x1a, 0x10, 0x00},
      {0xbf, 0x02, 0x04, 0x78, 0x22, 0x20, 0xa1, 0x1a, 0x10, 0x00},
      {0x27, 0x04, 0x04, 0x78, 0x22, 0x20, 0xa1, 0x1a, 0x30, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa3, 0x02, 0x30, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x2a, 0x20, 0xa0, 0x02, 0x30, 0x00},
   };
   for (unsigned i = 0; i < sizeof(related_b) / sizeof(related_b[0]); ++i) {
      CHECK(check_f32_rcp_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, related_b[i],
               sizeof(related_b[i])) == 0);
      CHECK(check_f32_rcp_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, related_c[i],
               sizeof(related_c[i])) == 0);
   }

   CHECK(!d12_agx3_encode_f32_rcp_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, NULL));
   CHECK(check_f32_rcp_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rcp_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src));
   CHECK(src == 0);
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rcp_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL));
   CHECK(dest == 0);
   return 0;
}

static bool
f32_rsqrt_encoding_is_cleared(
   const struct d12_agx3_f32_rsqrt_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_F32_RSQRT_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_f32_rsqrt_rejected_and_cleared(enum agx_chip chip,
                                     enum agx_isa_encoding encoding,
                                     uint16_t dest, uint16_t src)
{
   struct d12_agx3_f32_rsqrt_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_f32_rsqrt_reg(
      chip, encoding, dest, src, &encoded));
   CHECK(f32_rsqrt_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_f32_rsqrt_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rsqrt_reg(
      chip, encoding, instruction, instruction_size, &dest, &src));
   CHECK(dest == 0 && src == 0);
   return 0;
}

static int
check_f32_rsqrt_exact_witnesses(void)
{
   static const struct {
      uint16_t dest;
      uint16_t src;
      uint8_t agx3b[D12_AGX3_F32_RSQRT_REG_LENGTH];
      uint8_t agx3c[D12_AGX3_F32_RSQRT_REG_LENGTH];
   } vectors[] = {
      {0, 0,
       {0xaf, 0x01, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x00, 0x00},
       {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa3, 0x02, 0x30, 0x00}},
      {14, 14,
       {0xaf, 0x01, 0x56, 0x0e, 0x02, 0x1c, 0xb0, 0x40, 0x00, 0x00},
       {0x3f, 0x02, 0x04, 0x78, 0x22, 0x20, 0xa3, 0x1a, 0x30, 0x00}},
      {2, 4,
       {0xaf, 0x01, 0x56, 0x02, 0x02, 0x08, 0xb0, 0x40, 0x00, 0x00},
       {0x3f, 0x00, 0x04, 0x38, 0x22, 0x20, 0xa3, 0x02, 0x30, 0x00}},
      {42, 86,
       {0xaf, 0x01, 0x56, 0x2a, 0x02, 0xac, 0xb0, 0x40, 0x00, 0x00},
       {0x3f, 0x02, 0x04, 0xb8, 0x22, 0x20, 0xa3, 0x32, 0x31, 0x00}},
      {126, 126,
       {0xaf, 0x01, 0x56, 0x7e, 0x02, 0xfc, 0xb0, 0x40, 0x00, 0x00},
       {0xbf, 0x02, 0x84, 0xf8, 0x22, 0xa0, 0xa3, 0x3a, 0x31, 0x00}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         struct agx3_encoded_f32_rsqrt_reg shared;
         struct d12_agx3_f32_rsqrt_reg_encoding encoded;
         uint16_t dest = UINT16_MAX, src = UINT16_MAX;

         CHECK(agx3_pack_f32_rsqrt_reg(
            target->encoding, vectors[v].dest, vectors[v].src, &shared));
         CHECK(shared.length == D12_AGX3_F32_RSQRT_REG_LENGTH);
         CHECK(memcmp(shared.bytes, expected, shared.length) == 0);
         CHECK(d12_agx3_encode_f32_rsqrt_reg(
            target->chip, target->encoding, vectors[v].dest, vectors[v].src,
            &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, encoded.length) == 0);
         CHECK(d12_agx3_decode_f32_rsqrt_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &dest, &src));
         CHECK(dest == vectors[v].dest);
         CHECK(src == vectors[v].src);
      }
   }
   return 0;
}

static int
check_f32_rsqrt_operand_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (uint16_t dest_in = 0; dest_in < 128; dest_in += 2) {
         for (uint16_t src_in = 0; src_in < 128; src_in += 2) {
            struct agx3_encoded_f32_rsqrt_reg shared;
            struct d12_agx3_f32_rsqrt_reg_encoding encoded;
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;

            CHECK(agx3_pack_f32_rsqrt_reg(
               target->encoding, dest_in, src_in, &shared));
            CHECK(d12_agx3_encode_f32_rsqrt_reg(
               target->chip, target->encoding, dest_in, src_in, &encoded));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_f32_rsqrt_reg(
               target->chip, target->encoding, encoded.bytes, encoded.length,
               &dest, &src));
            CHECK(dest == dest_in);
            CHECK(src == src_in);
         }
      }
   }
   return 0;
}

static int
check_f32_rsqrt_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_F32_RSQRT_REG_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xe0, 0x00, 0x80, 0x00, 0x38, 0x01, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_f32_rsqrt_reg_encoding canonical;
         CHECK(d12_agx3_encode_f32_rsqrt_reg(
            representatives[r].chip, representatives[r].encoding,
            states[state][0], states[state][1], &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[D12_AGX3_F32_RSQRT_REG_LENGTH] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            const bool accepted = d12_agx3_decode_f32_rsqrt_reg(
               representatives[r].chip, representatives[r].encoding,
               mutated, canonical.length, &dest, &src);
            const bool is_operand =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               struct d12_agx3_f32_rsqrt_reg_encoding repacked;
               CHECK(d12_agx3_encode_f32_rsqrt_reg(
                  representatives[r].chip, representatives[r].encoding,
                  dest, src, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(dest == 0 && src == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_f32_rsqrt_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0xaf, 0x01, 0x56, 0x00, 0x02, 0x00, 0xb0, 0x40, 0x00, 0x00,
   };
   static const uint8_t canonical_c[] = {
      0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa3, 0x02, 0x30, 0x00,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_f32_rsqrt_rejected_and_cleared(
                  target->chip, all_encodings[e], 0, 0) == 0);
         CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  sizeof(canonical_b)) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_f32_rsqrt_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], 0, 0) == 0);
         CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         CHECK(check_f32_rsqrt_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  invalid_values[value], 0) == 0);
         CHECK(check_f32_rsqrt_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding, 0,
                  invalid_values[value]) == 0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) ==
            0);
   uint8_t appended[D12_AGX3_F32_RSQRT_REG_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t related_b[][D12_AGX3_F32_RSQRT_REG_LENGTH] = {
      {0xaf, 0x01, 0x56, 0x00, 0x02, 0x04, 0x90, 0x40, 0x00, 0x00},
      {0xaf, 0x11, 0x54, 0x0c, 0x02, 0x18, 0xb0, 0x40, 0x00, 0x00},
      {0xaf, 0x00, 0x56, 0x00, 0x02, 0x00, 0x10, 0x48, 0x20, 0x00},
      {0x2f, 0x00, 0x56, 0x00, 0x03, 0x00, 0xb0, 0x40, 0x00, 0x00},
   };
   static const uint8_t related_c[][D12_AGX3_F32_RSQRT_REG_LENGTH] = {
      {0x2f, 0x02, 0x04, 0x18, 0x22, 0x20, 0xa3, 0x02, 0x10, 0x00},
      {0x27, 0x04, 0x04, 0x78, 0x22, 0x20, 0xa3, 0x1a, 0x30, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x22, 0x20, 0xa1, 0x02, 0x30, 0x00},
      {0x2f, 0x00, 0x04, 0x18, 0x2a, 0x20, 0xa0, 0x02, 0x30, 0x00},
   };
   for (unsigned i = 0; i < sizeof(related_b) / sizeof(related_b[0]); ++i) {
      CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, related_b[i],
               sizeof(related_b[i])) == 0);
      CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, related_c[i],
               sizeof(related_c[i])) == 0);
   }

   CHECK(!d12_agx3_encode_f32_rsqrt_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0, 0, NULL));
   CHECK(check_f32_rsqrt_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rsqrt_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &src));
   CHECK(src == 0);
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_rsqrt_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &dest, NULL));
   CHECK(dest == 0);
   return 0;
}

static bool
i32_compare_select_imm_5_7_encoding_is_cleared(
   const struct d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0;
        i < D12_AGX3_I32_COMPARE_SELECT_IMM_5_7_REG_REG_MAX_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_i32_compare_select_imm_5_7_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   enum d12_agx3_i32_compare_predicate predicate, uint16_t dest,
   uint16_t src0, uint16_t src1)
{
   struct d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
      chip, encoding, predicate, dest, src0, src1, &encoded));
   CHECK(i32_compare_select_imm_5_7_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   enum d12_agx3_i32_compare_predicate predicate =
      (enum d12_agx3_i32_compare_predicate)99;
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
      chip, encoding, instruction, instruction_size, &predicate, &dest,
      &src0, &src1));
   CHECK(predicate == D12_AGX3_I32_COMPARE_EQ);
   CHECK(dest == 0 && src0 == 0 && src1 == 0);
   return 0;
}

static int
check_i32_compare_select_imm_5_7_exact_witnesses(void)
{
   static const struct {
      enum d12_agx3_i32_compare_predicate predicate;
      enum agx3_i32_compare_predicate shared_predicate;
      uint16_t dest, src0, src1;
      uint8_t agx3b[10];
      uint8_t agx3c[14];
   } vectors[] = {
      {D12_AGX3_I32_COMPARE_EQ, AGX3_I32_COMPARE_EQ, 0, 0, 4,
       {0x02, 0x01, 0x1f, 0x05, 0x26, 0x85, 0x07, 0xc0, 0x20, 0x87},
       {0x22, 0x00, 0x07, 0x0b, 0x27, 0x00, 0xac,
        0x02, 0x02, 0x81, 0x64, 0x0c, 0x21, 0x10}},
      {D12_AGX3_I32_COMPARE_SLT, AGX3_I32_COMPARE_SLT, 0, 0, 4,
       {0x02, 0x01, 0x1f, 0x05, 0x22, 0x85, 0x07, 0xc0, 0x20, 0x87},
       {0x22, 0x00, 0x47, 0x0b, 0x27, 0x00, 0xac,
        0x02, 0x02, 0x81, 0x64, 0x0c, 0x21, 0x10}},
      {D12_AGX3_I32_COMPARE_EQ, AGX3_I32_COMPARE_EQ, 42, 86, 14,
       {0x52, 0x57, 0x5f, 0x0f, 0x26, 0x85, 0x07, 0xc0, 0x20, 0x87},
       {0x32, 0x02, 0x07, 0xab, 0x27, 0x00, 0xad,
        0x32, 0x82, 0x83, 0x64, 0x0c, 0x21, 0x10}},
      {D12_AGX3_I32_COMPARE_SLT, AGX3_I32_COMPARE_SLT, 126, 126, 126,
       {0xf2, 0x7f, 0xdf, 0x7f, 0x22, 0x85, 0x07, 0xc0, 0x20, 0x87},
       {0xb2, 0x02, 0xc7, 0xeb, 0x27, 0x80, 0xad,
        0x3a, 0x82, 0x9f, 0x64, 0x0c, 0x21, 0x10}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected = target->encoding == AGX_ISA_ENCODING_AGX3B
                                      ? vectors[v].agx3b
                                      : vectors[v].agx3c;
         const uint8_t expected_length =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? 10 : 14;
         struct agx3_encoded_i32_compare_select_imm_5_7_reg_reg shared;
         struct d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding encoded;
         enum d12_agx3_i32_compare_predicate predicate =
            (enum d12_agx3_i32_compare_predicate)99;
         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;

         CHECK(agx3_pack_i32_compare_select_imm_5_7_reg_reg(
            target->encoding, vectors[v].shared_predicate, vectors[v].dest,
            vectors[v].src0, vectors[v].src1, &shared));
         CHECK(shared.length == expected_length);
         CHECK(memcmp(shared.bytes, expected, expected_length) == 0);
         CHECK(d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
            target->chip, target->encoding, vectors[v].predicate,
            vectors[v].dest, vectors[v].src0, vectors[v].src1, &encoded));
         CHECK(encoded.length == expected_length);
         CHECK(memcmp(encoded.bytes, expected, expected_length) == 0);
         for (uint8_t i = expected_length;
              i < D12_AGX3_I32_COMPARE_SELECT_IMM_5_7_REG_REG_MAX_LENGTH;
              ++i)
            CHECK(encoded.bytes[i] == 0);
         CHECK(d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &predicate, &dest, &src0, &src1));
         CHECK(predicate == vectors[v].predicate);
         CHECK(dest == vectors[v].dest);
         CHECK(src0 == vectors[v].src0);
         CHECK(src1 == vectors[v].src1);
      }
   }
   return 0;
}

static int
check_i32_compare_select_imm_5_7_operand_domain(void)
{
   static const struct {
      enum d12_agx3_i32_compare_predicate d12;
      enum agx3_i32_compare_predicate shared;
   } predicates[] = {
      {D12_AGX3_I32_COMPARE_EQ, AGX3_I32_COMPARE_EQ},
      {D12_AGX3_I32_COMPARE_SLT, AGX3_I32_COMPARE_SLT},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned p = 0; p < sizeof(predicates) / sizeof(predicates[0]);
           ++p) {
         for (uint16_t dest_in = 0; dest_in < 128; dest_in += 2) {
            for (uint16_t src0_in = 0; src0_in < 128; src0_in += 2) {
               for (uint16_t src1_in = 0; src1_in < 128; src1_in += 2) {
                  struct agx3_encoded_i32_compare_select_imm_5_7_reg_reg
                     shared;
                  struct
                     d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding
                        encoded;
                  enum d12_agx3_i32_compare_predicate predicate =
                     (enum d12_agx3_i32_compare_predicate)99;
                  uint16_t dest = UINT16_MAX, src0 = UINT16_MAX,
                           src1 = UINT16_MAX;

                  CHECK(agx3_pack_i32_compare_select_imm_5_7_reg_reg(
                     target->encoding, predicates[p].shared, dest_in, src0_in,
                     src1_in, &shared));
                  CHECK(d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
                     target->chip, target->encoding, predicates[p].d12,
                     dest_in, src0_in, src1_in, &encoded));
                  CHECK(encoded.length == shared.length);
                  CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) ==
                        0);
                  CHECK(d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
                     target->chip, target->encoding, encoded.bytes,
                     encoded.length, &predicate, &dest, &src0, &src1));
                  CHECK(predicate == predicates[p].d12);
                  CHECK(dest == dest_in);
                  CHECK(src0 == src0_in);
                  CHECK(src1 == src1_in);
               }
            }
         }
      }
   }
   return 0;
}

static int
check_i32_compare_select_imm_5_7_canonical_bit_domain(void)
{
   static const uint8_t masks[][14] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x04, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x90, 0x02, 0xc0, 0xe0, 0x00, 0x80, 0x01,
       0x38, 0x80, 0x1f, 0x00, 0x00, 0x00, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const uint16_t states[][3] = {{0, 0, 0}, {126, 126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned state = 0; state < 2; ++state) {
         struct d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding
            canonical;
         CHECK(d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
            representatives[r].chip, representatives[r].encoding,
            D12_AGX3_I32_COMPARE_EQ, states[state][0], states[state][1],
            states[state][2], &canonical));
         for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
            uint8_t mutated[14] = {0};
            memcpy(mutated, canonical.bytes, canonical.length);
            mutated[bit / 8] ^= 1u << (bit % 8);
            enum d12_agx3_i32_compare_predicate predicate =
               (enum d12_agx3_i32_compare_predicate)99;
            uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
            const bool accepted =
               d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  mutated, canonical.length, &predicate, &dest, &src0,
                  &src1);
            const bool is_operand_or_predicate =
               (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand_or_predicate);
            if (accepted) {
               struct d12_agx3_i32_compare_select_imm_5_7_reg_reg_encoding
                  repacked;
               CHECK(d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
                  representatives[r].chip, representatives[r].encoding,
                  predicate, dest, src0, src1, &repacked));
               CHECK(memcmp(repacked.bytes, mutated, canonical.length) == 0);
            } else {
               CHECK(predicate == D12_AGX3_I32_COMPARE_EQ);
               CHECK(dest == 0 && src0 == 0 && src1 == 0);
            }
         }
      }
   }
   return 0;
}

static int
check_i32_compare_select_imm_5_7_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t canonical_b[] = {
      0x02, 0x01, 0x1f, 0x05, 0x26, 0x85, 0x07, 0xc0, 0x20, 0x87,
   };
   static const uint8_t canonical_c[] = {
      0x22, 0x00, 0x07, 0x0b, 0x27, 0x00, 0xac,
      0x02, 0x02, 0x81, 0x64, 0x0c, 0x21, 0x10,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical = target->encoding == AGX_ISA_ENCODING_AGX3B
                                    ? canonical_b
                                    : canonical_c;
      const uint32_t canonical_length =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? sizeof(canonical_b)
                                                    : sizeof(canonical_c);
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
                  target->chip, all_encodings[e], D12_AGX3_I32_COMPARE_EQ,
                  0, 0, 4) == 0);
         CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  canonical_length) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e],
                  D12_AGX3_I32_COMPARE_EQ, 0, 0, 4) == 0);
         CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
               representatives[r].chip, representatives[r].encoding,
               (enum d12_agx3_i32_compare_predicate)-1, 0, 0, 4) == 0);
      CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
               representatives[r].chip, representatives[r].encoding,
               (enum d12_agx3_i32_compare_predicate)2, 0, 0, 4) == 0);
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  D12_AGX3_I32_COMPARE_EQ, invalid_values[value], 0, 4) ==
               0);
         CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  D12_AGX3_I32_COMPARE_EQ, 0, invalid_values[value], 4) ==
               0);
         CHECK(check_i32_compare_select_imm_5_7_rejected_and_cleared(
                  representatives[r].chip, representatives[r].encoding,
                  D12_AGX3_I32_COMPARE_SLT, 0, 0, invalid_values[value]) ==
               0);
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) ==
            0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) ==
            0);

   uint8_t appended[sizeof(canonical_c) + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t related_b[][10] = {
      {0x42, 0x05, 0x07, 0x01, 0x22, 0x85, 0x07, 0xc0, 0x20, 0x87},
      {0x52, 0x07, 0x07, 0x05, 0x22, 0x85, 0x07, 0xc0, 0x20, 0x87},
      {0x42, 0x81, 0x07, 0x8f, 0x22, 0x82, 0x07, 0x00, 0x20, 0x84},
      {0x02, 0x01, 0x1f, 0x05, 0x82, 0x08, 0x07, 0xc0, 0x80, 0x06},
   };
   static const uint8_t related_c[][14] = {
      {0x22, 0x00, 0x47, 0x2b, 0x23, 0x00, 0xac,
       0x12, 0x02, 0x80, 0x44, 0x0c, 0x21, 0x10},
      {0x32, 0x02, 0x47, 0x2b, 0x23, 0x00, 0xac,
       0x12, 0x02, 0x81, 0x44, 0x0c, 0x21, 0x10},
      {0x22, 0x00, 0x47, 0x0a, 0x27, 0x00, 0xac,
       0x02, 0x0a, 0x81, 0xa0, 0x04, 0x20, 0x18},
      {0x32, 0x02, 0x47, 0x0a, 0x23, 0x00, 0xa0,
       0x02, 0x14, 0x01, 0xa1, 0x60, 0x22, 0x40},
   };
   for (unsigned i = 0; i < sizeof(related_b) / sizeof(related_b[0]); ++i) {
      CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, related_b[i],
               sizeof(related_b[i])) == 0);
      CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, related_c[i],
               sizeof(related_c[i])) == 0);
   }

   CHECK(!d12_agx3_encode_i32_compare_select_imm_5_7_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, D12_AGX3_I32_COMPARE_EQ,
      0, 0, 4, NULL));
   CHECK(check_i32_compare_select_imm_5_7_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   enum d12_agx3_i32_compare_predicate predicate =
      (enum d12_agx3_i32_compare_predicate)99;
   uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &dest, &src0, &src1));
   CHECK(dest == 0 && src0 == 0 && src1 == 0);
   predicate = (enum d12_agx3_i32_compare_predicate)99;
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &predicate, NULL, &src0, &src1));
   CHECK(predicate == D12_AGX3_I32_COMPARE_EQ && src0 == 0 && src1 == 0);
   predicate = (enum d12_agx3_i32_compare_predicate)99;
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &predicate, &dest, NULL, &src1));
   CHECK(predicate == D12_AGX3_I32_COMPARE_EQ && dest == 0 && src1 == 0);
   predicate = (enum d12_agx3_i32_compare_predicate)99;
   dest = src0 = src1 = UINT16_MAX;
   CHECK(!d12_agx3_decode_i32_compare_select_imm_5_7_reg_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &predicate, &dest, &src0, NULL));
   CHECK(predicate == D12_AGX3_I32_COMPARE_EQ && dest == 0 && src0 == 0);
   return 0;
}

static bool
f32_to_int_rtz_encoding_is_cleared(
   const struct d12_agx3_f32_to_int_rtz_reg_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_f32_to_int_rtz_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   enum d12_agx3_f32_to_int_rtz_mode mode, uint16_t dest, uint16_t src)
{
   struct d12_agx3_f32_to_int_rtz_reg_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));
   CHECK(!d12_agx3_encode_f32_to_int_rtz_reg(
      chip, encoding, mode, dest, src, &encoded));
   CHECK(f32_to_int_rtz_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_f32_to_int_rtz_decode_rejected_and_cleared(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size)
{
   enum d12_agx3_f32_to_int_rtz_mode mode =
      (enum d12_agx3_f32_to_int_rtz_mode)99;
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_to_int_rtz_reg(
      chip, encoding, instruction, instruction_size, &mode, &dest, &src));
   CHECK(mode == D12_AGX3_F32_TO_I32_RTZ);
   CHECK(dest == 0 && src == 0);
   return 0;
}

static int
check_f32_to_int_rtz_exact_witnesses(void)
{
   static const struct {
      enum d12_agx3_f32_to_int_rtz_mode d3d_mode;
      enum agx3_f32_to_int_rtz_mode shared_mode;
      uint16_t dest;
      uint16_t src;
      uint8_t agx3b[D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH];
      uint8_t agx3c[D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH];
   } vectors[] = {
      {D12_AGX3_F32_TO_I32_RTZ, AGX3_F32_TO_I32_RTZ, 0, 0,
       {0x27, 0x07, 0x56, 0x00, 0x02, 0x00, 0xb4, 0x48, 0x03, 0x00},
       {0x2f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01}},
      {D12_AGX3_F32_TO_I32_RTZ, AGX3_F32_TO_I32_RTZ, 2, 0,
       {0x27, 0x07, 0x56, 0x02, 0x02, 0x00, 0xb4, 0x48, 0x03, 0x00},
       {0x3f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01}},
      {D12_AGX3_F32_TO_I32_RTZ, AGX3_F32_TO_I32_RTZ, 0, 2,
       {0x27, 0x07, 0x56, 0x00, 0x02, 0x04, 0xb4, 0x48, 0x03, 0x00},
       {0x2f, 0x02, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01}},
      {D12_AGX3_F32_TO_I32_RTZ, AGX3_F32_TO_I32_RTZ, 4, 10,
       {0x27, 0x07, 0x56, 0x04, 0x02, 0x14, 0xb4, 0x48, 0x03, 0x00},
       {0x2f, 0x02, 0x00, 0x5a, 0x22, 0x00, 0xae, 0x0a, 0x70, 0x01}},
      {D12_AGX3_F32_TO_I32_RTZ, AGX3_F32_TO_I32_RTZ, 126, 126,
       {0x27, 0x07, 0x56, 0x7e, 0x02, 0xfc, 0xb4, 0x48, 0x03, 0x00},
       {0xbf, 0x02, 0x80, 0xfa, 0x22, 0x80, 0xae, 0x3a, 0x71, 0x01}},
      {D12_AGX3_F32_TO_U32_RTZ, AGX3_F32_TO_U32_RTZ, 0, 0,
       {0x27, 0x07, 0x56, 0x00, 0x02, 0x00, 0xb4, 0x08, 0x02, 0x00},
       {0x2f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0xb0, 0x03}},
      {D12_AGX3_F32_TO_U32_RTZ, AGX3_F32_TO_U32_RTZ, 2, 0,
       {0x27, 0x07, 0x56, 0x02, 0x02, 0x00, 0xb4, 0x08, 0x02, 0x00},
       {0x3f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0xb0, 0x03}},
      {D12_AGX3_F32_TO_U32_RTZ, AGX3_F32_TO_U32_RTZ, 0, 2,
       {0x27, 0x07, 0x56, 0x00, 0x02, 0x04, 0xb4, 0x08, 0x02, 0x00},
       {0x2f, 0x02, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0xb0, 0x03}},
      {D12_AGX3_F32_TO_U32_RTZ, AGX3_F32_TO_U32_RTZ, 4, 10,
       {0x27, 0x07, 0x56, 0x04, 0x02, 0x14, 0xb4, 0x08, 0x02, 0x00},
       {0x2f, 0x02, 0x00, 0x5a, 0x22, 0x00, 0xae, 0x0a, 0xb0, 0x03}},
      {D12_AGX3_F32_TO_U32_RTZ, AGX3_F32_TO_U32_RTZ, 126, 126,
       {0x27, 0x07, 0x56, 0x7e, 0x02, 0xfc, 0xb4, 0x08, 0x02, 0x00},
       {0xbf, 0x02, 0x80, 0xfa, 0x22, 0x80, 0xae, 0x3a, 0xb1, 0x03}},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
         const uint8_t *expected =
            target->encoding == AGX_ISA_ENCODING_AGX3B ? vectors[v].agx3b
                                                       : vectors[v].agx3c;
         struct agx3_encoded_f32_to_int_rtz_reg shared;
         struct d12_agx3_f32_to_int_rtz_reg_encoding encoded;
         enum d12_agx3_f32_to_int_rtz_mode mode =
            (enum d12_agx3_f32_to_int_rtz_mode)99;
         uint16_t dest = UINT16_MAX, src = UINT16_MAX;

         CHECK(agx3_pack_f32_to_int_rtz_reg(
            target->encoding, vectors[v].shared_mode, vectors[v].dest,
            vectors[v].src, &shared));
         CHECK(shared.length == D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH);
         CHECK(memcmp(shared.bytes, expected, shared.length) == 0);
         CHECK(d12_agx3_encode_f32_to_int_rtz_reg(
            target->chip, target->encoding, vectors[v].d3d_mode,
            vectors[v].dest, vectors[v].src, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, expected, encoded.length) == 0);
         CHECK(d12_agx3_decode_f32_to_int_rtz_reg(
            target->chip, target->encoding, encoded.bytes, encoded.length,
            &mode, &dest, &src));
         CHECK(mode == vectors[v].d3d_mode);
         CHECK(dest == vectors[v].dest);
         CHECK(src == vectors[v].src);
      }
   }
   return 0;
}

static int
check_f32_to_int_rtz_operand_domain(void)
{
   static const struct {
      enum d12_agx3_f32_to_int_rtz_mode d3d;
      enum agx3_f32_to_int_rtz_mode shared;
   } modes[] = {
      {D12_AGX3_F32_TO_I32_RTZ, AGX3_F32_TO_I32_RTZ},
      {D12_AGX3_F32_TO_U32_RTZ, AGX3_F32_TO_U32_RTZ},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
         for (uint16_t dest_in = 0; dest_in < 128; dest_in += 2) {
            for (uint16_t src_in = 0; src_in < 128; src_in += 2) {
               struct agx3_encoded_f32_to_int_rtz_reg shared;
               struct d12_agx3_f32_to_int_rtz_reg_encoding encoded;
               enum d12_agx3_f32_to_int_rtz_mode mode =
                  (enum d12_agx3_f32_to_int_rtz_mode)99;
               uint16_t dest = UINT16_MAX, src = UINT16_MAX;
               CHECK(agx3_pack_f32_to_int_rtz_reg(
                  target->encoding, modes[m].shared, dest_in, src_in,
                  &shared));
               CHECK(d12_agx3_encode_f32_to_int_rtz_reg(
                  target->chip, target->encoding, modes[m].d3d, dest_in,
                  src_in, &encoded));
               CHECK(encoded.length == shared.length);
               CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
               CHECK(d12_agx3_decode_f32_to_int_rtz_reg(
                  target->chip, target->encoding, encoded.bytes,
                  encoded.length, &mode, &dest, &src));
               CHECK(mode == modes[m].d3d);
               CHECK(dest == dest_in);
               CHECK(src == src_in);
            }
         }
      }
   }
   return 0;
}

static int
check_f32_to_int_rtz_canonical_bit_domain(void)
{
   static const uint8_t masks[][D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xe0, 0x00, 0x80, 0x00, 0x38, 0x01, 0x00},
   };
   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   static const enum d12_agx3_f32_to_int_rtz_mode modes[] = {
      D12_AGX3_F32_TO_I32_RTZ, D12_AGX3_F32_TO_U32_RTZ,
   };
   static const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
         for (unsigned state = 0; state < 2; ++state) {
            struct d12_agx3_f32_to_int_rtz_reg_encoding canonical;
            CHECK(d12_agx3_encode_f32_to_int_rtz_reg(
               representatives[r].chip, representatives[r].encoding,
               modes[m], states[state][0], states[state][1], &canonical));
            for (unsigned bit = 0; bit < canonical.length * 8; ++bit) {
               uint8_t mutated[D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH] = {0};
               memcpy(mutated, canonical.bytes, canonical.length);
               mutated[bit / 8] ^= 1u << (bit % 8);
               enum d12_agx3_f32_to_int_rtz_mode mode =
                  (enum d12_agx3_f32_to_int_rtz_mode)99;
               uint16_t dest = UINT16_MAX, src = UINT16_MAX;
               const bool accepted = d12_agx3_decode_f32_to_int_rtz_reg(
                  representatives[r].chip, representatives[r].encoding,
                  mutated, canonical.length, &mode, &dest, &src);
               const bool is_operand =
                  (masks[r][bit / 8] & (1u << (bit % 8))) != 0;
               CHECK(accepted == is_operand);
               if (accepted) {
                  struct d12_agx3_f32_to_int_rtz_reg_encoding repacked;
                  CHECK(mode == modes[m]);
                  CHECK(d12_agx3_encode_f32_to_int_rtz_reg(
                     representatives[r].chip, representatives[r].encoding,
                     mode, dest, src, &repacked));
                  CHECK(memcmp(repacked.bytes, mutated, canonical.length) ==
                        0);
               } else {
                  CHECK(mode == D12_AGX3_F32_TO_I32_RTZ);
                  CHECK(dest == 0 && src == 0);
               }
            }
         }
      }
   }
   return 0;
}

static int
check_f32_to_int_rtz_rejection(void)
{
   static const enum agx_isa_encoding all_encodings[] = {
      AGX_ISA_ENCODING_AGX2, AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C, AGX_ISA_ENCODING_UNKNOWN,
      (enum agx_isa_encoding)99,
   };
   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   static const enum d12_agx3_f32_to_int_rtz_mode modes[] = {
      D12_AGX3_F32_TO_I32_RTZ, D12_AGX3_F32_TO_U32_RTZ,
   };
   static const uint8_t canonical_b[] = {
      0x27, 0x07, 0x56, 0x00, 0x02, 0x00, 0xb4, 0x48, 0x03, 0x00,
   };
   static const uint8_t canonical_c[] = {
      0x2f, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01,
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const uint8_t *canonical =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         if (all_encodings[e] == target->encoding)
            continue;
         for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
            CHECK(check_f32_to_int_rtz_rejected_and_cleared(
                     target->chip, all_encodings[e], modes[m], 0, 0) == 0);
         CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
                  target->chip, all_encodings[e], canonical,
                  sizeof(canonical_b)) == 0);
      }
   }

   for (unsigned t = 0;
        t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++t) {
      for (unsigned e = 0; e < sizeof(all_encodings) / sizeof(all_encodings[0]);
           ++e) {
         for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
            CHECK(check_f32_to_int_rtz_rejected_and_cleared(
                     unsupported_targets[t], all_encodings[e], modes[m], 0,
                     0) == 0);
         CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
                  unsupported_targets[t], all_encodings[e], canonical_b,
                  sizeof(canonical_b)) == 0);
      }
   }

   static const struct integer_target representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C},
   };
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      CHECK(check_f32_to_int_rtz_rejected_and_cleared(
               representatives[r].chip, representatives[r].encoding,
               (enum d12_agx3_f32_to_int_rtz_mode)-1, 0, 0) == 0);
      CHECK(check_f32_to_int_rtz_rejected_and_cleared(
               representatives[r].chip, representatives[r].encoding,
               (enum d12_agx3_f32_to_int_rtz_mode)2, 0, 0) == 0);
      for (unsigned value = 0;
           value < sizeof(invalid_values) / sizeof(invalid_values[0]);
           ++value) {
         for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
            CHECK(check_f32_to_int_rtz_rejected_and_cleared(
                     representatives[r].chip, representatives[r].encoding,
                     modes[m], invalid_values[value], 0) == 0);
            CHECK(check_f32_to_int_rtz_rejected_and_cleared(
                     representatives[r].chip, representatives[r].encoding,
                     modes[m], 0, invalid_values[value]) == 0);
         }
      }
   }

   for (uint32_t size = 0; size < sizeof(canonical_b); ++size)
      CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
               AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_b, size) == 0);
   for (uint32_t size = 0; size < sizeof(canonical_c); ++size)
      CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
               AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c, size) == 0);

   uint8_t appended[D12_AGX3_F32_TO_INT_RTZ_REG_LENGTH + 1];
   memcpy(appended, canonical_c, sizeof(canonical_c));
   appended[sizeof(canonical_c)] = 0xff;
   CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, appended,
            sizeof(appended)) == 0);
   CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
            AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, canonical_c,
            sizeof(canonical_c)) == 0);
   CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_b,
            sizeof(canonical_b)) == 0);

   static const uint8_t signed_source_reuse_b[] = {
      0x27, 0x07, 0x54, 0x00, 0x02, 0x00, 0xb4, 0x48, 0x03, 0x00,
   };
   static const uint8_t signed_source_reuse_c[] = {
      0x27, 0x00, 0x00, 0x1a, 0x22, 0x00, 0xae, 0x02, 0x70, 0x01,
   };
   static const uint8_t unsigned_division_b[] = {
      0x27, 0x07, 0x54, 0x0e, 0x03, 0x2c, 0x96, 0x08, 0x02, 0x00,
   };
   static const uint8_t unsigned_division_c[] = {
      0x37, 0x02, 0x00, 0xba, 0x2a, 0x00, 0xae, 0x1a, 0x90, 0x03,
   };
   static const uint8_t int_to_float_b[] = {
      0xa7, 0x07, 0x56, 0x00, 0x02, 0x00, 0xac, 0x60,
   };
   static const uint8_t int_to_float_c[] = {
      0x2f, 0x00, 0x00, 0x02, 0x22, 0x80, 0xaf, 0x02, 0x68, 0x00,
   };
   const struct {
      enum agx_chip chip;
      enum agx_isa_encoding encoding;
      const uint8_t *bytes;
      uint32_t length;
   } related[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, signed_source_reuse_b,
       sizeof(signed_source_reuse_b)},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, signed_source_reuse_c,
       sizeof(signed_source_reuse_c)},
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, unsigned_division_b,
       sizeof(unsigned_division_b)},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, unsigned_division_c,
       sizeof(unsigned_division_c)},
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, int_to_float_b,
       sizeof(int_to_float_b)},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, int_to_float_c,
       sizeof(int_to_float_c)},
   };
   for (unsigned i = 0; i < sizeof(related) / sizeof(related[0]); ++i)
      CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
               related[i].chip, related[i].encoding, related[i].bytes,
               related[i].length) == 0);

   struct d12_agx3_f32_to_int_rtz_reg_encoding encoded;
   CHECK(!d12_agx3_encode_f32_to_int_rtz_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, D12_AGX3_F32_TO_I32_RTZ,
      0, 0, NULL));
   CHECK(check_f32_to_int_rtz_decode_rejected_and_cleared(
            AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, NULL,
            sizeof(canonical_c)) == 0);

   enum d12_agx3_f32_to_int_rtz_mode mode =
      (enum d12_agx3_f32_to_int_rtz_mode)99;
   uint16_t dest = UINT16_MAX, src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_to_int_rtz_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), NULL, &dest, &src));
   CHECK(dest == 0 && src == 0);
   mode = (enum d12_agx3_f32_to_int_rtz_mode)99;
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_to_int_rtz_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &mode, NULL, &src));
   CHECK(mode == D12_AGX3_F32_TO_I32_RTZ && src == 0);
   mode = (enum d12_agx3_f32_to_int_rtz_mode)99;
   dest = src = UINT16_MAX;
   CHECK(!d12_agx3_decode_f32_to_int_rtz_reg(
      AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, canonical_c,
      sizeof(canonical_c), &mode, &dest, NULL));
   CHECK(mode == D12_AGX3_F32_TO_I32_RTZ && dest == 0);
   (void)encoded;
   return 0;
}

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

static bool
end_wait_encoding_is_cleared(
   const struct d12_agx3_end_with_wait_fence_mask_encoding *encoded)
{
   if (encoded->length != 0)
      return false;

   for (unsigned i = 0; i < D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH; ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }

   return true;
}

static int
check_end_wait_rejected_and_cleared(enum agx_chip chip, uint8_t mask)
{
   struct d12_agx3_end_with_wait_fence_mask_encoding encoded;
   memset(&encoded, 0xff, sizeof(encoded));

   CHECK(!d12_agx3_encode_end_with_wait_fence_mask(chip, mask, &encoded));
   CHECK(end_wait_encoding_is_cleared(&encoded));
   return 0;
}

static int
check_end_wait_exact_witnesses(void)
{
   static const struct {
      enum agx_chip chip;
      enum agx_isa_encoding encoding;
      uint8_t mask;
      uint8_t bytes[D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH];
   } witnesses[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, 0x00,
       {0x0e, 0x00, 0x00, 0x00}},
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B, 0x01,
       {0x0e, 0x04, 0x00, 0x00}},
      {AGX_CHIP_G16G, AGX_ISA_ENCODING_AGX3B, 0x20,
       {0x0e, 0x80, 0x00, 0x00}},
      {AGX_CHIP_G17P, AGX_ISA_ENCODING_AGX3B, 0x3f,
       {0x0e, 0xfc, 0x00, 0x00}},
      {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, 0x40,
       {0x0e, 0x00, 0x02, 0x00}},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0x80,
       {0x0e, 0x00, 0x04, 0x00}},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 0xff,
       {0x0e, 0xfc, 0x06, 0x00}},
   };

   for (unsigned w = 0; w < sizeof(witnesses) / sizeof(witnesses[0]); ++w) {
      struct agx3_encoded_end shared;
      struct d12_agx3_end_with_wait_fence_mask_encoding encoded;
      uint8_t decoded_mask = UINT8_MAX;

      CHECK(agx3_pack_end_with_wait_fence_mask(
         witnesses[w].encoding, witnesses[w].mask, &shared));
      CHECK(shared.length == sizeof(witnesses[w].bytes));
      CHECK(memcmp(shared.bytes, witnesses[w].bytes,
                   sizeof(witnesses[w].bytes)) == 0);
      CHECK(d12_agx3_encode_end_with_wait_fence_mask(
         witnesses[w].chip, witnesses[w].mask, &encoded));
      CHECK(encoded.length == sizeof(witnesses[w].bytes));
      CHECK(memcmp(encoded.bytes, witnesses[w].bytes,
                   sizeof(witnesses[w].bytes)) == 0);
      CHECK(d12_agx3_decode_end_with_wait_fence_mask(
         witnesses[w].chip, witnesses[w].bytes, sizeof(witnesses[w].bytes),
         &decoded_mask));
      CHECK(decoded_mask == witnesses[w].mask);
   }

   return 0;
}

static int
check_end_wait_mask_domain(void)
{
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      const unsigned mask_count =
         target->encoding == AGX_ISA_ENCODING_AGX3B ? 64 : 256;

      for (unsigned expected_mask = 0; expected_mask < mask_count;
           ++expected_mask) {
         struct agx3_encoded_end shared;
         struct d12_agx3_end_with_wait_fence_mask_encoding encoded;
         uint8_t shared_mask = UINT8_MAX;
         uint8_t decoded_mask = UINT8_MAX;
         uint8_t shared_length = 0;

         CHECK(agx3_pack_end_with_wait_fence_mask(
            target->encoding, expected_mask, &shared));
         CHECK(d12_agx3_encode_end_with_wait_fence_mask(
            target->chip, expected_mask, &encoded));
         CHECK(encoded.length == shared.length);
         CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
         CHECK(agx3_unpack_end_with_wait_fence_mask(
            target->encoding, encoded.bytes, encoded.length, &shared_mask,
            &shared_length));
         CHECK(shared_mask == expected_mask);
         CHECK(shared_length == encoded.length);
         CHECK(d12_agx3_decode_end_with_wait_fence_mask(
            target->chip, encoded.bytes, encoded.length, &decoded_mask));
         CHECK(decoded_mask == expected_mask);
      }
   }

   return 0;
}

static int
check_end_wait_canonical_bit_domain(void)
{
   static const struct {
      enum agx_chip chip;
      enum agx_isa_encoding encoding;
      unsigned mask_count;
      uint32_t mutable_bits;
   } representatives[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, 64, 0x0000fc00u},
      {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, 256, 0x0006fc00u},
   };

   for (unsigned r = 0;
        r < sizeof(representatives) / sizeof(representatives[0]); ++r) {
      for (unsigned mask = 0; mask < representatives[r].mask_count; ++mask) {
         struct agx3_encoded_end canonical;
         CHECK(agx3_pack_end_with_wait_fence_mask(
            representatives[r].encoding, mask, &canonical));

         for (unsigned bit = 0;
              bit < D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH * 8; ++bit) {
            uint8_t mutated[D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH];
            memcpy(mutated, canonical.bytes, sizeof(mutated));
            mutated[bit / 8] ^= 1u << (bit % 8);

            const bool modeled =
               (representatives[r].mutable_bits & (1u << bit)) != 0;
            uint8_t decoded_mask = UINT8_MAX;
            const bool accepted =
               d12_agx3_decode_end_with_wait_fence_mask(
                  representatives[r].chip, mutated, sizeof(mutated),
                  &decoded_mask);
            CHECK(accepted == modeled);

            if (accepted) {
               const unsigned slot = bit <= 15 ? bit - 10 : bit - 11;
               struct agx3_encoded_end shared;
               struct d12_agx3_end_with_wait_fence_mask_encoding repacked;
               uint8_t shared_mask = UINT8_MAX;
               uint8_t shared_length = 0;

               CHECK(decoded_mask == (mask ^ (1u << slot)));
               CHECK(agx3_unpack_end_with_wait_fence_mask(
                  representatives[r].encoding, mutated, sizeof(mutated),
                  &shared_mask, &shared_length));
               CHECK(shared_mask == decoded_mask);
               CHECK(shared_length == sizeof(mutated));
               CHECK(d12_agx3_encode_end_with_wait_fence_mask(
                  representatives[r].chip, decoded_mask, &repacked));
               CHECK(repacked.length == shared_length);
               CHECK(memcmp(repacked.bytes, mutated, shared_length) == 0);
               CHECK(agx3_pack_end_with_wait_fence_mask(
                  representatives[r].encoding, decoded_mask, &shared));
               CHECK(memcmp(shared.bytes, repacked.bytes, shared.length) ==
                     0);
            } else {
               CHECK(decoded_mask == 0);
            }
         }
      }
   }

   return 0;
}

static int
check_end_wait_rejection_domain(void)
{
   struct agx3_encoded_end packed_b, packed_c;
   CHECK(agx3_pack_end_with_wait_fence_mask(AGX_ISA_ENCODING_AGX3B, 0x3f,
                                            &packed_b));
   CHECK(agx3_pack_end_with_wait_fence_mask(AGX_ISA_ENCODING_AGX3C, 0xff,
                                            &packed_c));

   /* Slots 6 and 7 are compiler-rejected AGX3b table gaps.  Exercise every
    * raw mask using either bit on all seven independently witnessed targets. */
   for (unsigned raw_mask = 0x40; raw_mask <= 0xff; ++raw_mask) {
      uint8_t invalid[D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH] = {
         0x0e,
         (raw_mask & 0x3f) << 2,
         ((raw_mask >> 6) & 0x03) << 1,
         0x00,
      };

      for (unsigned t = 0;
           t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
         if (integer_targets[t].encoding != AGX_ISA_ENCODING_AGX3B)
            continue;

         CHECK(check_end_wait_rejected_and_cleared(
                  integer_targets[t].chip, raw_mask) == 0);
         uint8_t decoded_mask = UINT8_MAX;
         CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
            integer_targets[t].chip, invalid, sizeof(invalid),
            &decoded_mask));
         CHECK(decoded_mask == 0);
      }
   }

   for (uint32_t size = 0;
        size < D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH; ++size) {
      uint8_t decoded_mask = UINT8_MAX;
      CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
         AGX_CHIP_G15G, packed_b.bytes, size, &decoded_mask));
      CHECK(decoded_mask == 0);
      decoded_mask = UINT8_MAX;
      CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
         AGX_CHIP_G17G, packed_c.bytes, size, &decoded_mask));
      CHECK(decoded_mask == 0);
   }

   uint8_t appended_b[D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH + 1];
   uint8_t appended_c[D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH + 1];
   memcpy(appended_b, packed_b.bytes, packed_b.length);
   memcpy(appended_c, packed_c.bytes, packed_c.length);
   appended_b[packed_b.length] = 0xa5;
   appended_c[packed_c.length] = 0x5a;
   uint8_t decoded_mask = UINT8_MAX;
   CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
      AGX_CHIP_G15G, appended_b, sizeof(appended_b), &decoded_mask));
   CHECK(decoded_mask == 0);
   decoded_mask = UINT8_MAX;
   CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
      AGX_CHIP_G17G, appended_c, sizeof(appended_c), &decoded_mask));
   CHECK(decoded_mask == 0);

   static const uint8_t
      wrong_forms[][D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH] = {
      {0x0f, 0x00, 0x00, 0x00},
      {0x0e, 0x01, 0x00, 0x00},
      {0x0e, 0x00, 0x01, 0x00},
      {0x0e, 0x00, 0x00, 0x01},
   };
   for (unsigned i = 0; i < sizeof(wrong_forms) / sizeof(wrong_forms[0]);
        ++i) {
      decoded_mask = UINT8_MAX;
      CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
         AGX_CHIP_G15G, wrong_forms[i], sizeof(wrong_forms[i]),
         &decoded_mask));
      CHECK(decoded_mask == 0);
      decoded_mask = UINT8_MAX;
      CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
         AGX_CHIP_G17G, wrong_forms[i], sizeof(wrong_forms[i]),
         &decoded_mask));
      CHECK(decoded_mask == 0);
   }

   /* C-only high-slot encodings reject on every B target.  Low-slot bytes
    * are intentionally identical between generations and valid on C too. */
   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      decoded_mask = UINT8_MAX;
      if (integer_targets[t].encoding == AGX_ISA_ENCODING_AGX3B) {
         CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
            integer_targets[t].chip, packed_c.bytes, packed_c.length,
            &decoded_mask));
         CHECK(decoded_mask == 0);
      } else {
         CHECK(d12_agx3_decode_end_with_wait_fence_mask(
            integer_targets[t].chip, packed_b.bytes, packed_b.length,
            &decoded_mask));
         CHECK(decoded_mask == 0x3f);
      }
   }

   static const enum agx_chip unsupported_targets[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
      AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0;
        i < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
        ++i) {
      CHECK(check_end_wait_rejected_and_cleared(unsupported_targets[i], 0) ==
            0);
      decoded_mask = UINT8_MAX;
      CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
         unsupported_targets[i], packed_b.bytes, packed_b.length,
         &decoded_mask));
      CHECK(decoded_mask == 0);
      decoded_mask = UINT8_MAX;
      CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
         unsupported_targets[i], packed_c.bytes, packed_c.length,
         &decoded_mask));
      CHECK(decoded_mask == 0);
   }

   CHECK(check_end_wait_rejected_and_cleared((enum agx_chip)99, 0) == 0);
   CHECK(!d12_agx3_encode_end_with_wait_fence_mask(AGX_CHIP_G17G, 0,
                                                   NULL));
   decoded_mask = UINT8_MAX;
   CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
      AGX_CHIP_G17G, NULL, packed_c.length, &decoded_mask));
   CHECK(decoded_mask == 0);
   CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
      AGX_CHIP_G17G, packed_c.bytes, packed_c.length, NULL));
   decoded_mask = UINT8_MAX;
   CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
      AGX_CHIP_G17G, packed_c.bytes, UINT32_MAX, &decoded_mask));
   CHECK(decoded_mask == 0);
   decoded_mask = UINT8_MAX;
   CHECK(!d12_agx3_decode_end_with_wait_fence_mask(
      (enum agx_chip)99, packed_c.bytes, packed_c.length, &decoded_mask));
   CHECK(decoded_mask == 0);

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

static bool
cf_encoding_is_cleared(const struct d12_agx3_cf_encoding *encoded)
{
   if (encoded->length != 0)
      return false;
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i) {
      if (encoded->bytes[i] != 0)
         return false;
   }
   return true;
}

static int
check_cf_encoding(const struct d12_agx3_cf_encoding *encoded,
                  const uint8_t *expected, uint8_t expected_length)
{
   CHECK(encoded->length == expected_length);
   CHECK(memcmp(encoded->bytes, expected, expected_length) == 0);
   for (unsigned i = expected_length; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   return 0;
}

static int
check_control_flow_bridge(void)
{
   static const struct {
      enum d12_agx3_i32_cf_predicate d3d;
      enum agx3_cf_icond apple;
   } integer_predicates[] = {
      {D12_AGX3_I32_CF_EQ, AGX3_CF_ICOND_EQ},
      {D12_AGX3_I32_CF_ULT, AGX3_CF_ICOND_ULT},
      {D12_AGX3_I32_CF_UGT, AGX3_CF_ICOND_UGT},
      {D12_AGX3_I32_CF_SLT, AGX3_CF_ICOND_SLT},
      {D12_AGX3_I32_CF_SGT, AGX3_CF_ICOND_SGT},
   };
   static const struct {
      enum d12_agx3_f32_cf_predicate d3d;
      enum agx3_cf_fcond apple;
      bool or_nan;
      bool flips_invert;
   } float_predicates[] = {
      {D12_AGX3_F32_CF_EQ, AGX3_CF_FCOND_EQ, false, false},
      {D12_AGX3_F32_CF_LT, AGX3_CF_FCOND_LT, false, false},
      {D12_AGX3_F32_CF_GT, AGX3_CF_FCOND_GT, false, false},
      {D12_AGX3_F32_CF_LT_OR_NAN, AGX3_CF_FCOND_LT, true, false},
      {D12_AGX3_F32_CF_GT_OR_NAN, AGX3_CF_FCOND_GT, true, false},
      {D12_AGX3_F32_CF_GE, AGX3_CF_FCOND_LT, true, true},
      {D12_AGX3_F32_CF_LE, AGX3_CF_FCOND_GT, true, true},
   };

   for (unsigned t = 0;
        t < sizeof(integer_targets) / sizeof(integer_targets[0]); ++t) {
      const struct integer_target *target = &integer_targets[t];
      struct d12_agx3_cf_encoding encoded;
      uint8_t expected[D12_AGX3_CF_MAX_LENGTH] = {0};
      uint8_t compare_length = 0, cnd_length = 0;

      for (unsigned p = 0;
           p < sizeof(integer_predicates) / sizeof(integer_predicates[0]);
           ++p) {
         for (unsigned invert = 0; invert < 2; ++invert) {
            memset(expected, 0, sizeof(expected));
            CHECK(agx3_cf_icmp_reg(target->encoding,
                                   integer_predicates[p].apple, 0, 0, 2,
                                   expected, &compare_length));
            CHECK(agx3_cf_cndst(target->encoding, 0, invert, 1,
                                expected + compare_length, &cnd_length));
            CHECK(d12_agx3_encode_i32_cf(
               target->chip, D12_AGX3_CF_IF, integer_predicates[p].d3d,
               invert, 0, false, 2, &encoded));
            CHECK(check_cf_encoding(&encoded, expected,
                                    compare_length + cnd_length) == 0);
         }
      }

      for (unsigned p = 0;
           p < sizeof(float_predicates) / sizeof(float_predicates[0]); ++p) {
         for (unsigned invert = 0; invert < 2; ++invert) {
            memset(expected, 0, sizeof(expected));
            CHECK(agx3_cf_fcmp_reg(target->encoding, float_predicates[p].apple,
                                   float_predicates[p].or_nan, 0, 0, 2,
                                   expected, &compare_length));
            CHECK(agx3_cf_cndst(
               target->encoding, 0,
               (bool)invert ^ float_predicates[p].flips_invert, 1,
               expected + compare_length, &cnd_length));
            CHECK(d12_agx3_encode_f32_cf(
               target->chip, D12_AGX3_CF_IF, float_predicates[p].d3d, invert,
               0, 2, &encoded));
            CHECK(check_cf_encoding(&encoded, expected,
                                    compare_length + cnd_length) == 0);
         }
      }

      /* Register-register if: integer condition and invert route directly to
       * the compare flag and cndst consumer. */
      CHECK(agx3_cf_icmp_reg(target->encoding, AGX3_CF_ICOND_ULT, 0, 0, 2,
                             expected, &compare_length));
      CHECK(agx3_cf_cndst(target->encoding, 0, true, 1,
                          expected + compare_length, &cnd_length));
      CHECK(d12_agx3_encode_i32_cf(
         target->chip, D12_AGX3_CF_IF, D12_AGX3_I32_CF_ULT, true, 0, false,
         2, &encoded));
      CHECK(check_cf_encoding(&encoded, expected,
                              compare_length + cnd_length) == 0);

      /* Immediate loop/break test: exact imm8 compare followed by cndlt. */
      memset(expected, 0, sizeof(expected));
      CHECK(agx3_cf_icmp_imm(target->encoding, AGX3_CF_ICOND_EQ, 0, 4, 7,
                             expected, &compare_length));
      CHECK(agx3_cf_cndlt(target->encoding, 0, false,
                          expected + compare_length, &cnd_length));
      CHECK(d12_agx3_encode_i32_cf(
         target->chip, D12_AGX3_CF_LOOP_OR_BREAK_TEST, D12_AGX3_I32_CF_EQ,
         false, 4, true, 7, &encoded));
      CHECK(check_cf_encoding(&encoded, expected,
                              compare_length + cnd_length) == 0);

      /* Ordered >= is invert(less-than-or-NaN), matching Mesa's production
       * normalization and Apple's witnessed sequence. */
      memset(expected, 0, sizeof(expected));
      CHECK(agx3_cf_fcmp_reg(target->encoding, AGX3_CF_FCOND_LT, true, 0, 0,
                             2, expected, &compare_length));
      CHECK(agx3_cf_cndst(target->encoding, 0, true, 1,
                          expected + compare_length, &cnd_length));
      CHECK(d12_agx3_encode_f32_cf(
         target->chip, D12_AGX3_CF_IF, D12_AGX3_F32_CF_GE, false, 0, 2,
         &encoded));
      CHECK(check_cf_encoding(&encoded, expected,
                              compare_length + cnd_length) == 0);

      const enum d12_agx3_cf_stack_op stack_ops[] = {
         D12_AGX3_CF_LOOP_ENTRY,
         D12_AGX3_CF_ELSE,
         D12_AGX3_CF_POP_1,
         D12_AGX3_CF_POP_2,
      };
      for (unsigned s = 0; s < sizeof(stack_ops) / sizeof(stack_ops[0]); ++s) {
         memset(expected, 0, sizeof(expected));
         uint8_t length = 0;
         bool ok;
         switch (stack_ops[s]) {
         case D12_AGX3_CF_LOOP_ENTRY:
            ok = agx3_cf_cndst(target->encoding, 6, false, 2, expected,
                               &length);
            break;
         case D12_AGX3_CF_ELSE:
            ok = agx3_cf_cndef(target->encoding, expected, &length);
            break;
         case D12_AGX3_CF_POP_1:
            ok = agx3_cf_cndend(target->encoding, 1, expected, &length);
            break;
         case D12_AGX3_CF_POP_2:
            ok = agx3_cf_cndend(target->encoding, 2, expected, &length);
            break;
         default:
            ok = false;
            break;
         }
         CHECK(ok);
         CHECK(d12_agx3_encode_cf_stack(target->chip, stack_ops[s], &encoded));
         CHECK(check_cf_encoding(&encoded, expected, length) == 0);
      }

      const struct {
         enum d12_agx3_cf_branch_op op;
         int64_t displacement;
         bool is_bn;
      } branches[] = {
         {D12_AGX3_CF_BRANCH_EXEC_NONE, 1208, true},
         {D12_AGX3_CF_BRANCH_EXEC_ANY, -62, false},
      };
      for (unsigned b = 0; b < sizeof(branches) / sizeof(branches[0]); ++b) {
         memset(expected, 0, sizeof(expected));
         uint8_t length = 0;
         CHECK(agx3_cf_branch(target->encoding, branches[b].is_bn, expected,
                              &length));
         CHECK(agx3_cf_patch_branch(target->encoding, branches[b].is_bn,
                                    expected, branches[b].displacement));
         CHECK(d12_agx3_encode_cf_branch(target->chip, branches[b].op,
                                         branches[b].displacement, &encoded));
         CHECK(check_cf_encoding(&encoded, expected, length) == 0);
      }
   }

   /* Every rejected call must clear a previously dirty result. */
   struct d12_agx3_cf_encoding rejected;
#define REJECT_CF(call)                                                       \
   do {                                                                        \
      memset(&rejected, 0xff, sizeof(rejected));                               \
      CHECK(!(call));                                                          \
      CHECK(cf_encoding_is_cleared(&rejected));                                \
   } while (0)
   REJECT_CF(d12_agx3_encode_i32_cf(
      AGX_CHIP_G17C, D12_AGX3_CF_IF, D12_AGX3_I32_CF_EQ, false, 0, false, 2,
      &rejected));
   REJECT_CF(d12_agx3_encode_i32_cf(
      AGX_CHIP_G17S, D12_AGX3_CF_IF, (enum d12_agx3_i32_cf_predicate)99,
      false, 0, false, 2, &rejected));
   REJECT_CF(d12_agx3_encode_i32_cf(
      AGX_CHIP_G17S, D12_AGX3_CF_IF, D12_AGX3_I32_CF_EQ, false, 1, false, 2,
      &rejected));
   REJECT_CF(d12_agx3_encode_i32_cf(
      AGX_CHIP_G17S, D12_AGX3_CF_IF, D12_AGX3_I32_CF_EQ, false, 0, true, 256,
      &rejected));
   REJECT_CF(d12_agx3_encode_f32_cf(
      AGX_CHIP_G15G, (enum d12_agx3_cf_condition_op)99, D12_AGX3_F32_CF_EQ,
      false, 0, 2, &rejected));
   REJECT_CF(d12_agx3_encode_f32_cf(
      AGX_CHIP_G15G, D12_AGX3_CF_IF, D12_AGX3_F32_CF_EQ, false, 0, 128,
      &rejected));
   REJECT_CF(d12_agx3_encode_cf_stack(
      AGX_CHIP_G17S, (enum d12_agx3_cf_stack_op)99, &rejected));
   REJECT_CF(d12_agx3_encode_cf_branch(
      AGX_CHIP_G17S, D12_AGX3_CF_BRANCH_EXEC_NONE, -2, &rejected));
   REJECT_CF(d12_agx3_encode_cf_branch(
      AGX_CHIP_G17S, D12_AGX3_CF_BRANCH_EXEC_ANY, 2, &rejected));
   REJECT_CF(d12_agx3_encode_cf_branch(
      AGX_CHIP_G17S, D12_AGX3_CF_BRANCH_EXEC_NONE, 3, &rejected));
#undef REJECT_CF

   CHECK(!d12_agx3_encode_i32_cf(
      AGX_CHIP_G17S, D12_AGX3_CF_IF, D12_AGX3_I32_CF_EQ, false, 0, false, 2,
      NULL));
   CHECK(!d12_agx3_encode_f32_cf(
      AGX_CHIP_G17S, D12_AGX3_CF_IF, D12_AGX3_F32_CF_EQ, false, 0, 2, NULL));
   CHECK(!d12_agx3_encode_cf_stack(AGX_CHIP_G17S, D12_AGX3_CF_ELSE, NULL));
   CHECK(!d12_agx3_encode_cf_branch(
      AGX_CHIP_G17S, D12_AGX3_CF_BRANCH_EXEC_NONE, 2, NULL));
   return 0;
}

int
main(void)
{
   CHECK(check_control_flow_bridge() == 0);

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

   CHECK(check_fadd_exact_witnesses() == 0);
   CHECK(check_fadd_register_domain() == 0);
   CHECK(check_fadd_canonical_bit_domain() == 0);
   CHECK(check_fadd_rejection() == 0);
   CHECK(check_fmul_exact_witnesses() == 0);
   CHECK(check_fmul_register_domain() == 0);
   CHECK(check_fmul_canonical_bit_domain() == 0);
   CHECK(check_fmul_rejection() == 0);
   CHECK(check_fmul_long_exact_witnesses() == 0);
   CHECK(check_fmul_long_register_domain() == 0);
   CHECK(check_fmul_long_canonical_bit_domain() == 0);
   CHECK(check_fmul_long_rejection() == 0);
   CHECK(check_fma_exact_witnesses() == 0);
   CHECK(check_fma_register_domain() == 0);
   CHECK(check_fma_canonical_bit_domain() == 0);
   CHECK(check_fma_rejection() == 0);
   CHECK(check_fmin_exact_witnesses() == 0);
   CHECK(check_fmin_register_domain() == 0);
   CHECK(check_fmin_canonical_bit_domain() == 0);
   CHECK(check_fmin_rejection() == 0);
   CHECK(check_fmax_exact_witnesses() == 0);
   CHECK(check_fmax_register_domain() == 0);
   CHECK(check_fmax_canonical_bit_domain() == 0);
   CHECK(check_fmax_rejection() == 0);
   CHECK(check_i32_to_f32_exact_witnesses() == 0);
   CHECK(check_i32_to_f32_register_domain() == 0);
   CHECK(check_i32_to_f32_canonical_bit_domain() == 0);
   CHECK(check_i32_to_f32_rejection() == 0);
   CHECK(check_u32_to_f32_exact_witnesses() == 0);
   CHECK(check_u32_to_f32_register_domain() == 0);
   CHECK(check_u32_to_f32_canonical_bit_domain() == 0);
   CHECK(check_u32_to_f32_rejection() == 0);
   CHECK(check_f32_rint_exact_witnesses() == 0);
   CHECK(check_f32_rint_operand_domain() == 0);
   CHECK(check_f32_rint_canonical_bit_domain() == 0);
   CHECK(check_f32_rint_rejection() == 0);
   CHECK(check_f32_rcp_exact_witnesses() == 0);
   CHECK(check_f32_rcp_operand_domain() == 0);
   CHECK(check_f32_rcp_canonical_bit_domain() == 0);
   CHECK(check_f32_rcp_rejection() == 0);
   CHECK(check_f32_rsqrt_exact_witnesses() == 0);
   CHECK(check_f32_rsqrt_operand_domain() == 0);
   CHECK(check_f32_rsqrt_canonical_bit_domain() == 0);
   CHECK(check_f32_rsqrt_rejection() == 0);
   CHECK(check_i32_compare_select_imm_5_7_exact_witnesses() == 0);
   CHECK(check_i32_compare_select_imm_5_7_operand_domain() == 0);
   CHECK(check_i32_compare_select_imm_5_7_canonical_bit_domain() == 0);
   CHECK(check_i32_compare_select_imm_5_7_rejection() == 0);
   CHECK(check_f32_to_int_rtz_exact_witnesses() == 0);
   CHECK(check_f32_to_int_rtz_operand_domain() == 0);
   CHECK(check_f32_to_int_rtz_canonical_bit_domain() == 0);
   CHECK(check_f32_to_int_rtz_rejection() == 0);

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
   CHECK(check_end_wait_exact_witnesses() == 0);
   CHECK(check_end_wait_mask_domain() == 0);
   CHECK(check_end_wait_canonical_bit_domain() == 0);
   CHECK(check_end_wait_rejection_domain() == 0);

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
