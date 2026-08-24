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
parse_hex(const char *hex, uint8_t out[AGX3_LDIMM_MAX_LENGTH])
{
   unsigned length = strlen(hex) / 2;
   CHECK(length <= AGX3_LDIMM_MAX_LENGTH);

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
         const unsigned expected_length = parse_hex(hex, expected);
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

int
main(void)
{
   test_vectors();
   test_boundaries_and_rejection();

   printf("agx3 ldimm pack: %u checks, %u failures\n", checks, failures);
   return failures != 0;
}
