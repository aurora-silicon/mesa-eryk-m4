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
   test_nop();
   test_threadgroup_barrier();
   test_end();
   test_enable_all_threads();
   test_update_node();

   printf("agx3 proven-subset pack: %u checks, %u failures\n", checks,
          failures);
   return failures != 0;
}
