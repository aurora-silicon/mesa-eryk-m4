/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "asahi/compiler/agx3_pack.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(cond)                                                            \
   do {                                                                        \
      ++checks;                                                                \
      if (!(cond)) {                                                           \
         fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);          \
         ++failures;                                                           \
      }                                                                        \
   } while (0)

struct vector {
   uint16_t dest;
   uint32_t imm;
   const char *agx3b;
   const char *agx3c;
};

/*
 * Hard-coded, source-correlated specimens.  The immediate ladder includes the
 * complete boundary set from the mi_* probes; the destination ladder exercises
 * every recovered f2 fragment independently.
 */
static const struct vector vectors[] = {
   {0, 1,          "0c01",             "0401"},
   {0, 2,          "0c02",             "0402"},
   {0, 7,          "0c07",             "0407"},
   {0, 15,         "0c0f",             "040f"},
   {0, 31,         "0c1f",             "041f"},
   {0, 63,         "0c3f",             "043f"},
   {0, 127,        "0c7f",             "047f"},
   {0, 128,        "0c80020002000000", "0c80020002000000"},
   {0, 255,        "0cff020002000000", "0cff020002000000"},
   {0, 1000,       "0ce802000e000000", "0ce802000e000000"},
   {0, 65535,      "0cff02001e0c0700", "0cff02001e0c0700"},
   {0, 0x12345678, "0cf802121808a201", "0cf802121808a201"},
   {0, 0xffffffff, "0cff02fe1e0cff0f", "0cff02fe1e0cff0f"},
   {0, 0xfffffff9, "0cf902fe1e0cff0f", "0cf902fe1e0cff0f"},

   {2,   0x12345678, "1cf802121808a201", "1cf802121808a201"},
   {16,  0x12345678, "8cf802121808a201", "8cf802121808a201"},
   {30,  0x12345678, "fcf802121808a201", "fcf802121808a201"},
   {32,  0x12345678, "0cf842121808a201", "0cf842121808a201"},
   {64,  0x12345678, "0cf882121808a201", "0cf882121808a201"},
   {128, 0x12345678, "0cf80a121808a201", "0cf80a121808a201"},
   {254, 0x12345678, "fcf8ca121808a201", "fcf8ca121808a201"},
};

static uint8_t
nibble(char c)
{
   return c >= 'a' ? (uint8_t)(c - 'a' + 10) : (uint8_t)(c - '0');
}

static unsigned
parse_hex(const char *hex, uint8_t *out, size_t capacity)
{
   unsigned length = strlen(hex) / 2;
   CHECK(length <= capacity);

   for (unsigned i = 0; i < length; ++i)
      out[i] = (nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]);

   return length;
}

static void
test_vectors(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
         const struct vector *v = &vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_LDIMM_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_ldimm packed;

         CHECK(agx3_pack_ldimm_u32(encodings[e], v->dest, v->imm, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX;
         uint32_t imm = 0;
         uint8_t length = 0;
         CHECK(agx3_unpack_ldimm_u32(encodings[e], expected,
                                     expected_length, &dest, &imm, &length));
         CHECK(dest == v->dest);
         CHECK(imm == v->imm);
         CHECK(length == expected_length);
      }
   }
}

static void
test_boundaries_and_rejection(void)
{
   struct agx3_encoded_ldimm packed;

   /* Short-form selection is exact at both immediate and register bounds. */
   CHECK(agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3B, 30, 127, &packed));
   CHECK(packed.length == 2);
   CHECK(agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3B, 32, 127, &packed));
   CHECK(packed.length == 8);
   CHECK(agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3B, 30, 128, &packed));
   CHECK(packed.length == 8);

   CHECK(!agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX2, 0, 1, &packed));
   CHECK(!agx3_pack_ldimm_u32(AGX_ISA_ENCODING_UNKNOWN, 0, 1, &packed));
   CHECK(!agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, 1, 1, &packed));
   CHECK(!agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, 256, 1, &packed));
   CHECK(!agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, 0, 1, NULL));

   uint8_t good[8] = {0};
   CHECK(agx3_pack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, 254, 0x12345678,
                             &packed));
   memcpy(good, packed.bytes, sizeof(good));

   uint16_t dest;
   uint32_t imm;
   uint8_t length;
   CHECK(!agx3_unpack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, good, 7, &dest,
                                &imm, &length));
   CHECK(!agx3_unpack_ldimm_u32(AGX_ISA_ENCODING_AGX2, good, 8, &dest,
                                &imm, &length));

   /*
    * These bits are outside the proven form.  Bits 24/44/60 are unassigned;
    * bit 20 is unassigned in AGX3b and is an unobserved ninth f2 bit in
    * AGX3c, beyond Mesa's 256-half-register model.
    */
   const struct {
      unsigned byte;
      uint8_t mask;
   } unknown_bits[] = {{2, 0x10}, {3, 0x01}, {5, 0x10}, {7, 0x10}};

   for (unsigned i = 0;
        i < sizeof(unknown_bits) / sizeof(unknown_bits[0]); ++i) {
      uint8_t malformed[8];
      memcpy(malformed, good, sizeof(malformed));
      malformed[unknown_bits[i].byte] |= unknown_bits[i].mask;
      CHECK(!agx3_unpack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, malformed,
                                   sizeof(malformed), &dest, &imm, &length));
   }

   /* f2 bit zero is an odd half-register and therefore not the u32 form. */
   uint8_t odd_dest[8];
   memcpy(odd_dest, good, sizeof(odd_dest));
   odd_dest[2] |= 0x04;
   CHECK(!agx3_unpack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, odd_dest,
                                sizeof(odd_dest), &dest, &imm, &length));

   /* The two variants intentionally disagree on compact opcode 0x04/0x0c. */
   const uint8_t agx3b_short[] = {0x0c, 0x01};
   const uint8_t agx3c_short[] = {0x04, 0x01};
   CHECK(!agx3_unpack_ldimm_u32(AGX_ISA_ENCODING_AGX3B, agx3c_short, 2,
                                &dest, &imm, &length));
   CHECK(!agx3_unpack_ldimm_u32(AGX_ISA_ENCODING_AGX3C, agx3b_short, 2,
                                &dest, &imm, &length));
}

struct iadd_vector {
   uint16_t dest;
   uint16_t src;
   uint8_t imm;
   const char *agx3b;
   const char *agx3c;
};

/*
 * Exact Apple-emitted specimens from the hash-pinned cross-generation corpus.
 * Together they exercise every destination/source fragment and the complete
 * immediate boundary set while keeping the same canonical operand form.
 */
static const struct iadd_vector iadd_vectors[] = {
   {0, 2, 1,   "9f115400020208881104", "2705041a2500a302a0800100"},
   {0, 2, 63,  "9f115400027e08881104", "270704fa2580a302a0800100"},
   {0, 2, 64,  "9f115400028008881104", "2704041a2500a302a1800100"},
   {0, 2, 128, "9f115400020009881104", "2704041a2500a302a2800100"},
   {0, 2, 255, "9f11540002fe09881104", "270704fa2580a302a3800100"},
   {2, 0, 64,  "9f115402028000881104", "3704041a2500a30221800100"},
   {4, 10, 64, "9f115404028028881104", "2704041a2500a30aa1820100"},
   {6, 14, 64, "9f115406028038881104", "3704041a2500a30aa1830100"},
   {8, 16, 64, "9f115408028040881104", "2704041a2500a31221840100"},
};

static void
test_iadd_u32_imm8(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(iadd_vectors) / sizeof(iadd_vectors[0]); ++i) {
         const struct iadd_vector *v = &iadd_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_IADD_U32_IMM8_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_iadd_u32_imm8 packed;

         CHECK(agx3_pack_iadd_u32_imm8(encodings[e], v->dest, v->src,
                                       v->imm, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src = UINT16_MAX;
         uint8_t imm = 0, length = 0;
         CHECK(agx3_unpack_iadd_u32_imm8(
            encodings[e], expected, expected_length, &dest, &src, &imm,
            &length));
         CHECK(dest == v->dest);
         CHECK(src == v->src);
         CHECK(imm == v->imm);
         CHECK(length == expected_length);
      }

      /* Exercise every independently scattered operand value. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][2] = {{value, 0}, {0, value}};
         for (unsigned o = 0; o < 2; ++o) {
            struct agx3_encoded_iadd_u32_imm8 packed, repacked;
            uint16_t dest, src;
            uint8_t imm, length;
            CHECK(agx3_pack_iadd_u32_imm8(
               encodings[e], operands[o][0], operands[o][1], 0, &packed));
            CHECK(agx3_unpack_iadd_u32_imm8(
               encodings[e], packed.bytes, packed.length, &dest, &src, &imm,
               &length));
            CHECK(dest == operands[o][0]);
            CHECK(src == operands[o][1]);
            CHECK(imm == 0);
            CHECK(agx3_pack_iadd_u32_imm8(encodings[e], dest, src, imm,
                                          &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }

      for (unsigned value = 0; value <= UINT8_MAX; ++value) {
         struct agx3_encoded_iadd_u32_imm8 packed;
         uint16_t dest, src;
         uint8_t imm, length;
         CHECK(agx3_pack_iadd_u32_imm8(encodings[e], 0, 0, value,
                                       &packed));
         CHECK(agx3_unpack_iadd_u32_imm8(
            encodings[e], packed.bytes, packed.length, &dest, &src, &imm,
            &length));
         CHECK(dest == 0);
         CHECK(src == 0);
         CHECK(imm == value);
      }
   }
}

static void
test_iadd_u32_imm8_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_IADD_U32_IMM8_MAX_LENGTH] = {
      /* AGX3b: f2, f31 and f15 bits accepted by the u32/u8 subset. */
      {0, 0, 0, 0xfe, 0, 0xfe, 0xf9, 0x03, 0, 0, 0, 0},
      /* AGX3c: the same fields use the generation-C scatter. */
      {0x90, 0x03, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_iadd_u32_imm8 packed, repacked;
      CHECK(agx3_pack_iadd_u32_imm8(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_IADD_U32_IMM8_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src;
         uint8_t imm, length;
         const bool accepted = agx3_unpack_iadd_u32_imm8(
            encodings[e], malformed, packed.length, &dest, &src, &imm,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_iadd_u32_imm8(encodings[e], dest, src, imm,
                                          &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_iadd_u32_imm8 packed_b, packed_c;
   CHECK(agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                 &packed_b));
   CHECK(agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                 &packed_c));

   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                  &packed_b));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                  &packed_b));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                  &packed_b));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                  &packed_c));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                  &packed_b));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                  &packed_b));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                  &packed_c));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                  &packed_c));
   CHECK(!agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3C, 0, 0, 0, NULL));

   /* Invalid packs need not preserve their output, so restore valid inputs. */
   CHECK(agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                 &packed_b));
   CHECK(agx3_pack_iadd_u32_imm8(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                 &packed_c));

   uint16_t dest, src;
   uint8_t imm, length;
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, 9, &dest, &src, &imm, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 11, &dest, &src, &imm, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, 10, &dest, &src, &imm, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, 12, &dest, &src, &imm, &length));

   uint8_t padded_b[AGX3_IADD_U32_IMM8_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src, &imm,
      &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, NULL, 12, &dest, &src, &imm, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, NULL, &src, &imm, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, NULL, &imm, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, &src, NULL, &length));
   CHECK(!agx3_unpack_iadd_u32_imm8(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, &src, &imm, NULL));
}

struct iadd_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (0,0,2) row is the exact cross-target compiler witness.  Every row is
 * independently composed by Apple's table from that source-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct iadd_reg_reg_vector iadd_reg_reg_vectors[] = {
   {0, 0, 0,       "9f015600020000a81305", "2f00041a2100a30228800100"},
   {0, 0, 2,       "9f015600020008a81305", "2f00041a2100a302a8800100"},
   {0, 4, 0,       "9f015600020800a81305", "2f00043a2100a30228800100"},
   {2, 2, 0,       "9f015602020400a81305", "3f02041a2100a30228800100"},
   {190, 190, 190, "9f0156be027cf9aa1305", "bf020cfa2180a33aaaaf0100"},
   {128, 64, 32,   "9f015680028080a81305", "2f000c1a2100a30229880100"},
   {64, 128, 190,  "9f0156400200f9aa1305", "2f00841a2100a302aaaf0100"},
   {32, 16, 8,     "9f015620022020a81305", "2f00049a2100a32228820100"},
};

static void
test_iadd_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(iadd_reg_reg_vectors) /
                  sizeof(iadd_reg_reg_vectors[0]);
           ++i) {
         const struct iadd_reg_reg_vector *v = &iadd_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_IADD_U32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_iadd_u32_reg_reg packed;

         CHECK(agx3_pack_iadd_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_iadd_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      /* Exercise every value in each independently scattered register. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_iadd_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_iadd_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_iadd_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_iadd_u32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_iadd_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_IADD_U32_REG_REG_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0},
      /* AGX3c: the bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_iadd_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_iadd_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_IADD_U32_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_iadd_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_iadd_u32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_iadd_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                     &packed_c));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                     &packed_c));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                     &packed_b));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 192,
                                     &packed_b));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                     &packed_c));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                     &packed_c));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                     &packed_c));
   CHECK(!agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                     NULL));

   /* Invalid packs need not preserve their output, so restore valid inputs. */
   CHECK(agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_iadd_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, 9, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 11, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, 10, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, 12, &dest, &src0, &src1,
      &length));

   uint8_t padded_b[AGX3_IADD_U32_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, 12, &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, NULL, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, NULL, &src1,
      &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, &src0, NULL,
      &length));
   CHECK(!agx3_unpack_iadd_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, &src0, &src1,
      NULL));
}

struct fadd_f32_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *expected;
};

/*
 * (4,2,0) and the two high-register rows are exact compact compiler specimens.
 * (16,10,2) is the independently correlated operand tuple from the extended
 * nine-target replay, composed here with the compact selection.  The other
 * rows were independently composed by both Apple tables from that selection.
 */
static const struct fadd_f32_reg_reg_vector fadd_f32_reg_reg_vectors[] = {
   {0,   0,   0,   "090104010020"},
   {4,   2,   0,   "290104030020"},
   {16,  10,  2,   "8903040b0020"},
   {132, 130, 128, "290104030035"},
   {128, 126, 124, "097d047f0030"},
   {254, 254, 254, "f97fc47f0035"},
   {32,  16,  8,   "090944110020"},
   {64,  128, 254, "097f84010025"},
};

static void
test_fadd_f32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(fadd_f32_reg_reg_vectors) /
                  sizeof(fadd_f32_reg_reg_vectors[0]);
           ++i) {
         const struct fadd_f32_reg_reg_vector *v =
            &fadd_f32_reg_reg_vectors[i];
         uint8_t expected[AGX3_FADD_F32_REG_REG_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(v->expected, expected, sizeof(expected));
         struct agx3_encoded_fadd_f32_reg_reg packed;

         CHECK(agx3_pack_fadd_f32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_fadd_f32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      /* Exhaust every modeled value in each independently correlated field. */
      for (uint16_t value = 0; value < 256; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };

         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_fadd_f32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_fadd_f32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_fadd_f32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_fadd_f32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_fadd_f32_reg_reg_rejection(void)
{
   static const uint8_t operand_mask[AGX3_FADD_F32_REG_REG_LENGTH] = {
      0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x15,
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   /* Exercise every instruction bit from both zero and one operand states. */
   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t operand_states[][3] = {
         {0, 0, 0}, {254, 254, 254},
      };

      for (unsigned state = 0; state < 2; ++state) {
         struct agx3_encoded_fadd_f32_reg_reg packed, repacked;
         CHECK(agx3_pack_fadd_f32_reg_reg(
            encodings[e], operand_states[state][0], operand_states[state][1],
            operand_states[state][2], &packed));

         for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
            uint8_t malformed[AGX3_FADD_F32_REG_REG_LENGTH];
            memcpy(malformed, packed.bytes, sizeof(malformed));
            malformed[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest, src0, src1;
            uint8_t length;
            const bool accepted = agx3_unpack_fadd_f32_reg_reg(
               encodings[e], malformed, sizeof(malformed), &dest, &src0,
               &src1, &length);
            const bool is_operand =
               (operand_mask[bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               CHECK(agx3_pack_fadd_f32_reg_reg(
                  encodings[e], dest, src0, src1, &repacked));
               CHECK(repacked.length == length);
               CHECK(memcmp(repacked.bytes, malformed, length) == 0);
            }
         }
      }
   }

   struct agx3_encoded_fadd_f32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));
   CHECK(memcmp(packed_b.bytes, packed_c.bytes, packed_b.length) == 0);

   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                     &packed_c));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                     &packed_c));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 255, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 255, 0,
                                     &packed_c));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 255,
                                     &packed_c));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 256, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                     &packed_c));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                     &packed_c));
   CHECK(!agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                     NULL));

   CHECK(agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_fadd_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   for (size_t available = 0; available < packed_b.length; ++available) {
      CHECK(!agx3_unpack_fadd_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &dest, &src0,
         &src1, &length));
      CHECK(!agx3_unpack_fadd_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &dest, &src0,
         &src1, &length));
   }

   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_UNKNOWN, packed_c.bytes, packed_c.length, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));

   /* A canonical prefix is consumed without claiming following instructions. */
   uint8_t appended[AGX3_FADD_F32_REG_REG_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   CHECK(agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &dest, &src0,
      &src1, &length));
   CHECK(length == AGX3_FADD_F32_REG_REG_LENGTH);

   /* The compact bytes are proven common, so both table generations accept
    * the other's canonical prefix. */
   CHECK(agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));

   /* Observed alternative selections, extended forms, and f32mul must not be
    * mistaken for this exact compact f32add contract. */
   static const uint8_t alternatives[][12] = {
      {0x89, 0x03, 0x04, 0x0b, 0x00, 0xc0},
      {0xa9, 0x02, 0x04, 0x0a, 0x22, 0x20, 0xa0, 0x02,
       0x04, 0x08, 0x12, 0x00},
      {0x19, 0x03, 0x0c, 0x01, 0x00, 0x20},
      {0x09, 0x05, 0x1d, 0x01, 0x00, 0xc0},
   };
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, alternatives[0], sizeof(alternatives[0]),
      &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, alternatives[1], sizeof(alternatives[1]),
      &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, alternatives[2], sizeof(alternatives[2]),
      &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_fadd_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, alternatives[3], sizeof(alternatives[3]),
      &dest, &src0, &src1, &length));
}

struct fmul_f32_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * (0,0,2) is the exact AGX3b L-ladder and G17S Q-ladder witness; (0,0,4) is
 * the exact P2f_mul AGX3b witness.  The remaining rows exercise every exposed
 * operand fragment and were independently accepted by both pinned Apple
 * encoding tables.
 */
static const struct fmul_f32_reg_reg_vector fmul_f32_reg_reg_vectors[] = {
   {0,   0,   0,   "09011d0100c0", "09011d010020"},
   {0,   0,   2,   "09031d0100c0", "09031d010020"},
   {0,   0,   4,   "09051d0100c0", "09051d010020"},
   {2,   0,   2,   "19031d0100c0", "19031d010020"},
   {16,  10,  2,   "89031d0b00c0", "89031d0b0020"},
   {126, 126, 126, "f97fdd7f00c0", "f97fdd7f0020"},
};

static void
test_fmul_f32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(fmul_f32_reg_reg_vectors) /
                  sizeof(fmul_f32_reg_reg_vectors[0]);
           ++i) {
         const struct fmul_f32_reg_reg_vector *v =
            &fmul_f32_reg_reg_vectors[i];
         const char *expected_text = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_FMUL_F32_REG_REG_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(expected_text, expected, sizeof(expected));
         struct agx3_encoded_fmul_f32_reg_reg packed, repacked;

         CHECK(agx3_pack_fmul_f32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_fmul_f32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
         CHECK(agx3_pack_fmul_f32_reg_reg(encodings[e], dest, src0, src1,
                                          &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, expected, length) == 0);
      }

      /* Exhaust every conservatively exposed register value in every field. */
      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };

         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_fmul_f32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_fmul_f32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_fmul_f32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_fmul_f32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_fmul_f32_reg_reg_rejection(void)
{
   static const uint8_t operand_mask[AGX3_FMUL_F32_REG_REG_LENGTH] = {
      0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x00,
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };
   const uint16_t operand_states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   /* Exercise every instruction bit from both zero and one operand states. */
   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned state = 0; state < 2; ++state) {
         struct agx3_encoded_fmul_f32_reg_reg packed, repacked;
         CHECK(agx3_pack_fmul_f32_reg_reg(
            encodings[e], operand_states[state][0], operand_states[state][1],
            operand_states[state][2], &packed));

         for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
            uint8_t malformed[AGX3_FMUL_F32_REG_REG_LENGTH];
            memcpy(malformed, packed.bytes, sizeof(malformed));
            malformed[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest, src0, src1;
            uint8_t length;
            const bool accepted = agx3_unpack_fmul_f32_reg_reg(
               encodings[e], malformed, sizeof(malformed), &dest, &src0,
               &src1, &length);
            const bool is_operand =
               (operand_mask[bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               CHECK(agx3_pack_fmul_f32_reg_reg(
                  encodings[e], dest, src0, src1, &repacked));
               CHECK(repacked.length == length);
               CHECK(memcmp(repacked.bytes, malformed, length) == 0);
            }
         }
      }
   }

   struct agx3_encoded_fmul_f32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_fmul_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 2,
                                    &packed_b));
   CHECK(agx3_pack_fmul_f32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 2,
                                    &packed_c));
   CHECK(memcmp(packed_b.bytes, packed_c.bytes, packed_b.length) != 0);

   CHECK(!agx3_pack_fmul_f32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fmul_f32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_fmul_f32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                     NULL));
   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned value = 0;
        value < sizeof(invalid_values) / sizeof(invalid_values[0]); ++value) {
      for (unsigned operand = 0; operand < 3; ++operand) {
         uint16_t args[3] = {0};
         args[operand] = invalid_values[value];
         CHECK(!agx3_pack_fmul_f32_reg_reg(
            AGX_ISA_ENCODING_AGX3B, args[0], args[1], args[2], &packed_b));
         CHECK(!agx3_pack_fmul_f32_reg_reg(
            AGX_ISA_ENCODING_AGX3C, args[0], args[1], args[2], &packed_c));
      }
   }

   uint16_t dest, src0, src1;
   uint8_t length;
   for (size_t available = 0; available < packed_b.length; ++available) {
      CHECK(!agx3_unpack_fmul_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &dest, &src0,
         &src1, &length));
      CHECK(!agx3_unpack_fmul_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &dest, &src0,
         &src1, &length));
   }

   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_UNKNOWN, packed_c.bytes, packed_c.length, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));

   uint8_t appended[AGX3_FMUL_F32_REG_REG_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   CHECK(agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &dest, &src0,
      &src1, &length));
   CHECK(length == AGX3_FMUL_F32_REG_REG_LENGTH);

   /* Variant trailers, alternate selections, long forms, and f32add fail. */
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   static const uint8_t unsupported[][14] = {
      {0x19, 0x03, 0x0d, 0x01, 0x00, 0xc0},
      {0x19, 0x03, 0x0d, 0x01, 0x00, 0x20},
      {0x29, 0x00, 0x05, 0x30, 0x22, 0x80, 0xa0,
       0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10},
      {0x09, 0x01, 0x04, 0x01, 0x00, 0x20},
   };
   const size_t unsupported_lengths[] = {6, 6, 14, 6};
   for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]);
        ++i) {
      CHECK(!agx3_unpack_fmul_f32_reg_reg(
         i == 0 ? AGX_ISA_ENCODING_AGX3B : AGX_ISA_ENCODING_AGX3C,
         unsupported[i], unsupported_lengths[i], &dest, &src0, &src1,
         &length));
   }
}

struct fmul_f32_reg_reg_long_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *expected;
};

/*
 * (0,0,2) is the exact final G17G/G17S L-ladder witness and (0,0,4) is
 * G17S's exact operand-correlated P2f_mul witness.  The remaining rows were
 * independently composed and decoded by the pinned AGX3c table from that
 * exact anonymous/context selection.
 */
static const struct fmul_f32_reg_reg_long_vector
   fmul_f32_reg_reg_long_vectors[] = {
      {0,   0,   0,   "290005102280a0021c0020000010"},
      {0,   0,   2,   "290205102280a0021c0020000010"},
      {0,   0,   4,   "290005302280a0021c0020000010"},
      {2,   0,   2,   "390205102280a0021c0020000010"},
      {16,  10,  2,   "a90205102280a0029c0220000010"},
      {126, 126, 126, "b90285f02280a03a9d1f20000210"},
   };

static void
test_fmul_f32_reg_reg_long(void)
{
   for (unsigned i = 0;
        i < sizeof(fmul_f32_reg_reg_long_vectors) /
               sizeof(fmul_f32_reg_reg_long_vectors[0]);
        ++i) {
      const struct fmul_f32_reg_reg_long_vector *v =
         &fmul_f32_reg_reg_long_vectors[i];
      uint8_t expected[AGX3_FMUL_F32_REG_REG_LONG_LENGTH] = {0};
      const unsigned expected_length =
         parse_hex(v->expected, expected, sizeof(expected));
      struct agx3_encoded_fmul_f32_reg_reg_long packed, repacked;

      CHECK(agx3_pack_fmul_f32_reg_reg_long(
         AGX_ISA_ENCODING_AGX3C, v->dest, v->src0, v->src1, &packed));
      CHECK(packed.length == expected_length);
      CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

      uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
      uint8_t length = 0;
      CHECK(agx3_unpack_fmul_f32_reg_reg_long(
         AGX_ISA_ENCODING_AGX3C, expected, expected_length, &dest, &src0,
         &src1, &length));
      CHECK(dest == v->dest);
      CHECK(src0 == v->src0);
      CHECK(src1 == v->src1);
      CHECK(length == expected_length);
      CHECK(agx3_pack_fmul_f32_reg_reg_long(
         AGX_ISA_ENCODING_AGX3C, dest, src0, src1, &repacked));
      CHECK(repacked.length == length);
      CHECK(memcmp(repacked.bytes, expected, length) == 0);
   }

   for (uint16_t value = 0; value < 128; value += 2) {
      const uint16_t operands[][3] = {
         {value, 0, 0}, {0, value, 0}, {0, 0, value},
      };

      for (unsigned operand = 0; operand < 3; ++operand) {
         struct agx3_encoded_fmul_f32_reg_reg_long packed, repacked;
         uint16_t dest, src0, src1;
         uint8_t length;
         CHECK(agx3_pack_fmul_f32_reg_reg_long(
            AGX_ISA_ENCODING_AGX3C, operands[operand][0],
            operands[operand][1], operands[operand][2], &packed));
         CHECK(agx3_unpack_fmul_f32_reg_reg_long(
            AGX_ISA_ENCODING_AGX3C, packed.bytes, packed.length, &dest,
            &src0, &src1, &length));
         CHECK(dest == operands[operand][0]);
         CHECK(src0 == operands[operand][1]);
         CHECK(src1 == operands[operand][2]);
         CHECK(agx3_pack_fmul_f32_reg_reg_long(
            AGX_ISA_ENCODING_AGX3C, dest, src0, src1, &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
      }
   }
}

static void
test_fmul_f32_reg_reg_long_rejection(void)
{
   static const uint8_t operand_mask[AGX3_FMUL_F32_REG_REG_LONG_LENGTH] = {
      0x90, 0x02, 0x80, 0xe0, 0x00, 0x00, 0x00,
      0x38, 0x81, 0x1f, 0x00, 0x00, 0x02, 0x00,
   };
   const uint16_t operand_states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned state = 0; state < 2; ++state) {
      struct agx3_encoded_fmul_f32_reg_reg_long packed, repacked;
      CHECK(agx3_pack_fmul_f32_reg_reg_long(
         AGX_ISA_ENCODING_AGX3C, operand_states[state][0],
         operand_states[state][1], operand_states[state][2], &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_FMUL_F32_REG_REG_LONG_LENGTH];
         memcpy(malformed, packed.bytes, sizeof(malformed));
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_fmul_f32_reg_reg_long(
            AGX_ISA_ENCODING_AGX3C, malformed, sizeof(malformed), &dest,
            &src0, &src1, &length);
         const bool is_operand =
            (operand_mask[bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_fmul_f32_reg_reg_long(
               AGX_ISA_ENCODING_AGX3C, dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_fmul_f32_reg_reg_long packed;
   CHECK(agx3_pack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, 0, 0, 2, &packed));
   CHECK(!agx3_pack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX2, 0, 0, 2, &packed));
   CHECK(!agx3_pack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3B, 0, 0, 2, &packed));
   CHECK(!agx3_pack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_UNKNOWN, 0, 0, 2, &packed));
   CHECK(!agx3_pack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, 0, 0, 2, NULL));

   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned value = 0;
        value < sizeof(invalid_values) / sizeof(invalid_values[0]); ++value) {
      for (unsigned operand = 0; operand < 3; ++operand) {
         uint16_t args[3] = {0};
         args[operand] = invalid_values[value];
         CHECK(!agx3_pack_fmul_f32_reg_reg_long(
            AGX_ISA_ENCODING_AGX3C, args[0], args[1], args[2], &packed));
      }
   }

   uint16_t dest, src0, src1;
   uint8_t length;
   for (size_t available = 0; available < packed.length; ++available) {
      CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
         AGX_ISA_ENCODING_AGX3C, packed.bytes, available, &dest, &src0,
         &src1, &length));
   }
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX2, packed.bytes, packed.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3B, packed.bytes, packed.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_UNKNOWN, packed.bytes, packed.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, NULL, packed.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, packed.bytes, packed.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, packed.bytes, packed.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, packed.bytes, packed.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, packed.bytes, packed.length, &dest, &src0,
      &src1, NULL));

   uint8_t appended[AGX3_FMUL_F32_REG_REG_LONG_LENGTH + 1];
   memcpy(appended, packed.bytes, packed.length);
   appended[packed.length] = 0xff;
   CHECK(agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &dest, &src0,
      &src1, &length));
   CHECK(length == AGX3_FMUL_F32_REG_REG_LONG_LENGTH);

   static const uint8_t alternate_context[] = {
      0x39, 0x02, 0x05, 0x10, 0x22, 0x80, 0xa0,
      0x02, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x10,
   };
   static const uint8_t compact[] = {
      0x09, 0x03, 0x1d, 0x01, 0x00, 0x20,
   };
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, alternate_context, sizeof(alternate_context),
      &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_fmul_f32_reg_reg_long(
      AGX_ISA_ENCODING_AGX3C, compact, sizeof(compact), &dest, &src0, &src1,
      &length));
}

struct fma_f32_reg_reg_reg_vector {
   uint16_t dest;
   uint16_t mul0;
   uint16_t mul1;
   uint16_t addend;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The L final operation and P3f_fma/P3f_mad rows are exact compiler bytes.
 * The remaining rows were independently composed and decoded by both pinned
 * Apple tables from that exact anonymous/context selection.
 */
static const struct fma_f32_reg_reg_reg_vector
   fma_f32_reg_reg_reg_vectors[] = {
      {0,   0,   0,   0,   "09011e01810002c0",
                              "2900070a2020a3023800a00000080080"},
      {0,   0,   2,   0,   "09031e01810002c0",
                              "2902070a2020a3023800a00000080080"},
      {0,   4,   0,   8,   "09011e05810802c0",
                              "2900070a2020a3023801a00008080080"},
      {0,   0,   4,   8,   "09051e01810802c0",
                              "2900071a2020a3023800a00008080080"},
      {2,   0,   2,   0,   "19031e01810002c0",
                              "3902070a2020a3023800a00000080080"},
      {16,  10,  2,   12,  "89031e0b810c02c0",
                              "a902070a2020a302b802a0000c080080"},
      {126, 126, 126, 126, "f97fde7f817e02c0",
                              "b90287fa2020a33ab91fa1003e080080"},
   };

static void
test_fma_f32_reg_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(fma_f32_reg_reg_reg_vectors) /
                  sizeof(fma_f32_reg_reg_reg_vectors[0]);
           ++i) {
         const struct fma_f32_reg_reg_reg_vector *v =
            &fma_f32_reg_reg_reg_vectors[i];
         const char *expected_text = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(expected_text, expected, sizeof(expected));
         struct agx3_encoded_fma_f32_reg_reg_reg packed, repacked;

         CHECK(agx3_pack_fma_f32_reg_reg_reg(
            encodings[e], v->dest, v->mul0, v->mul1, v->addend, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
         uint16_t addend = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_fma_f32_reg_reg_reg(
            encodings[e], expected, expected_length, &dest, &mul0, &mul1,
            &addend, &length));
         CHECK(dest == v->dest);
         CHECK(mul0 == v->mul0);
         CHECK(mul1 == v->mul1);
         CHECK(addend == v->addend);
         CHECK(length == expected_length);
         CHECK(agx3_pack_fma_f32_reg_reg_reg(
            encodings[e], dest, mul0, mul1, addend, &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, expected, length) == 0);
      }

      /* Exhaust every proved register independently in all four fields. */
      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][4] = {
            {value, 0, 0, 0}, {0, value, 0, 0},
            {0, 0, value, 0}, {0, 0, 0, value},
         };

         for (unsigned operand = 0; operand < 4; ++operand) {
            struct agx3_encoded_fma_f32_reg_reg_reg packed, repacked;
            uint16_t dest, mul0, mul1, addend;
            uint8_t length;
            CHECK(agx3_pack_fma_f32_reg_reg_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               operands[operand][2], operands[operand][3], &packed));
            CHECK(agx3_unpack_fma_f32_reg_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &mul0,
               &mul1, &addend, &length));
            CHECK(dest == operands[operand][0]);
            CHECK(mul0 == operands[operand][1]);
            CHECK(mul1 == operands[operand][2]);
            CHECK(addend == operands[operand][3]);
            CHECK(agx3_pack_fma_f32_reg_reg_reg(
               encodings[e], dest, mul0, mul1, addend, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_fma_f32_reg_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x7e, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xf0, 0x00, 0x00, 0x00, 0x38,
       0x81, 0x1f, 0x01, 0x00, 0x3e, 0x00, 0x00, 0x00},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };
   const uint16_t states[][4] = {
      {0, 0, 0, 0}, {126, 126, 126, 126},
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned state = 0; state < 2; ++state) {
         struct agx3_encoded_fma_f32_reg_reg_reg packed, repacked;
         CHECK(agx3_pack_fma_f32_reg_reg_reg(
            encodings[e], states[state][0], states[state][1],
            states[state][2], states[state][3], &packed));

         for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
            uint8_t malformed[AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH] = {0};
            memcpy(malformed, packed.bytes, packed.length);
            malformed[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest, mul0, mul1, addend;
            uint8_t length;
            const bool accepted = agx3_unpack_fma_f32_reg_reg_reg(
               encodings[e], malformed, packed.length, &dest, &mul0, &mul1,
               &addend, &length);
            const bool is_operand =
               (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);

            if (accepted) {
               CHECK(agx3_pack_fma_f32_reg_reg_reg(
                  encodings[e], dest, mul0, mul1, addend, &repacked));
               CHECK(repacked.length == length);
               CHECK(memcmp(repacked.bytes, malformed, length) == 0);
            }
         }
      }
   }

   struct agx3_encoded_fma_f32_reg_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 0, 2, 0, &packed_b));
   CHECK(agx3_pack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, 0, 0, 2, 0, &packed_c));
   CHECK(!agx3_pack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX2, 0, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_UNKNOWN, 0, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fma_f32_reg_reg_reg(
      (enum agx_isa_encoding)99, 0, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 0, 2, 0, NULL));

   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned value = 0;
        value < sizeof(invalid_values) / sizeof(invalid_values[0]); ++value) {
      for (unsigned operand = 0; operand < 4; ++operand) {
         uint16_t args[4] = {0};
         args[operand] = invalid_values[value];
         CHECK(!agx3_pack_fma_f32_reg_reg_reg(
            AGX_ISA_ENCODING_AGX3B, args[0], args[1], args[2], args[3],
            &packed_b));
         CHECK(!agx3_pack_fma_f32_reg_reg_reg(
            AGX_ISA_ENCODING_AGX3C, args[0], args[1], args[2], args[3],
            &packed_c));
      }
   }

   uint16_t dest, mul0, mul1, addend;
   uint8_t length;
   for (size_t available = 0; available < packed_b.length; ++available) {
      CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &dest, &mul0,
         &mul1, &addend, &length));
   }
   for (size_t available = 0; available < packed_c.length; ++available) {
      CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &dest, &mul0,
         &mul1, &addend, &length));
   }

   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &mul0,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &mul0,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &mul0,
      NULL, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &mul0,
      &mul1, NULL, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &mul0,
      &mul1, &addend, NULL));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &mul0, &mul1,
      &addend, &length));

   uint8_t appended[AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   CHECK(agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &dest, &mul0,
      &mul1, &addend, &length));
   CHECK(length == packed_c.length);

   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &mul0,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &mul0,
      &mul1, &addend, &length));

   /* Alternate anonymous/context selections and multiply-only forms fail. */
   static const uint8_t alternate_b[] = {
      0x19, 0x03, 0x0e, 0x01, 0x01, 0x00, 0x02, 0xc0,
   };
   static const uint8_t alternate_c[] = {
      0x39, 0x02, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
      0x38, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80,
   };
   static const uint8_t fmul_c[] = {
      0x29, 0x02, 0x05, 0x10, 0x22, 0x80, 0xa0,
      0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10,
   };
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3B, alternate_b, sizeof(alternate_b), &dest,
      &mul0, &mul1, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, alternate_c, sizeof(alternate_c), &dest,
      &mul0, &mul1, &addend, &length));
   CHECK(!agx3_unpack_fma_f32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, fmul_c, sizeof(fmul_c), &dest, &mul0, &mul1,
      &addend, &length));
}

struct fmin_f32_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/* L final and P2 rows are exact compiler bytes; the remaining vectors were
 * composed and decoded independently by both pinned Apple tables. */
static const struct fmin_f32_reg_reg_vector fmin_f32_reg_reg_vectors[] = {
   {0,   0,   0,   "02011e0101c0", "2200470a2300a0021400a1600040"},
   {0,   2,   0,   "02031e0101c0", "2202470a2300a0021400a1600240"},
   {2,   2,   4,   "12031e0501c0", "3202470a2300a0021401a1602240"},
   {16,  10,  2,   "820b1e0301c0", "a202474b2300a0029400a1600a40"},
   {126, 126, 126, "f27fde7f01c0", "b202c7eb2380a03ab71fa1607e4d"},
};

static void
test_fmin_f32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B, AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(fmin_f32_reg_reg_vectors) /
                  sizeof(fmin_f32_reg_reg_vectors[0]);
           ++i) {
         const struct fmin_f32_reg_reg_vector *v =
            &fmin_f32_reg_reg_vectors[i];
         const char *expected_text = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_FMIN_F32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(expected_text, expected, sizeof(expected));
         struct agx3_encoded_fmin_f32_reg_reg packed, repacked;

         CHECK(agx3_pack_fmin_f32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_fmin_f32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
         CHECK(agx3_pack_fmin_f32_reg_reg(
            encodings[e], dest, src0, src1, &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, expected, length) == 0);
      }

      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fmin_f32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_fmin_f32_reg_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               operands[operand][2], &packed));
            CHECK(agx3_unpack_fmin_f32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
            CHECK(agx3_pack_fmin_f32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_fmin_f32_reg_reg_rejection(void)
{
   static const uint8_t masks[][AGX3_FMIN_F32_REG_REG_MAX_LENGTH] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x00},
      /* Each AGX3c source bit is deposited twice.  A one-bit mutation makes
       * the comparison/select copies incoherent and must be rejected. */
      {0x90, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
       0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B, AGX_ISA_ENCODING_AGX3C,
   };
   const uint16_t states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned state = 0; state < 2; ++state) {
         struct agx3_encoded_fmin_f32_reg_reg packed, repacked;
         CHECK(agx3_pack_fmin_f32_reg_reg(
            encodings[e], states[state][0], states[state][1],
            states[state][2], &packed));

         for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
            uint8_t malformed[AGX3_FMIN_F32_REG_REG_MAX_LENGTH] = {0};
            memcpy(malformed, packed.bytes, packed.length);
            malformed[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest, src0, src1;
            uint8_t length;
            const bool accepted = agx3_unpack_fmin_f32_reg_reg(
               encodings[e], malformed, packed.length, &dest, &src0, &src1,
               &length);
            const bool is_operand =
               (masks[e][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               CHECK(agx3_pack_fmin_f32_reg_reg(
                  encodings[e], dest, src0, src1, &repacked));
               CHECK(repacked.length == length);
               CHECK(memcmp(repacked.bytes, malformed, length) == 0);
            }
         }
      }
   }

   struct agx3_encoded_fmin_f32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 2, 0, &packed_b));
   CHECK(agx3_pack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, 0, 2, 0, &packed_c));
   CHECK(!agx3_pack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX2, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_UNKNOWN, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fmin_f32_reg_reg(
      (enum agx_isa_encoding)99, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 2, 0, NULL));

   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned value = 0;
        value < sizeof(invalid_values) / sizeof(invalid_values[0]); ++value) {
      for (unsigned operand = 0; operand < 3; ++operand) {
         uint16_t args[3] = {0};
         args[operand] = invalid_values[value];
         CHECK(!agx3_pack_fmin_f32_reg_reg(
            AGX_ISA_ENCODING_AGX3B, args[0], args[1], args[2], &packed_b));
         CHECK(!agx3_pack_fmin_f32_reg_reg(
            AGX_ISA_ENCODING_AGX3C, args[0], args[1], args[2], &packed_c));
      }
   }

   uint16_t dest, src0, src1;
   uint8_t length;
   for (size_t available = 0; available < packed_b.length; ++available) {
      CHECK(!agx3_unpack_fmin_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &dest, &src0,
         &src1, &length));
   }
   for (size_t available = 0; available < packed_c.length; ++available) {
      CHECK(!agx3_unpack_fmin_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &dest, &src0,
         &src1, &length));
   }

   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));

   uint8_t appended[AGX3_FMIN_F32_REG_REG_MAX_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   CHECK(agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &dest, &src0,
      &src1, &length));
   CHECK(length == packed_c.length);

   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));

   static const uint8_t alternate_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x01, 0x20,
   };
   static const uint8_t fmax_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x00, 0xc0,
   };
   static const uint8_t fmax_c[] = {
      0x22, 0x02, 0x07, 0x0a, 0x23, 0x00, 0xa0,
      0x02, 0x14, 0x00, 0xa1, 0x64, 0x02, 0x40,
   };
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, alternate_b, sizeof(alternate_b), &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, fmax_b, sizeof(fmax_b), &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fmin_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, fmax_c, sizeof(fmax_c), &dest, &src0, &src1,
      &length));
}

struct fmax_f32_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/* L final and P2 rows are exact compiler bytes; the remaining vectors were
 * composed and decoded independently by both pinned Apple tables. */
static const struct fmax_f32_reg_reg_vector fmax_f32_reg_reg_vectors[] = {
   {0,   0,   0,   "02011e0100c0", "2200070a2300a0021400a1640040"},
   {0,   2,   0,   "02031e0100c0", "2202070a2300a0021400a1640240"},
   {2,   2,   4,   "12031e0500c0", "3202070a2300a0021401a1642240"},
   {16,  10,  2,   "820b1e0300c0", "a202074b2300a0029400a1640a40"},
   {126, 126, 126, "f27fde7f00c0", "b20287eb2380a03ab71fa1647e4d"},
};

static void
test_fmax_f32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B, AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(fmax_f32_reg_reg_vectors) /
                  sizeof(fmax_f32_reg_reg_vectors[0]);
           ++i) {
         const struct fmax_f32_reg_reg_vector *v =
            &fmax_f32_reg_reg_vectors[i];
         const char *expected_text = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_FMAX_F32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(expected_text, expected, sizeof(expected));
         struct agx3_encoded_fmax_f32_reg_reg packed, repacked;

         CHECK(agx3_pack_fmax_f32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_fmax_f32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
         CHECK(agx3_pack_fmax_f32_reg_reg(
            encodings[e], dest, src0, src1, &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, expected, length) == 0);
      }

      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned operand = 0; operand < 3; ++operand) {
            struct agx3_encoded_fmax_f32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_fmax_f32_reg_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               operands[operand][2], &packed));
            CHECK(agx3_unpack_fmax_f32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[operand][0]);
            CHECK(src0 == operands[operand][1]);
            CHECK(src1 == operands[operand][2]);
            CHECK(agx3_pack_fmax_f32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_fmax_f32_reg_reg_rejection(void)
{
   static const uint8_t masks[][AGX3_FMAX_F32_REG_REG_MAX_LENGTH] = {
      {0xf0, 0x7e, 0xc0, 0x7e, 0x00, 0x00},
      /* Each AGX3c source bit is deposited twice.  A one-bit mutation makes
       * the comparison/select copies incoherent and must be rejected. */
      {0x90, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
       0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B, AGX_ISA_ENCODING_AGX3C,
   };
   const uint16_t states[][3] = {
      {0, 0, 0}, {126, 126, 126},
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned state = 0; state < 2; ++state) {
         struct agx3_encoded_fmax_f32_reg_reg packed, repacked;
         CHECK(agx3_pack_fmax_f32_reg_reg(
            encodings[e], states[state][0], states[state][1],
            states[state][2], &packed));

         for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
            uint8_t malformed[AGX3_FMAX_F32_REG_REG_MAX_LENGTH] = {0};
            memcpy(malformed, packed.bytes, packed.length);
            malformed[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest, src0, src1;
            uint8_t length;
            const bool accepted = agx3_unpack_fmax_f32_reg_reg(
               encodings[e], malformed, packed.length, &dest, &src0, &src1,
               &length);
            const bool is_operand =
               (masks[e][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               CHECK(agx3_pack_fmax_f32_reg_reg(
                  encodings[e], dest, src0, src1, &repacked));
               CHECK(repacked.length == length);
               CHECK(memcmp(repacked.bytes, malformed, length) == 0);
            }
         }
      }
   }

   struct agx3_encoded_fmax_f32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 2, 0, &packed_b));
   CHECK(agx3_pack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, 0, 2, 0, &packed_c));
   CHECK(!agx3_pack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX2, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_UNKNOWN, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fmax_f32_reg_reg(
      (enum agx_isa_encoding)99, 0, 2, 0, &packed_b));
   CHECK(!agx3_pack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 2, 0, NULL));

   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned value = 0;
        value < sizeof(invalid_values) / sizeof(invalid_values[0]); ++value) {
      for (unsigned operand = 0; operand < 3; ++operand) {
         uint16_t args[3] = {0};
         args[operand] = invalid_values[value];
         CHECK(!agx3_pack_fmax_f32_reg_reg(
            AGX_ISA_ENCODING_AGX3B, args[0], args[1], args[2], &packed_b));
         CHECK(!agx3_pack_fmax_f32_reg_reg(
            AGX_ISA_ENCODING_AGX3C, args[0], args[1], args[2], &packed_c));
      }
   }

   uint16_t dest, src0, src1;
   uint8_t length;
   for (size_t available = 0; available < packed_b.length; ++available) {
      CHECK(!agx3_unpack_fmax_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &dest, &src0,
         &src1, &length));
   }
   for (size_t available = 0; available < packed_c.length; ++available) {
      CHECK(!agx3_unpack_fmax_f32_reg_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &dest, &src0,
         &src1, &length));
   }

   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));

   uint8_t appended[AGX3_FMAX_F32_REG_REG_MAX_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   CHECK(agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &dest, &src0,
      &src1, &length));
   CHECK(length == packed_c.length);

   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));

   static const uint8_t alternate_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x00, 0x20,
   };
   static const uint8_t fmin_b[] = {
      0x02, 0x03, 0x1e, 0x01, 0x01, 0xc0,
   };
   static const uint8_t fmin_c[] = {
      0x22, 0x02, 0x47, 0x0a, 0x23, 0x00, 0xa0,
      0x02, 0x14, 0x00, 0xa1, 0x60, 0x02, 0x40,
   };
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, alternate_b, sizeof(alternate_b), &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, fmin_b, sizeof(fmin_b), &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_fmax_f32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, fmin_c, sizeof(fmin_c), &dest, &src0, &src1,
      &length));
}

struct i32_to_f32_reg_vector {
   uint16_t dest;
   uint16_t src;
   const char *agx3b;
   const char *agx3c;
};

/* The zero row is the exact nine-target L/PC compiler witness.  Remaining
 * rows are strict Apple-table compositions of the same encoding point and
 * exercise every exposed fragment without admitting the unprobed high bit. */
static const struct i32_to_f32_reg_vector i32_to_f32_reg_vectors[] = {
   {0,   0,   "a70756000200ac60", "2f0000022280af026800"},
   {2,   0,   "a70756020200ac60", "3f0000022280af026800"},
   {0,   2,   "a70756000204ac60", "2f0200022280af026800"},
   {4,   10,  "a70756040214ac60", "2f0200222280af0a6800"},
   {12,  4,   "a707560c0208ac60", "2f0000122280af1a6800"},
   {126, 126, "a707567e02fcac60", "bf0280f22280af3a6900"},
};

static void
test_i32_to_f32_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B, AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(i32_to_f32_reg_vectors) /
                  sizeof(i32_to_f32_reg_vectors[0]);
           ++i) {
         const struct i32_to_f32_reg_vector *v = &i32_to_f32_reg_vectors[i];
         const char *expected_text = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_I32_TO_F32_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(expected_text, expected, sizeof(expected));
         struct agx3_encoded_i32_to_f32_reg packed, repacked;

         CHECK(agx3_pack_i32_to_f32_reg(
            encodings[e], v->dest, v->src, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_i32_to_f32_reg(
            encodings[e], expected, expected_length, &dest, &src, &length));
         CHECK(dest == v->dest);
         CHECK(src == v->src);
         CHECK(length == expected_length);
         CHECK(agx3_pack_i32_to_f32_reg(
            encodings[e], dest, src, &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, expected, length) == 0);
      }

      for (uint16_t value = 0; value < 128; value += 2) {
         const uint16_t operands[][2] = {{value, 0}, {0, value}};
         for (unsigned operand = 0; operand < 2; ++operand) {
            struct agx3_encoded_i32_to_f32_reg packed, repacked;
            uint16_t dest, src;
            uint8_t length;
            CHECK(agx3_pack_i32_to_f32_reg(
               encodings[e], operands[operand][0], operands[operand][1],
               &packed));
            CHECK(agx3_unpack_i32_to_f32_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src,
               &length));
            CHECK(dest == operands[operand][0]);
            CHECK(src == operands[operand][1]);
            CHECK(agx3_pack_i32_to_f32_reg(
               encodings[e], dest, src, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_i32_to_f32_reg_rejection(void)
{
   static const uint8_t masks[][AGX3_I32_TO_F32_REG_MAX_LENGTH] = {
      {0x00, 0x00, 0x00, 0x7e, 0x00, 0xfc, 0x00, 0x00},
      {0x90, 0x02, 0x80, 0xf0, 0x00, 0x00, 0x00, 0x38, 0x01, 0x00},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B, AGX_ISA_ENCODING_AGX3C,
   };
   const uint16_t states[][2] = {{0, 0}, {126, 126}};

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned state = 0; state < 2; ++state) {
         struct agx3_encoded_i32_to_f32_reg packed, repacked;
         CHECK(agx3_pack_i32_to_f32_reg(
            encodings[e], states[state][0], states[state][1], &packed));

         for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
            uint8_t malformed[AGX3_I32_TO_F32_REG_MAX_LENGTH] = {0};
            memcpy(malformed, packed.bytes, packed.length);
            malformed[bit / 8] ^= 1u << (bit % 8);

            uint16_t dest = UINT16_MAX, src = UINT16_MAX;
            uint8_t length = 0;
            const bool accepted = agx3_unpack_i32_to_f32_reg(
               encodings[e], malformed, packed.length, &dest, &src, &length);
            const bool is_operand =
               (masks[e][bit / 8] & (1u << (bit % 8))) != 0;
            CHECK(accepted == is_operand);
            if (accepted) {
               CHECK(agx3_pack_i32_to_f32_reg(
                  encodings[e], dest, src, &repacked));
               CHECK(repacked.length == length);
               CHECK(memcmp(repacked.bytes, malformed, length) == 0);
            }
         }
      }
   }

   struct agx3_encoded_i32_to_f32_reg packed_b, packed_c;
   CHECK(agx3_pack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 0, &packed_b));
   CHECK(agx3_pack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, 0, 0, &packed_c));
   CHECK(!agx3_pack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX2, 0, 0, &packed_b));
   CHECK(!agx3_pack_i32_to_f32_reg(
      AGX_ISA_ENCODING_UNKNOWN, 0, 0, &packed_b));
   CHECK(!agx3_pack_i32_to_f32_reg(
      (enum agx_isa_encoding)99, 0, 0, &packed_b));
   CHECK(!agx3_pack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3B, 0, 0, NULL));

   const uint16_t invalid_values[] = {1, 127, 128, 255, 256};
   for (unsigned value = 0;
        value < sizeof(invalid_values) / sizeof(invalid_values[0]); ++value) {
      CHECK(!agx3_pack_i32_to_f32_reg(
         AGX_ISA_ENCODING_AGX3B, invalid_values[value], 0, &packed_b));
      CHECK(!agx3_pack_i32_to_f32_reg(
         AGX_ISA_ENCODING_AGX3C, 0, invalid_values[value], &packed_c));
   }

   uint16_t dest, src;
   uint8_t length;
   for (size_t available = 0; available < packed_b.length; ++available)
      CHECK(!agx3_unpack_i32_to_f32_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &dest, &src,
         &length));
   for (size_t available = 0; available < packed_c.length; ++available)
      CHECK(!agx3_unpack_i32_to_f32_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &dest, &src,
         &length));

   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src,
      &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src, &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src,
      &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src,
      NULL));

   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src,
      &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src,
      &length));

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
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3B, unsigned_b, sizeof(unsigned_b), &dest, &src,
      &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, unsigned_c, sizeof(unsigned_c), &dest, &src,
      &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3B, alternate_unsigned_b,
      sizeof(alternate_unsigned_b), &dest, &src, &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, alternate_unsigned_c,
      sizeof(alternate_unsigned_c), &dest, &src, &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3B, f32_to_i32_b, sizeof(f32_to_i32_b), &dest,
      &src, &length));
   CHECK(!agx3_unpack_i32_to_f32_reg(
      AGX_ISA_ENCODING_AGX3C, f32_to_i32_c, sizeof(f32_to_i32_c), &dest,
      &src, &length));
}

struct bit_and_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (0,0,2) row is the exact cross-target compiler witness.  Every row is
 * independently composed by Apple's table from that source-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct bit_and_reg_reg_vector bit_and_reg_reg_vectors[] = {
   {0, 0, 0,       "0b010f01000000800000", "2b0007803030a0020480"},
   {0, 0, 2,       "0b010f03000000800000", "2b0007803030a0028480"},
   {0, 4, 0,       "0b050f01000000800000", "2b0007803070a0020480"},
   {2, 2, 0,       "1b030f01000000800000", "3b0207803030a0020480"},
   {254, 254, 254, "fb7fcf7f001500800000", "bb028fba30f0a03a84bf"},
   {128, 64, 32,   "0b410f21001000800000", "2b000f903030a0020488"},
   {64, 128, 254,  "0b018f7f000500800000", "2b0087a03030a00284bf"},
   {32, 16, 8,     "0b114f09000000800000", "2b0007823030a0220482"},
};

static void
test_bit_and_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(bit_and_reg_reg_vectors) /
                  sizeof(bit_and_reg_reg_vectors[0]);
           ++i) {
         const struct bit_and_reg_reg_vector *v = &bit_and_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_BIT_AND_U32_REG_REG_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_bit_and_u32_reg_reg packed;

         CHECK(agx3_pack_bit_and_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_bit_and_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < 256; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_bit_and_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_bit_and_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_bit_and_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_bit_and_u32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_bit_and_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_BIT_AND_U32_REG_REG_LENGTH] = {
      /* AGX3b: bounded even f2/f16/f30 half-register bits. */
      {0xf0, 0x7e, 0xc0, 0x7e, 0, 0x15, 0, 0, 0, 0},
      /* AGX3c: bounded eight-bit scatters of the same fields. */
      {0x90, 0x02, 0x88, 0x3a, 0, 0xc0, 0, 0x38, 0x80, 0x3f},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_bit_and_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_bit_and_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_BIT_AND_U32_REG_REG_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_bit_and_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_bit_and_u32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_bit_and_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                       &packed_b));
   CHECK(agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                       &packed_c));

   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                        &packed_c));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                        &packed_c));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 256, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                        &packed_c));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                        &packed_c));
   CHECK(!agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                        NULL));

   CHECK(agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                       &packed_b));
   CHECK(agx3_pack_bit_and_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                       &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes,
      AGX3_BIT_AND_U32_REG_REG_LENGTH - 1, &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_bit_and_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct bit_or_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (0,0,2) row is the exact cross-target compiler witness.  Every row is
 * independently composed by Apple's table from that source-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct bit_or_reg_reg_vector bit_or_reg_reg_vectors[] = {
   {0, 0, 0,       "0b010f01020800800000", "2b0007803238a0020480"},
   {0, 0, 2,       "0b010f03020800800000", "2b0007803238a0028480"},
   {0, 4, 0,       "0b050f01020800800000", "2b0007803278a0020480"},
   {2, 2, 0,       "1b030f01020800800000", "3b0207803238a0020480"},
   {254, 254, 254, "fb7fcf7f021d00800000", "bb028fba32f8a03a84bf"},
   {128, 64, 32,   "0b410f21021800800000", "2b000f903238a0020488"},
   {64, 128, 254,  "0b018f7f020d00800000", "2b0087a03238a00284bf"},
   {32, 16, 8,     "0b114f09020800800000", "2b0007823238a0220482"},
};

static void
test_bit_or_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(bit_or_reg_reg_vectors) /
                  sizeof(bit_or_reg_reg_vectors[0]);
           ++i) {
         const struct bit_or_reg_reg_vector *v = &bit_or_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_BIT_OR_U32_REG_REG_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_bit_or_u32_reg_reg packed;

         CHECK(agx3_pack_bit_or_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_bit_or_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < 256; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_bit_or_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_bit_or_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_bit_or_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_bit_or_u32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_bit_or_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_BIT_OR_U32_REG_REG_LENGTH] = {
      /* AGX3b: bounded even f2/f16/f30 half-register bits. */
      {0xf0, 0x7e, 0xc0, 0x7e, 0, 0x15, 0, 0, 0, 0},
      /* AGX3c: bounded eight-bit scatters of the same fields. */
      {0x90, 0x02, 0x88, 0x3a, 0, 0xc0, 0, 0x38, 0x80, 0x3f},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_bit_or_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_bit_or_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_BIT_OR_U32_REG_REG_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_bit_or_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_bit_or_u32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_bit_or_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                      &packed_b));
   CHECK(agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                      &packed_c));

   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                       &packed_b));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                       &packed_b));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                       &packed_b));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                       &packed_c));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                       &packed_c));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 256, 0, 0,
                                       &packed_b));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                       &packed_c));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                       &packed_c));
   CHECK(!agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                       NULL));

   CHECK(agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                      &packed_b));
   CHECK(agx3_pack_bit_or_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                      &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes,
      AGX3_BIT_OR_U32_REG_REG_LENGTH - 1, &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_bit_or_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct bit_xor_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (0,0,2) row is the exact cross-target compiler witness.  Every row is
 * independently composed by Apple's table from that source-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct bit_xor_reg_reg_vector bit_xor_reg_reg_vectors[] = {
   {0, 0, 0,       "0b010e01020800800000", "2b0006803238a0020480"},
   {0, 0, 2,       "0b010e03020800800000", "2b0006803238a0028480"},
   {0, 4, 0,       "0b050e01020800800000", "2b0006803278a0020480"},
   {2, 2, 0,       "1b030e01020800800000", "3b0206803238a0020480"},
   {254, 254, 254, "fb7fce7f021d00800000", "bb028eba32f8a03a84bf"},
   {128, 64, 32,   "0b410e21021800800000", "2b000e903238a0020488"},
   {64, 128, 254,  "0b018e7f020d00800000", "2b0086a03238a00284bf"},
   {32, 16, 8,     "0b114e09020800800000", "2b0006823238a0220482"},
};

static void
test_bit_xor_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(bit_xor_reg_reg_vectors) /
                  sizeof(bit_xor_reg_reg_vectors[0]);
           ++i) {
         const struct bit_xor_reg_reg_vector *v = &bit_xor_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_BIT_XOR_U32_REG_REG_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_bit_xor_u32_reg_reg packed;

         CHECK(agx3_pack_bit_xor_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_bit_xor_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < 256; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_bit_xor_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_bit_xor_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_bit_xor_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_bit_xor_u32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_bit_xor_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_BIT_XOR_U32_REG_REG_LENGTH] = {
      /* AGX3b: bounded even f2/f16/f30 half-register bits. */
      {0xf0, 0x7e, 0xc0, 0x7e, 0, 0x15, 0, 0, 0, 0},
      /* AGX3c: bounded eight-bit scatters of the same fields. */
      {0x90, 0x02, 0x88, 0x3a, 0, 0xc0, 0, 0x38, 0x80, 0x3f},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_bit_xor_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_bit_xor_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_BIT_XOR_U32_REG_REG_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_bit_xor_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_bit_xor_u32_reg_reg(
               encodings[e], dest, src0, src1, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_bit_xor_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                       &packed_b));
   CHECK(agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                       &packed_c));

   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                        &packed_c));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                        &packed_c));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 256, 0, 0,
                                        &packed_b));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                        &packed_c));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                        &packed_c));
   CHECK(!agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                        NULL));

   CHECK(agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                       &packed_b));
   CHECK(agx3_pack_bit_xor_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                       &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes,
      AGX3_BIT_XOR_U32_REG_REG_LENGTH - 1, &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_bit_xor_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct bit_not_reg_vector {
   uint16_t dest;
   uint16_t src;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (6,0) row is the exact forced-live compiler witness.  Every row is
 * independently composed by Apple's table from that operand-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct bit_not_reg_vector bit_not_reg_vectors[] = {
   {0, 0,     "0b012600010800800000", "2b00061a2908a802"},
   {6, 0,     "3b012600010800800000", "3b00061a2908a80a"},
   {0, 2,     "0b032600010800800000", "2b02061a2908a802"},
   {2, 2,     "1b032600010800800000", "3b02061a2908a802"},
   {254, 254, "fb7fe600011900800000", "bb028efa2988ab3a"},
   {128, 64,  "0b412600011800800000", "2b000e1a2908a902"},
   {64, 128,  "0b01a600010900800000", "2b00861a2908aa02"},
   {32, 16,   "0b116600010800800000", "2b00069a2908a822"},
};

static void
test_bit_not_u32_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      for (unsigned i = 0;
           i < sizeof(bit_not_reg_vectors) / sizeof(bit_not_reg_vectors[0]);
           ++i) {
         const struct bit_not_reg_vector *v = &bit_not_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_BIT_NOT_U32_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_bit_not_u32_reg packed;

         CHECK(agx3_pack_bit_not_u32_reg(encodings[e], v->dest, v->src,
                                         &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_bit_not_u32_reg(encodings[e], expected,
                                           expected_length, &dest, &src,
                                           &length));
         CHECK(dest == v->dest);
         CHECK(src == v->src);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < 256; value += 2) {
         const uint16_t operands[][2] = {
            {value, 0}, {0, value},
         };
         for (unsigned o = 0; o < 2; ++o) {
            struct agx3_encoded_bit_not_u32_reg packed, repacked;
            uint16_t dest, src;
            uint8_t length;
            CHECK(agx3_pack_bit_not_u32_reg(
               encodings[e], operands[o][0], operands[o][1], &packed));
            CHECK(agx3_unpack_bit_not_u32_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src,
               &length));
            CHECK(dest == operands[o][0]);
            CHECK(src == operands[o][1]);
            CHECK(agx3_pack_bit_not_u32_reg(encodings[e], dest, src,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_bit_not_u32_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_BIT_NOT_U32_REG_MAX_LENGTH] = {
      /* AGX3b: bounded even f2/f16 half-register bits. */
      {0xf0, 0x7e, 0xc0, 0, 0, 0x11, 0, 0, 0, 0},
      /* AGX3c: bounded even eight-bit scatters of f2/f16. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0x03, 0x38, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_bit_not_u32_reg packed, repacked;
      CHECK(agx3_pack_bit_not_u32_reg(encodings[e], 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_BIT_NOT_U32_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src;
         uint8_t length;
         const bool accepted = agx3_unpack_bit_not_u32_reg(
            encodings[e], malformed, packed.length, &dest, &src, &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_bit_not_u32_reg(encodings[e], dest, src,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_bit_not_u32_reg packed_b, packed_c;
   CHECK(agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3B, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3C, 0, 0,
                                   &packed_c));

   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX2, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3B, 1, 0,
                                    &packed_b));
   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3C, 0, 1,
                                    &packed_c));
   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3B, 256, 0,
                                    &packed_b));
   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3C, 0, 256,
                                    &packed_c));
   CHECK(!agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, NULL));

   CHECK(agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3B, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_bit_not_u32_reg(AGX_ISA_ENCODING_AGX3C, 0, 0,
                                   &packed_c));

   uint16_t dest, src;
   uint8_t length;
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, packed_b.length - 1, &dest,
      &src, &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length - 1, &dest,
      &src, &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src,
      &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &dest, &src,
      &length));

   uint8_t padded_c[AGX3_BIT_NOT_U32_REG_MAX_LENGTH] = {0};
   memcpy(padded_c, packed_c.bytes, packed_c.length);
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3B, padded_c, sizeof(padded_c), &dest, &src,
      &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src, &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src,
      &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &length));
   CHECK(!agx3_unpack_bit_not_u32_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src,
      NULL));
}

struct isub_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (6,4,0) row is the exact forced-live compiler witness.  Every row is
 * independently composed by Apple's table from that operand-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct isub_reg_reg_vector isub_reg_reg_vectors[] = {
   {0, 0, 0,       "1f015600020000a81105", "2f00041a2100a20208800100"},
   {6, 4, 0,       "1f015606020800a81105", "3f00043a2100a20a08800100"},
   {0, 0, 2,       "1f015600020008a81105", "2f00041a2100a20288800100"},
   {2, 2, 0,       "1f015602020400a81105", "3f02041a2100a20208800100"},
   {190, 190, 190, "1f0156be027cf9aa1105", "bf020cfa2180a23a8aaf0100"},
   {128, 64, 32,   "1f015680028080a81105", "2f000c1a2100a20209880100"},
   {64, 128, 190,  "1f0156400200f9aa1105", "2f00841a2100a2028aaf0100"},
   {32, 16, 8,     "1f015620022020a81105", "2f00049a2100a22208820100"},
};

static void
test_isub_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(isub_reg_reg_vectors) /
                  sizeof(isub_reg_reg_vectors[0]);
           ++i) {
         const struct isub_reg_reg_vector *v = &isub_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_ISUB_U32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_isub_u32_reg_reg packed;

         CHECK(agx3_pack_isub_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_isub_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_isub_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_isub_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_isub_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_isub_u32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_isub_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_ISUB_U32_REG_REG_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0},
      /* AGX3c: the bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_isub_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_isub_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_ISUB_U32_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_isub_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_isub_u32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_isub_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                     &packed_c));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                     &packed_c));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                     &packed_b));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 192,
                                     &packed_b));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                     &packed_c));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                     &packed_c));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                     &packed_c));
   CHECK(!agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                     NULL));

   CHECK(agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_isub_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, 9, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 11, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, 10, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, 12, &dest, &src0, &src1,
      &length));

   uint8_t padded_b[AGX3_ISUB_U32_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, 12, &dest, &src0, &src1, &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, NULL, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, NULL, &src1,
      &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, &src0, NULL,
      &length));
   CHECK(!agx3_unpack_isub_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, 12, &dest, &src0, &src1,
      NULL));
}

struct sll_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (6,10,0) row is the exact forced-live compiler witness.  Every row is
 * independently composed by Apple's table from that operand-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct sll_reg_reg_vector sll_reg_reg_vectors[] = {
   {0, 0, 0,       "2710540002000000f0100500",     "2704001a2200a002088000340000"},
   {6, 10, 0,      "2710540602140000f0100500",     "3706005a2200a00a088000340000"},
   {0, 0, 2,       "2710540002000800f0100500",     "2704001a2200a002888000340000"},
   {2, 2, 0,       "2710540202040000f0100500",     "3706001a2200a002088000340000"},
   {190, 190, 190, "271054be027cf902f0100500",     "b70608fa2280a03a8aaf00340000"},
   {128, 64, 32,   "2710548002808000f0100500",     "2704081a2200a002098800340000"},
   {64, 128, 190,  "271054400200f902f0100500",     "2704801a2200a0028aaf00340000"},
   {32, 16, 8,     "2710542002202000f0100500",     "2704009a2200a022088200340000"},
};

static void
test_sll_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(sll_reg_reg_vectors) / sizeof(sll_reg_reg_vectors[0]);
           ++i) {
         const struct sll_reg_reg_vector *v = &sll_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_SLL_U32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_sll_u32_reg_reg packed;

         CHECK(agx3_pack_sll_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_sll_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_sll_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_sll_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_sll_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_sll_u32_reg_reg(encodings[e], dest, src0, src1,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_sll_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_SLL_U32_REG_REG_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0, 0, 0},
      /* AGX3c: the bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_sll_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_sll_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_SLL_U32_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_sll_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_sll_u32_reg_reg(encodings[e], dest, src0, src1,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_sll_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                   &packed_c));

   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                    &packed_c));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                    &packed_c));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                    &packed_b));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 192,
                                    &packed_b));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                    &packed_c));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                    &packed_c));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                    &packed_c));
   CHECK(!agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    NULL));

   CHECK(agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_sll_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                   &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, packed_b.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));

   uint8_t padded_b[AGX3_SLL_U32_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_sll_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct asr_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (6,10,0) row is the exact forced-live compiler witness.  Every row is
 * independently composed by Apple's table from that operand-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct asr_reg_reg_vector asr_reg_reg_vectors[] = {
   {0, 0, 0,       "a711540002000018e200",     "2704001a2200a30208004000"},
   {6, 10, 0,      "a711540602140018e200",     "3706005a2200a30a08004000"},
   {0, 0, 2,       "a711540002000818e200",     "2704001a2200a30288004000"},
   {2, 2, 0,       "a711540202040018e200",     "3706001a2200a30208004000"},
   {190, 190, 190, "a71154be027cf91ae200",     "b70608fa2280a33a8a2f4000"},
   {128, 64, 32,   "a711548002808018e200",     "2704081a2200a30209084000"},
   {64, 128, 190,  "a71154400200f91ae200",     "2704801a2200a3028a2f4000"},
   {32, 16, 8,     "a711542002202018e200",     "2704009a2200a32208024000"},
};

static void
test_asr_i32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(asr_reg_reg_vectors) / sizeof(asr_reg_reg_vectors[0]);
           ++i) {
         const struct asr_reg_reg_vector *v = &asr_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_ASR_I32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_asr_i32_reg_reg packed;

         CHECK(agx3_pack_asr_i32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_asr_i32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_asr_i32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_asr_i32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_asr_i32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_asr_i32_reg_reg(encodings[e], dest, src0, src1,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_asr_i32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_ASR_I32_REG_REG_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0},
      /* AGX3c: the bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_asr_i32_reg_reg packed, repacked;
      CHECK(agx3_pack_asr_i32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_ASR_I32_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_asr_i32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_asr_i32_reg_reg(encodings[e], dest, src0, src1,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_asr_i32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                   &packed_c));

   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                    &packed_c));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                    &packed_c));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                    &packed_b));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 192,
                                    &packed_b));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                    &packed_c));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                    &packed_c));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                    &packed_c));
   CHECK(!agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    NULL));

   CHECK(agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_asr_i32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                   &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, packed_b.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));

   uint8_t padded_b[AGX3_ASR_I32_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_asr_i32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct lsr_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (6,10,0) row is the exact forced-live compiler witness.  Every row is
 * independently composed by Apple's table from that operand-correlated
 * encoding point; the remaining values exercise every exposed fragment.
 */
static const struct lsr_reg_reg_vector lsr_reg_reg_vectors[] = {
   {0, 0, 0,       "a710540002000000f0100500",     "2704001a2200a102088000340000"},
   {6, 10, 0,      "a710540602140000f0100500",     "3706005a2200a10a088000340000"},
   {0, 0, 2,       "a710540002000800f0100500",     "2704001a2200a102888000340000"},
   {2, 2, 0,       "a710540202040000f0100500",     "3706001a2200a102088000340000"},
   {190, 190, 190, "a71054be027cf902f0100500",     "b70608fa2280a13a8aaf00340000"},
   {128, 64, 32,   "a710548002808000f0100500",     "2704081a2200a102098800340000"},
   {64, 128, 190,  "a71054400200f902f0100500",     "2704801a2200a1028aaf00340000"},
   {32, 16, 8,     "a710542002202000f0100500",     "2704009a2200a122088200340000"},
};

static void
test_lsr_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(lsr_reg_reg_vectors) / sizeof(lsr_reg_reg_vectors[0]);
           ++i) {
         const struct lsr_reg_reg_vector *v = &lsr_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_LSR_U32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_lsr_u32_reg_reg packed;

         CHECK(agx3_pack_lsr_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_lsr_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_lsr_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_lsr_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_lsr_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_lsr_u32_reg_reg(encodings[e], dest, src0, src1,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_lsr_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_LSR_U32_REG_REG_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0, 0, 0},
      /* AGX3c: the bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_lsr_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_lsr_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_LSR_U32_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_lsr_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_lsr_u32_reg_reg(encodings[e], dest, src0, src1,
                                            &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_lsr_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                   &packed_c));

   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                    &packed_c));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                    &packed_c));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                    &packed_b));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                    &packed_b));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 192,
                                    &packed_b));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                    &packed_c));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                    &packed_c));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                    &packed_c));
   CHECK(!agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    NULL));

   CHECK(agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                   &packed_b));
   CHECK(agx3_pack_lsr_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                   &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, packed_b.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));

   uint8_t padded_b[AGX3_LSR_U32_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_lsr_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct imul_reg_reg_vector {
   uint16_t dest;
   uint16_t src0;
   uint16_t src1;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (6,0,4) row is the exact forced-live compiler witness.  Every row is
 * independently composed by Apple's table from that operand-correlated,
 * implicit-zero-addend encoding point.
 */
static const struct imul_reg_reg_vector imul_reg_reg_vectors[] = {
   {0, 0, 0,       "9f00560002000000d0200a00",     "2f00041a2100a102088000000102"},
   {6, 0, 4,       "9f00560602001000d0200a00",     "3f00041a2100a10a088100000102"},
   {0, 0, 2,       "9f00560002000800d0200a00",     "2f00041a2100a102888000000102"},
   {2, 2, 0,       "9f00560202040000d0200a00",     "3f02041a2100a102088000000102"},
   {190, 190, 190, "9f0056be027cf902d0200a00",     "bf020cfa2180a13a8aaf00000102"},
   {128, 64, 32,   "9f00568002808000d0200a00",     "2f000c1a2100a102098800000102"},
   {64, 128, 190,  "9f0056400200f902d0200a00",     "2f00841a2100a1028aaf00000102"},
   {32, 16, 8,     "9f00562002202000d0200a00",     "2f00049a2100a122088200000102"},
};

static void
test_imul_u32_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(imul_reg_reg_vectors) / sizeof(imul_reg_reg_vectors[0]);
           ++i) {
         const struct imul_reg_reg_vector *v = &imul_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_IMUL_U32_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_imul_u32_reg_reg packed;

         CHECK(agx3_pack_imul_u32_reg_reg(
            encodings[e], v->dest, v->src0, v->src1, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, src0 = UINT16_MAX, src1 = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_imul_u32_reg_reg(
            encodings[e], expected, expected_length, &dest, &src0, &src1,
            &length));
         CHECK(dest == v->dest);
         CHECK(src0 == v->src0);
         CHECK(src1 == v->src1);
         CHECK(length == expected_length);
      }

      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][3] = {
            {value, 0, 0}, {0, value, 0}, {0, 0, value},
         };
         for (unsigned o = 0; o < 3; ++o) {
            struct agx3_encoded_imul_u32_reg_reg packed, repacked;
            uint16_t dest, src0, src1;
            uint8_t length;
            CHECK(agx3_pack_imul_u32_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               &packed));
            CHECK(agx3_unpack_imul_u32_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &src0,
               &src1, &length));
            CHECK(dest == operands[o][0]);
            CHECK(src0 == operands[o][1]);
            CHECK(src1 == operands[o][2]);
            CHECK(agx3_pack_imul_u32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_imul_u32_reg_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_IMUL_U32_REG_REG_MAX_LENGTH] = {
      /* AGX3b: even f2/f16/f31 half-register bits. */
      {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0x03, 0, 0, 0, 0, 0, 0},
      /* AGX3c: the bounded eight-bit forms of the same three fields. */
      {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x83, 0x3f, 0, 0, 0, 0},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_imul_u32_reg_reg packed, repacked;
      CHECK(agx3_pack_imul_u32_reg_reg(encodings[e], 0, 0, 0, &packed));

      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_IMUL_U32_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, src0, src1;
         uint8_t length;
         const bool accepted = agx3_unpack_imul_u32_reg_reg(
            encodings[e], malformed, packed.length, &dest, &src0, &src1,
            &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_imul_u32_reg_reg(encodings[e], dest, src0, src1,
                                             &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_imul_u32_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 1, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 1, 0,
                                     &packed_c));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 1,
                                     &packed_c));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 192, 0, 0,
                                     &packed_b));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 192, 0,
                                     &packed_b));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 192,
                                     &packed_b));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 256, 0, 0,
                                     &packed_c));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 256, 0,
                                     &packed_c));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 256,
                                     &packed_c));
   CHECK(!agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                     NULL));

   CHECK(agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                    &packed_b));
   CHECK(agx3_pack_imul_u32_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                    &packed_c));

   uint16_t dest, src0, src1;
   uint8_t length;
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, packed_b.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length - 1, &dest,
      &src0, &src1, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, &length));

   uint8_t padded_b[AGX3_IMUL_U32_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &src0, &src1,
      &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &src0,
      &src1, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &src1, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      NULL, &length));
   CHECK(!agx3_unpack_imul_u32_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &src0,
      &src1, NULL));
}

struct imadd_reg_reg_reg_vector {
   uint16_t dest;
   uint16_t mul0;
   uint16_t mul1;
   uint16_t addend;
   const char *agx3b;
   const char *agx3c;
};

/*
 * The (10,4,6,0) row is the exact forced-live four-store compiler witness.
 * Every row is independently composed by Apple's table from that
 * operand-correlated, real-register-addend encoding point.
 */
static const struct imadd_reg_reg_reg_vector imadd_reg_reg_reg_vectors[] = {
   {0, 0, 0, 0,       "9f00560002000000d0212a00",
                       "2f00041a2100a102088040000000"},
   {10, 4, 6, 0,      "9f00560a02081800d0212a00",
                       "3f00043a2100a112888140000000"},
   {0, 0, 0, 2,       "9f00560002000010d0212a00",
                       "2f00041a2100a102088040000200"},
   {2, 2, 2, 2,       "9f00560202040810d0212a00",
                       "3f02041a2100a102888040000200"},
   {190, 190, 190, 190, "9f0056be027cf9f2d5212a00",
                         "bf020cfa2180a13a9aaf4000ae00"},
   {128, 64, 32, 16,  "9f00568002808080d0212a00",
                       "2f000c1a2100a102198840000000"},
   {64, 128, 190, 190, "9f0056400200f9f2d5212a00",
                        "2f00841a2100a1029aaf4000ae00"},
   {32, 16, 8, 128,   "9f00562002202000d4212a00",
                       "2f00049a2100a122088240008000"},
};

static void
test_imadd_u32_reg_reg_reg(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t register_limit = e == 0 ? 192 : 256;

      for (unsigned i = 0;
           i < sizeof(imadd_reg_reg_reg_vectors) /
                  sizeof(imadd_reg_reg_reg_vectors[0]);
           ++i) {
         const struct imadd_reg_reg_reg_vector *v =
            &imadd_reg_reg_reg_vectors[i];
         const char *hex = e == 0 ? v->agx3b : v->agx3c;
         uint8_t expected[AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH] = {0};
         const unsigned expected_length =
            parse_hex(hex, expected, sizeof(expected));
         struct agx3_encoded_imadd_u32_reg_reg_reg packed;

         CHECK(agx3_pack_imadd_u32_reg_reg_reg(
            encodings[e], v->dest, v->mul0, v->mul1, v->addend, &packed));
         CHECK(packed.length == expected_length);
         CHECK(memcmp(packed.bytes, expected, expected_length) == 0);

         uint16_t dest = UINT16_MAX, mul0 = UINT16_MAX, mul1 = UINT16_MAX;
         uint16_t addend = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_unpack_imadd_u32_reg_reg_reg(
            encodings[e], expected, expected_length, &dest, &mul0, &mul1,
            &addend, &length));
         CHECK(dest == v->dest);
         CHECK(mul0 == v->mul0);
         CHECK(mul1 == v->mul1);
         CHECK(addend == v->addend);
         CHECK(length == expected_length);
      }

      /* Independently exhaust every modeled even half-register operand. */
      for (uint16_t value = 0; value < register_limit; value += 2) {
         const uint16_t operands[][4] = {
            {value, 0, 0, 0}, {0, value, 0, 0},
            {0, 0, value, 0}, {0, 0, 0, value},
         };
         for (unsigned o = 0; o < 4; ++o) {
            struct agx3_encoded_imadd_u32_reg_reg_reg packed, repacked;
            uint16_t dest, mul0, mul1, addend;
            uint8_t length;
            CHECK(agx3_pack_imadd_u32_reg_reg_reg(
               encodings[e], operands[o][0], operands[o][1], operands[o][2],
               operands[o][3], &packed));
            CHECK(agx3_unpack_imadd_u32_reg_reg_reg(
               encodings[e], packed.bytes, packed.length, &dest, &mul0,
               &mul1, &addend, &length));
            CHECK(dest == operands[o][0]);
            CHECK(mul0 == operands[o][1]);
            CHECK(mul1 == operands[o][2]);
            CHECK(addend == operands[o][3]);
            CHECK(agx3_pack_imadd_u32_reg_reg_reg(
               encodings[e], dest, mul0, mul1, addend, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
         }
      }
   }
}

static void
test_imadd_u32_reg_reg_reg_rejection(void)
{
   static const uint8_t
      operand_masks[][AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH] = {
         /* AGX3b: even f2/f16/f31/f46 half-register bits. */
         {0, 0, 0, 0xfe, 0, 0xfc, 0xf9, 0xf3, 0x07, 0, 0, 0, 0, 0},
         /* AGX3c: bounded eight-bit forms of all four fields. */
         {0x90, 0x02, 0x88, 0xe0, 0, 0x80, 0, 0x38, 0x93, 0x3f, 0x01, 0,
          0xae, 0},
      };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_imadd_u32_reg_reg_reg packed, repacked;
      CHECK(agx3_pack_imadd_u32_reg_reg_reg(encodings[e], 0, 0, 0, 0,
                                            &packed));

      /* Flipping every modeled operand bit must round-trip; every other bit
       * must fail canonical decode rather than selecting an unproven form. */
      for (unsigned bit = 0; bit < packed.length * 8; ++bit) {
         uint8_t malformed[AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH] = {0};
         memcpy(malformed, packed.bytes, packed.length);
         malformed[bit / 8] ^= 1u << (bit % 8);

         uint16_t dest, mul0, mul1, addend;
         uint8_t length;
         const bool accepted = agx3_unpack_imadd_u32_reg_reg_reg(
            encodings[e], malformed, packed.length, &dest, &mul0, &mul1,
            &addend, &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);

         if (accepted) {
            CHECK(agx3_pack_imadd_u32_reg_reg_reg(
               encodings[e], dest, mul0, mul1, addend, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, malformed, length) == 0);
         }
      }
   }

   struct agx3_encoded_imadd_u32_reg_reg_reg packed_b, packed_c;
   CHECK(agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                         0, &packed_b));
   CHECK(agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                         0, &packed_c));

   CHECK(!agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_AGX2, 0, 0, 0,
                                          0, &packed_b));
   CHECK(!agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_UNKNOWN, 0, 0, 0,
                                          0, &packed_b));

   /* Every operand independently rejects odd and first-out-of-range values. */
   for (unsigned e = 0; e < 2; ++e) {
      const uint16_t limit = e == 0 ? 192 : 256;
      struct agx3_encoded_imadd_u32_reg_reg_reg *out = e == 0 ? &packed_b
                                                               : &packed_c;
      for (unsigned o = 0; o < 4; ++o) {
         uint16_t odd[4] = {0};
         odd[o] = 1;
         CHECK(!agx3_pack_imadd_u32_reg_reg_reg(
            encodings[e], odd[0], odd[1], odd[2], odd[3], out));

         uint16_t oob[4] = {0};
         oob[o] = limit;
         CHECK(!agx3_pack_imadd_u32_reg_reg_reg(
            encodings[e], oob[0], oob[1], oob[2], oob[3], out));
      }
   }
   CHECK(!agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                          0, NULL));

   CHECK(agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_AGX3B, 0, 0, 0,
                                         0, &packed_b));
   CHECK(agx3_pack_imadd_u32_reg_reg_reg(AGX_ISA_ENCODING_AGX3C, 0, 0, 0,
                                         0, &packed_c));

   uint16_t dest, mul0, mul1, addend;
   uint8_t length;
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_b.bytes, packed_b.length - 1, &dest,
      &mul0, &mul1, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length - 1, &dest,
      &mul0, &mul1, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &dest, &mul0,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &dest, &mul0,
      &mul1, &addend, &length));

   uint8_t padded_b[AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH] = {0};
   memcpy(padded_b, packed_b.bytes, packed_b.length);
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, padded_b, sizeof(padded_b), &dest, &mul0,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &dest, &mul0, &mul1,
      &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL, &mul0,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, NULL,
      &mul1, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &mul0,
      NULL, &addend, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &mul0,
      &mul1, NULL, &length));
   CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &dest, &mul0,
      &mul1, &addend, NULL));

   /* Reject the separate implicit-zero multiply and all observed alternate or
    * high-half selections, even where they implement related arithmetic. */
   static const char *reject_b[] = {
      "9f00560002000000d0200a00", /* implicit-zero imul */
      "9f00560002080040d02f2a00", /* alternate P3 low */
      "9f00560002080010d02f2a00", /* alternate Q low */
      "9f00560003000800e0261e00", /* signed high */
      "9f00560003000800e0260a00", /* unsigned high */
   };
   static const char *reject_c[] = {
      "2f00041a2100a102088000000102", /* implicit-zero imul */
      "2f00043a2100a1022880e0000800", /* alternate P3 low */
      "2f00043a2100a1022880e0000200", /* alternate Q low */
      "2f0004022908a902a88020b0",     /* signed high imadd64 */
      "2f0004022900a902a8802030",     /* unsigned high imadd64 */
   };
   for (unsigned e = 0; e < 2; ++e) {
      const char *const *vectors = e == 0 ? reject_b : reject_c;
      for (unsigned i = 0; i < 5; ++i) {
         uint8_t rejected[AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH] = {0};
         const unsigned encoded_length =
            parse_hex(vectors[i], rejected, sizeof(rejected));
         const size_t available = e == 1 && encoded_length < 14
                                     ? sizeof(rejected)
                                     : encoded_length;
         CHECK(!agx3_unpack_imadd_u32_reg_reg_reg(
            encodings[e], rejected, available, &dest, &mul0, &mul1, &addend,
            &length));
      }
   }
}

static void
test_yield_i16_reg(void)
{
   static const struct {
      enum agx_isa_encoding encoding;
      uint16_t src;
      uint8_t expected[AGX3_YIELD_I16_REG_LENGTH];
   } vectors[] = {
      {AGX_ISA_ENCODING_AGX3B, 0,   {0x87, 0x04, 0x54, 0x00, 0x04, 0x00}},
      {AGX_ISA_ENCODING_AGX3B, 2,   {0x87, 0x04, 0x54, 0x02, 0x04, 0x00}},
      {AGX_ISA_ENCODING_AGX3B, 5,   {0x87, 0x04, 0x54, 0x05, 0x04, 0x00}},
      {AGX_ISA_ENCODING_AGX3B, 191, {0x87, 0x04, 0x54, 0xbf, 0x04, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 0,   {0x0f, 0x00, 0x00, 0x02, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 2,   {0x0f, 0x02, 0x00, 0x02, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 5,   {0x0f, 0x05, 0x00, 0x02, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 127, {0x0f, 0x7f, 0x00, 0x02, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 128, {0x0f, 0x00, 0x10, 0x02, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 255, {0x0f, 0x7f, 0x10, 0x02, 0x00, 0x00}},
   };

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      struct agx3_encoded_yield_i16_reg packed;
      CHECK(agx3_pack_yield_i16_reg(vectors[v].encoding, vectors[v].src,
                                    &packed));
      CHECK(packed.length == AGX3_YIELD_I16_REG_LENGTH);
      CHECK(memcmp(packed.bytes, vectors[v].expected,
                   AGX3_YIELD_I16_REG_LENGTH) == 0);

      uint16_t src = UINT16_MAX;
      uint8_t length = 0;
      CHECK(agx3_unpack_yield_i16_reg(
         vectors[v].encoding, vectors[v].expected,
         AGX3_YIELD_I16_REG_LENGTH, &src, &length));
      CHECK(src == vectors[v].src);
      CHECK(length == AGX3_YIELD_I16_REG_LENGTH);
   }

   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };
   const uint16_t source_counts[] = {192, 256};
   for (unsigned e = 0; e < 2; ++e) {
      for (uint16_t expected_src = 0; expected_src < source_counts[e];
           ++expected_src) {
         struct agx3_encoded_yield_i16_reg packed, repacked;
         uint16_t src = UINT16_MAX;
         uint8_t length = 0;
         CHECK(agx3_pack_yield_i16_reg(encodings[e], expected_src, &packed));
         CHECK(agx3_unpack_yield_i16_reg(
            encodings[e], packed.bytes, packed.length, &src, &length));
         CHECK(src == expected_src);
         CHECK(length == packed.length);
         CHECK(agx3_pack_yield_i16_reg(encodings[e], src, &repacked));
         CHECK(repacked.length == length);
         CHECK(memcmp(repacked.bytes, packed.bytes, length) == 0);
      }
   }
}

static void
test_yield_i16_reg_rejection(void)
{
   static const uint8_t operand_masks[][AGX3_YIELD_I16_REG_LENGTH] = {
      /* AGX3b f2 is the complete byte 3. */
      {0x00, 0x00, 0x00, 0xff, 0x00, 0x00},
      /* AGX3c exposes f2[0:7]; the ninth register bit stays closed. */
      {0x00, 0x7f, 0x10, 0x00, 0x00, 0x00},
   };
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_yield_i16_reg packed, repacked;
      CHECK(agx3_pack_yield_i16_reg(encodings[e], 0, &packed));

      for (unsigned bit = 0; bit < AGX3_YIELD_I16_REG_LENGTH * 8; ++bit) {
         uint8_t mutated[AGX3_YIELD_I16_REG_LENGTH];
         memcpy(mutated, packed.bytes, sizeof(mutated));
         mutated[bit / 8] ^= 1u << (bit % 8);

         uint16_t src = UINT16_MAX;
         uint8_t length = 0;
         const bool accepted = agx3_unpack_yield_i16_reg(
            encodings[e], mutated, sizeof(mutated), &src, &length);
         const bool is_operand =
            (operand_masks[e][bit / 8] & (1u << (bit % 8))) != 0;
         CHECK(accepted == is_operand);
         if (accepted) {
            CHECK(agx3_pack_yield_i16_reg(encodings[e], src, &repacked));
            CHECK(repacked.length == length);
            CHECK(memcmp(repacked.bytes, mutated, length) == 0);
         }
      }
   }

   struct agx3_encoded_yield_i16_reg packed_b, packed_c;
   CHECK(agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3B, 2, &packed_b));
   CHECK(agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3C, 2, &packed_c));

   CHECK(!agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX2, 0, &packed_b));
   CHECK(!agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_UNKNOWN, 0, &packed_b));
   CHECK(!agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3B, 192, &packed_b));
   CHECK(!agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3B, 256, &packed_b));
   CHECK(!agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3C, 256, &packed_c));
   CHECK(!agx3_pack_yield_i16_reg(AGX_ISA_ENCODING_AGX3C, 0, NULL));

   uint16_t src = UINT16_MAX;
   uint8_t length = 0;
   for (unsigned raw_src = 192; raw_src < 256; ++raw_src) {
      uint8_t out_of_table_range[AGX3_YIELD_I16_REG_LENGTH];
      memcpy(out_of_table_range, packed_b.bytes, sizeof(out_of_table_range));
      out_of_table_range[3] = raw_src;
      CHECK(!agx3_unpack_yield_i16_reg(
         AGX_ISA_ENCODING_AGX3B, out_of_table_range,
         sizeof(out_of_table_range), &src, &length));
   }

   for (unsigned raw_src = 256; raw_src < 512; ++raw_src) {
      uint8_t unmodeled_ninth_bit[AGX3_YIELD_I16_REG_LENGTH];
      memcpy(unmodeled_ninth_bit, packed_c.bytes,
             sizeof(unmodeled_ninth_bit));
      unmodeled_ninth_bit[1] = raw_src & 0x7f;
      unmodeled_ninth_bit[2] = (raw_src >> 3) & 0x30;
      CHECK(!agx3_unpack_yield_i16_reg(
         AGX_ISA_ENCODING_AGX3C, unmodeled_ninth_bit,
         sizeof(unmodeled_ninth_bit), &src, &length));
   }

   for (size_t available = 0; available < AGX3_YIELD_I16_REG_LENGTH;
        ++available) {
      CHECK(!agx3_unpack_yield_i16_reg(
         AGX_ISA_ENCODING_AGX3B, packed_b.bytes, available, &src, &length));
      CHECK(!agx3_unpack_yield_i16_reg(
         AGX_ISA_ENCODING_AGX3C, packed_c.bytes, available, &src, &length));
   }

   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX2, packed_b.bytes, packed_b.length, &src, &length));
   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_UNKNOWN, packed_c.bytes, packed_c.length, &src,
      &length));
   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX3B, packed_c.bytes, packed_c.length, &src,
      &length));
   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX3C, packed_b.bytes, packed_b.length, &src,
      &length));
   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX3C, NULL, packed_c.length, &src, &length));
   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, NULL,
      &length));
   CHECK(!agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX3C, packed_c.bytes, packed_c.length, &src, NULL));

   /* Prefix decode consumes exactly one instruction. */
   uint8_t appended[AGX3_YIELD_I16_REG_LENGTH + 1];
   memcpy(appended, packed_c.bytes, packed_c.length);
   appended[packed_c.length] = 0xff;
   CHECK(agx3_unpack_yield_i16_reg(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &src, &length));
   CHECK(src == 2);
   CHECK(length == AGX3_YIELD_I16_REG_LENGTH);

   /* Alternate operand selections and context bits are deliberately closed. */
   static const uint8_t rejected[][AGX3_YIELD_I16_REG_LENGTH] = {
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
      CHECK(!agx3_unpack_yield_i16_reg(
         i < 4 ? AGX_ISA_ENCODING_AGX3B : AGX_ISA_ENCODING_AGX3C,
         rejected[i], sizeof(rejected[i]), &src, &length));
   }
}

static void
test_nop(void)
{
   static const uint8_t expected[AGX3_NOP_LENGTH] = {0x06, 0x00};
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_nop packed;
      CHECK(agx3_pack_nop(encodings[e], &packed));
      CHECK(packed.length == AGX3_NOP_LENGTH);
      CHECK(memcmp(packed.bytes, expected, sizeof(expected)) == 0);

      uint8_t length = 0;
      CHECK(agx3_unpack_nop(encodings[e], expected, sizeof(expected),
                            &length));
      CHECK(length == AGX3_NOP_LENGTH);

      for (unsigned bit = 0; bit < AGX3_NOP_LENGTH * 8; ++bit) {
         uint8_t malformed[AGX3_NOP_LENGTH];
         memcpy(malformed, expected, sizeof(malformed));
         malformed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!agx3_unpack_nop(encodings[e], malformed, sizeof(malformed),
                                &length));
      }
   }

   struct agx3_encoded_nop packed;
   uint8_t length = 0;
   CHECK(!agx3_pack_nop(AGX_ISA_ENCODING_AGX2, &packed));
   CHECK(!agx3_pack_nop(AGX_ISA_ENCODING_UNKNOWN, &packed));
   CHECK(!agx3_pack_nop(AGX_ISA_ENCODING_AGX3B, NULL));
   CHECK(!agx3_unpack_nop(AGX_ISA_ENCODING_AGX2, expected,
                          sizeof(expected), &length));
   CHECK(!agx3_unpack_nop(AGX_ISA_ENCODING_AGX3C, expected,
                          AGX3_NOP_LENGTH - 1, &length));
   CHECK(!agx3_unpack_nop(AGX_ISA_ENCODING_AGX3C, NULL, sizeof(expected),
                          &length));
   CHECK(!agx3_unpack_nop(AGX_ISA_ENCODING_AGX3C, expected,
                          sizeof(expected), NULL));
}

static void
test_threadgroup_barrier(void)
{
   static const struct {
      enum agx_isa_encoding encoding;
      uint8_t expected[AGX3_THREADGROUP_BARRIER_LENGTH];
   } vectors[] = {
      {AGX_ISA_ENCODING_AGX3B, {0x07, 0x04, 0x54, 0x61, 0x09, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, {0x27, 0x51, 0x00, 0x06, 0x00, 0x00}},
   };
   struct agx3_encoded_threadgroup_barrier packed;
   uint8_t length = 0;

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      CHECK(agx3_pack_threadgroup_barrier(vectors[v].encoding, &packed));
      CHECK(packed.length == AGX3_THREADGROUP_BARRIER_LENGTH);
      CHECK(memcmp(packed.bytes, vectors[v].expected,
                   AGX3_THREADGROUP_BARRIER_LENGTH) == 0);
      CHECK(agx3_unpack_threadgroup_barrier(
         vectors[v].encoding, vectors[v].expected,
         AGX3_THREADGROUP_BARRIER_LENGTH, &length));
      CHECK(length == AGX3_THREADGROUP_BARRIER_LENGTH);

      for (unsigned bit = 0; bit < AGX3_THREADGROUP_BARRIER_LENGTH * 8; ++bit) {
         uint8_t malformed[AGX3_THREADGROUP_BARRIER_LENGTH];
         memcpy(malformed, vectors[v].expected, sizeof(malformed));
         malformed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!agx3_unpack_threadgroup_barrier(
            vectors[v].encoding, malformed, sizeof(malformed), &length));
      }

      CHECK(!agx3_unpack_threadgroup_barrier(
         vectors[v ^ 1].encoding, vectors[v].expected,
         AGX3_THREADGROUP_BARRIER_LENGTH, &length));
   }

   CHECK(!agx3_pack_threadgroup_barrier(AGX_ISA_ENCODING_AGX2, &packed));
   CHECK(!agx3_pack_threadgroup_barrier(AGX_ISA_ENCODING_UNKNOWN, &packed));
   CHECK(!agx3_pack_threadgroup_barrier(AGX_ISA_ENCODING_AGX3C, NULL));
   CHECK(!agx3_unpack_threadgroup_barrier(
      AGX_ISA_ENCODING_AGX3C, vectors[1].expected,
      AGX3_THREADGROUP_BARRIER_LENGTH - 1, &length));
   CHECK(!agx3_unpack_threadgroup_barrier(AGX_ISA_ENCODING_AGX3C, NULL,
                                          AGX3_THREADGROUP_BARRIER_LENGTH,
                                          &length));
   CHECK(!agx3_unpack_threadgroup_barrier(
      AGX_ISA_ENCODING_AGX3C, vectors[1].expected,
      AGX3_THREADGROUP_BARRIER_LENGTH, NULL));
}

static void
test_end(void)
{
   static const uint8_t expected[AGX3_END_LENGTH] = {0x0e, 0x00, 0x00, 0x00};
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      struct agx3_encoded_end packed;
      CHECK(agx3_pack_end(encodings[e], &packed));
      CHECK(packed.length == AGX3_END_LENGTH);
      CHECK(memcmp(packed.bytes, expected, sizeof(expected)) == 0);

      uint8_t length = 0;
      CHECK(agx3_unpack_end(encodings[e], expected, sizeof(expected),
                            &length));
      CHECK(length == AGX3_END_LENGTH);

      /* Every noncanonical bit is outside this evidence-backed subset. */
      for (unsigned bit = 0; bit < 32; ++bit) {
         uint8_t malformed[AGX3_END_LENGTH];
         memcpy(malformed, expected, sizeof(malformed));
         malformed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!agx3_unpack_end(encodings[e], malformed, sizeof(malformed),
                               &length));
      }
   }

   struct agx3_encoded_end packed;
   uint8_t length;
   CHECK(!agx3_pack_end(AGX_ISA_ENCODING_AGX2, &packed));
   CHECK(!agx3_pack_end(AGX_ISA_ENCODING_UNKNOWN, &packed));
   CHECK(!agx3_pack_end(AGX_ISA_ENCODING_AGX3B, NULL));
   CHECK(!agx3_unpack_end(AGX_ISA_ENCODING_AGX2, expected, sizeof(expected),
                          &length));
   CHECK(!agx3_unpack_end(AGX_ISA_ENCODING_AGX3C, expected,
                          AGX3_END_LENGTH - 1, &length));
   CHECK(!agx3_unpack_end(AGX_ISA_ENCODING_AGX3C, NULL, sizeof(expected),
                          &length));
   CHECK(!agx3_unpack_end(AGX_ISA_ENCODING_AGX3C, expected, sizeof(expected),
                          NULL));
}

static void
test_end_with_wait_fence_mask(void)
{
   static const struct {
      enum agx_isa_encoding encoding;
      uint8_t mask;
      uint8_t expected[AGX3_END_LENGTH];
   } witnesses[] = {
      {AGX_ISA_ENCODING_AGX3B, 0x00, {0x0e, 0x00, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3B, 0x01, {0x0e, 0x04, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3B, 0x20, {0x0e, 0x80, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3B, 0x3f, {0x0e, 0xfc, 0x00, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 0x40, {0x0e, 0x00, 0x02, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 0x80, {0x0e, 0x00, 0x04, 0x00}},
      {AGX_ISA_ENCODING_AGX3C, 0xff, {0x0e, 0xfc, 0x06, 0x00}},
   };

   for (unsigned v = 0; v < sizeof(witnesses) / sizeof(witnesses[0]); ++v) {
      struct agx3_encoded_end packed;
      uint8_t mask = 0, length = 0;
      CHECK(agx3_pack_end_with_wait_fence_mask(
         witnesses[v].encoding, witnesses[v].mask, &packed));
      CHECK(packed.length == AGX3_END_LENGTH);
      CHECK(memcmp(packed.bytes, witnesses[v].expected,
                   AGX3_END_LENGTH) == 0);
      CHECK(agx3_unpack_end_with_wait_fence_mask(
         witnesses[v].encoding, witnesses[v].expected,
         AGX3_END_LENGTH, &mask, &length));
      CHECK(mask == witnesses[v].mask);
      CHECK(length == AGX3_END_LENGTH);
   }

   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned e = 0; e < 2; ++e) {
      const unsigned mask_count = e == 0 ? 64 : 256;
      const uint32_t mutable_bits =
         e == 0 ? 0x0000fc00u : 0x0006fc00u;

      for (unsigned expected_mask = 0; expected_mask < mask_count;
           ++expected_mask) {
         struct agx3_encoded_end packed;
         CHECK(agx3_pack_end_with_wait_fence_mask(
            encodings[e], expected_mask, &packed));
         CHECK(packed.length == AGX3_END_LENGTH);
         CHECK(packed.bytes[0] == 0x0e);
         CHECK(packed.bytes[1] == ((expected_mask & 0x3f) << 2));
         CHECK(packed.bytes[2] == (((expected_mask >> 6) & 0x03) << 1));
         CHECK(packed.bytes[3] == 0x00);

         uint8_t decoded_mask = 0, length = 0;
         CHECK(agx3_unpack_end_with_wait_fence_mask(
            encodings[e], packed.bytes, packed.length,
            &decoded_mask, &length));
         CHECK(decoded_mask == expected_mask);
         CHECK(length == AGX3_END_LENGTH);

         uint8_t appended[AGX3_END_LENGTH + 2];
         memcpy(appended, packed.bytes, AGX3_END_LENGTH);
         appended[AGX3_END_LENGTH] = 0xff;
         appended[AGX3_END_LENGTH + 1] = 0xa5;
         CHECK(agx3_unpack_end_with_wait_fence_mask(
            encodings[e], appended, sizeof(appended),
            &decoded_mask, &length));
         CHECK(decoded_mask == expected_mask);
         CHECK(length == AGX3_END_LENGTH);

         /* Modeled dependency bits move to another canonical mask.  Every
          * other single-bit corruption must fail closed. */
         for (unsigned bit = 0; bit < AGX3_END_LENGTH * 8; ++bit) {
            uint8_t mutated[AGX3_END_LENGTH];
            memcpy(mutated, packed.bytes, sizeof(mutated));
            mutated[bit / 8] ^= 1u << (bit % 8);

            const bool modeled = mutable_bits & (1u << bit);
            const bool accepted = agx3_unpack_end_with_wait_fence_mask(
               encodings[e], mutated, sizeof(mutated),
               &decoded_mask, &length);
            CHECK(accepted == modeled);
            if (modeled) {
               const unsigned slot = bit <= 15 ? bit - 10 : bit - 11;
               CHECK(decoded_mask == (expected_mask ^ (1u << slot)));
               CHECK(length == AGX3_END_LENGTH);
            }
         }
      }
   }

   struct agx3_encoded_end packed;
   uint8_t mask = 0, length = 0;
   CHECK(!agx3_pack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3B, 0x40, &packed));
   CHECK(!agx3_pack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3B, 0x80, &packed));
   CHECK(!agx3_pack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3B, 0xc0, &packed));
   CHECK(!agx3_pack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX2, 0, &packed));
   CHECK(!agx3_pack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_UNKNOWN, 0, &packed));
   CHECK(!agx3_pack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3C, 0, NULL));

   static const uint8_t high_slots[] = {0x0e, 0x00, 0x06, 0x00};
   CHECK(!agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3B, high_slots, sizeof(high_slots),
      &mask, &length));
   CHECK(agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3C, high_slots, sizeof(high_slots),
      &mask, &length));
   CHECK(mask == 0xc0);

   for (size_t available = 0; available < AGX3_END_LENGTH; ++available) {
      CHECK(!agx3_unpack_end_with_wait_fence_mask(
         AGX_ISA_ENCODING_AGX3C, high_slots, available,
         &mask, &length));
   }

   CHECK(!agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX2, high_slots, sizeof(high_slots),
      &mask, &length));
   CHECK(!agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_UNKNOWN, high_slots, sizeof(high_slots),
      &mask, &length));
   CHECK(!agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3C, NULL, sizeof(high_slots),
      &mask, &length));
   CHECK(!agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3C, high_slots, sizeof(high_slots),
      NULL, &length));
   CHECK(!agx3_unpack_end_with_wait_fence_mask(
      AGX_ISA_ENCODING_AGX3C, high_slots, sizeof(high_slots),
      &mask, NULL));
}

static void
test_update_node(void)
{
   static const uint8_t expected_b[] = {
      0x07, 0x04, 0x54, 0x08, 0x09, 0x00, 0xdf, 0x05, 0x54, 0x00,
   };
   static const uint8_t expected_c[] = {
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00, 0x27,
      0x00, 0x01, 0x00, 0x22, 0x00, 0xad, 0x02,
   };
   const struct {
      enum agx_isa_encoding encoding;
      const uint8_t *expected;
      size_t length;
   } vectors[] = {
      {AGX_ISA_ENCODING_AGX3B, expected_b, sizeof(expected_b)},
      {AGX_ISA_ENCODING_AGX3C, expected_c, sizeof(expected_c)},
   };

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      struct agx3_encoded_update_node packed;
      CHECK(agx3_pack_update_node(vectors[v].encoding, &packed));
      CHECK(packed.length == vectors[v].length);
      CHECK(memcmp(packed.bytes, vectors[v].expected, vectors[v].length) == 0);

      uint8_t length = 0;
      CHECK(agx3_unpack_update_node(vectors[v].encoding,
                                    vectors[v].expected, vectors[v].length,
                                    &length));
      CHECK(length == vectors[v].length);

      /* No unknown bit or future operand-bearing form is accepted. */
      for (unsigned bit = 0; bit < vectors[v].length * 8; ++bit) {
         uint8_t malformed[AGX3_UPDATE_NODE_MAX_LENGTH] = {0};
         memcpy(malformed, vectors[v].expected, vectors[v].length);
         malformed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!agx3_unpack_update_node(vectors[v].encoding, malformed,
                                        vectors[v].length, &length));
      }
   }

   struct agx3_encoded_update_node packed;
   uint8_t length = 0;
   CHECK(!agx3_pack_update_node(AGX_ISA_ENCODING_AGX2, &packed));
   CHECK(!agx3_pack_update_node(AGX_ISA_ENCODING_UNKNOWN, &packed));
   CHECK(!agx3_pack_update_node(AGX_ISA_ENCODING_AGX3C, NULL));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX3B, expected_b,
                                  sizeof(expected_b) - 1, &length));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX3C, expected_c,
                                  sizeof(expected_c) - 1, &length));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX2, expected_c,
                                  sizeof(expected_c), &length));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX3B, expected_c,
                                  sizeof(expected_c), &length));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX3C, expected_b,
                                  sizeof(expected_b), &length));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX3C, NULL,
                                  sizeof(expected_c), &length));
   CHECK(!agx3_unpack_update_node(AGX_ISA_ENCODING_AGX3C, expected_c,
                                  sizeof(expected_c), NULL));
}

static void
test_continue_ray_fixed_first_site_witness(void)
{
   static const struct {
      enum agx_isa_encoding encoding;
      uint8_t expected[AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
      uint8_t length;
   } vectors[] = {
      {AGX_ISA_ENCODING_AGX3B,
       {0x5f, 0x11, 0x54, 0x00, 0x00, 0x00},
       6},
      {AGX_ISA_ENCODING_AGX3C,
       {0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x02},
       8},
   };
   struct agx3_encoded_continue_ray_fixed_first_site_witness packed;
   uint8_t length = 0;

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      CHECK(agx3_pack_continue_ray_fixed_first_site_witness(
         vectors[v].encoding, &packed));
      CHECK(packed.length == vectors[v].length);
      CHECK(memcmp(packed.bytes, vectors[v].expected, vectors[v].length) ==
            0);
      CHECK(agx3_unpack_continue_ray_fixed_first_site_witness(
         vectors[v].encoding, vectors[v].expected, vectors[v].length,
         &length));
      CHECK(length == vectors[v].length);

      /* Exact byte acceptance rejects every one-bit corruption, including
       * compiler allocation/finality variants whose meanings are unmodeled. */
      for (unsigned bit = 0; bit < vectors[v].length * 8; ++bit) {
         uint8_t malformed[
            AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
         memcpy(malformed, vectors[v].expected, vectors[v].length);
         malformed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
            vectors[v].encoding, malformed, vectors[v].length, &length));
      }

      for (size_t available = 0; available < vectors[v].length; ++available) {
         CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
            vectors[v].encoding, vectors[v].expected, available, &length));
      }

      /* Decode consumes its exact prefix and leaves following instructions to
       * the caller, matching the other bounded packers. */
      uint8_t appended[
         AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH + 1];
      memcpy(appended, vectors[v].expected, vectors[v].length);
      appended[vectors[v].length] = 0xff;
      CHECK(agx3_unpack_continue_ray_fixed_first_site_witness(
         vectors[v].encoding, appended, vectors[v].length + 1, &length));
      CHECK(length == vectors[v].length);

      CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
         vectors[v ^ 1].encoding, vectors[v].expected,
         sizeof(vectors[v].expected), &length));
   }

   /* One source intrinsic produces two hardware instructions.  The second is
    * intentionally outside this first-site-only API. */
   static const uint8_t second_site_b[] = {
      0x5f, 0x11, 0x54, 0x02, 0x00, 0x00,
   };
   static const uint8_t second_site_c[] = {
      0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x42,
   };
   CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX3B, second_site_b, sizeof(second_site_b), &length));
   CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX3C, second_site_c, sizeof(second_site_c), &length));

   CHECK(!agx3_pack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX2, &packed));
   CHECK(!agx3_pack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_UNKNOWN, &packed));
   CHECK(!agx3_pack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX3C, NULL));
   CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX2, vectors[1].expected, vectors[1].length, &length));
   CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_UNKNOWN, vectors[1].expected, vectors[1].length,
      &length));
   CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX3C, NULL, vectors[1].length, &length));
   CHECK(!agx3_unpack_continue_ray_fixed_first_site_witness(
      AGX_ISA_ENCODING_AGX3C, vectors[1].expected, vectors[1].length, NULL));
}

static void
test_update_bvh_fixed_witness(void)
{
   static const uint8_t expected[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH] = {
      /* barrier */
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00,
      /* update_bvh */
      0x27, 0x00, 0x21, 0x00, 0x22, 0x80, 0xac, 0x42,
   };
   struct agx3_encoded_update_bvh_fixed_witness packed;
   uint8_t length = 0;

   CHECK(agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX3C,
                                             &packed));
   CHECK(packed.length == sizeof(expected));
   CHECK(memcmp(packed.bytes, expected, sizeof(expected)) == 0);
   CHECK(agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_AGX3C, expected, sizeof(expected), &length));
   CHECK(length == sizeof(expected));

   /* Every bit in this fixed witness is canonical. */
   for (unsigned bit = 0; bit < sizeof(expected) * 8; ++bit) {
      uint8_t malformed[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH];
      memcpy(malformed, expected, sizeof(malformed));
      malformed[bit / 8] ^= 1u << (bit % 8);
      CHECK(!agx3_unpack_update_bvh_fixed_witness(
         AGX_ISA_ENCODING_AGX3C, malformed, sizeof(malformed), &length));
   }

   /* All 0..13 byte truncations reject. */
   for (size_t available = 0; available < sizeof(expected); ++available) {
      CHECK(!agx3_unpack_update_bvh_fixed_witness(
         AGX_ISA_ENCODING_AGX3C, expected, available, &length));
   }

   /* Like the other packers, decode consumes its exact prefix and reports the
    * expansion length without rejecting unrelated following bytes. */
   uint8_t appended[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH + 1];
   memcpy(appended, expected, sizeof(expected));
   appended[sizeof(expected)] = 0xff;
   length = 0;
   CHECK(agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_AGX3C, appended, sizeof(appended), &length));
   CHECK(length == sizeof(expected));

   CHECK(!agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX2,
                                              &packed));
   CHECK(!agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX3B,
                                              &packed));
   CHECK(!agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_UNKNOWN,
                                              &packed));
   CHECK(!agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX3C, NULL));
   CHECK(!agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_AGX2, expected, sizeof(expected), &length));
   CHECK(!agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_AGX3B, expected, sizeof(expected), &length));
   CHECK(!agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_UNKNOWN, expected, sizeof(expected), &length));
   CHECK(!agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_AGX3C, NULL, sizeof(expected), &length));
   CHECK(!agx3_unpack_update_bvh_fixed_witness(
      AGX_ISA_ENCODING_AGX3C, expected, sizeof(expected), NULL));
}

static void
test_enable_all_threads(void)
{
   static const uint8_t expected_b[] = {
      0x6f, 0x23, 0x54, 0x00, 0x00, 0x12,
   };
   static const uint8_t expected_c[] = {
      0x07, 0x07, 0x05, 0x44, 0x03, 0x00, 0x0b, 0x00,
   };
   static const uint8_t first_b[] = {
      0x6f, 0x03, 0x54, 0x00, 0x00, 0x12,
   };
   static const uint8_t first_c[] = {
      0x07, 0x07, 0x05, 0x04, 0x03, 0x00, 0x0b, 0x00,
   };
   const struct {
      enum agx_isa_encoding encoding;
      const uint8_t *expected;
      size_t length;
   } vectors[] = {
      {AGX_ISA_ENCODING_AGX3B, expected_b, sizeof(expected_b)},
      {AGX_ISA_ENCODING_AGX3C, expected_c, sizeof(expected_c)},
   };

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      struct agx3_encoded_enable_all_threads packed;
      CHECK(agx3_pack_enable_all_threads(vectors[v].encoding, &packed));
      CHECK(packed.length == vectors[v].length);
      CHECK(memcmp(packed.bytes, vectors[v].expected, vectors[v].length) == 0);

      uint8_t length = 0;
      CHECK(agx3_unpack_enable_all_threads(
         vectors[v].encoding, vectors[v].expected, vectors[v].length,
         &length));
      CHECK(length == vectors[v].length);

      /* Every bit outside the exact evidence-backed form fails closed. */
      for (unsigned bit = 0; bit < vectors[v].length * 8; ++bit) {
         uint8_t malformed[AGX3_ENABLE_ALL_THREADS_MAX_LENGTH] = {0};
         memcpy(malformed, vectors[v].expected, vectors[v].length);
         malformed[bit / 8] ^= 1u << (bit % 8);
         CHECK(!agx3_unpack_enable_all_threads(
            vectors[v].encoding, malformed, vectors[v].length, &length));
      }
   }

   struct agx3_encoded_enable_all_threads packed;
   uint8_t length = 0;
   CHECK(!agx3_pack_enable_all_threads(AGX_ISA_ENCODING_AGX2, &packed));
   CHECK(!agx3_pack_enable_all_threads(AGX_ISA_ENCODING_UNKNOWN, &packed));
   CHECK(!agx3_pack_enable_all_threads(AGX_ISA_ENCODING_AGX3B, NULL));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3B, expected_b,
                                         sizeof(expected_b) - 1, &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3C, expected_c,
                                         sizeof(expected_c) - 1, &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX2, expected_b,
                                         sizeof(expected_b), &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3B, expected_c,
                                         sizeof(expected_c), &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3C, expected_b,
                                         sizeof(expected_b), &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3B, first_b,
                                         sizeof(first_b), &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3C, first_c,
                                         sizeof(first_c), &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3C, NULL,
                                         sizeof(expected_c), &length));
   CHECK(!agx3_unpack_enable_all_threads(AGX_ISA_ENCODING_AGX3C, expected_c,
                                         sizeof(expected_c), NULL));
}

int
main(void)
{
   test_vectors();
   test_boundaries_and_rejection();
   test_iadd_u32_imm8();
   test_iadd_u32_imm8_rejection();
   test_iadd_u32_reg_reg();
   test_iadd_u32_reg_reg_rejection();
   test_fadd_f32_reg_reg();
   test_fadd_f32_reg_reg_rejection();
   test_fmul_f32_reg_reg();
   test_fmul_f32_reg_reg_rejection();
   test_fmul_f32_reg_reg_long();
   test_fmul_f32_reg_reg_long_rejection();
   test_fma_f32_reg_reg_reg();
   test_fma_f32_reg_reg_reg_rejection();
   test_fmin_f32_reg_reg();
   test_fmin_f32_reg_reg_rejection();
   test_fmax_f32_reg_reg();
   test_fmax_f32_reg_reg_rejection();
   test_i32_to_f32_reg();
   test_i32_to_f32_reg_rejection();
   test_bit_and_u32_reg_reg();
   test_bit_and_u32_reg_reg_rejection();
   test_bit_or_u32_reg_reg();
   test_bit_or_u32_reg_reg_rejection();
   test_bit_xor_u32_reg_reg();
   test_bit_xor_u32_reg_reg_rejection();
   test_bit_not_u32_reg();
   test_bit_not_u32_reg_rejection();
   test_isub_u32_reg_reg();
   test_isub_u32_reg_reg_rejection();
   test_sll_u32_reg_reg();
   test_sll_u32_reg_reg_rejection();
   test_asr_i32_reg_reg();
   test_asr_i32_reg_reg_rejection();
   test_lsr_u32_reg_reg();
   test_lsr_u32_reg_reg_rejection();
   test_imul_u32_reg_reg();
   test_imul_u32_reg_reg_rejection();
   test_imadd_u32_reg_reg_reg();
   test_imadd_u32_reg_reg_reg_rejection();
   test_yield_i16_reg();
   test_yield_i16_reg_rejection();
   test_nop();
   test_threadgroup_barrier();
   test_end();
   test_end_with_wait_fence_mask();
   test_enable_all_threads();
   test_update_node();
   test_continue_ray_fixed_first_site_witness();
   test_update_bvh_fixed_witness();

   printf("agx3 proven-subset pack: %u checks, %u failures\n", checks,
          failures);
   return failures != 0;
}
