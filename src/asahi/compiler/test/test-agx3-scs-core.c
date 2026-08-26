/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Controlled address-space(5) witnesses from Apple metal-nt targeting G17P
 * (AGX3b) and G17S (AGX3c).
 */

#include "agx3_scs_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);          \
         ++failures;                                                           \
      }                                                                        \
   } while (0)

struct scs_vec {
   enum agx_isa_encoding enc;
   bool load;
   struct agx3_scs_desc d;
   uint8_t bytes[AGX3_SCS_MAX_LENGTH];
   uint8_t length;
};

static const struct scs_vec vectors[] = {
   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 1, .width = AGX3_MEM_W8, .slot = 6},
    {0x5f, 0x02, 0x54, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0x0a, 0x00,
     0x00, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 12, .width = AGX3_MEM_W16, .byte_offset = 4, .slot = 6},
    {0x5f, 0x02, 0x54, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x02,
     0x00, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 2, .width = AGX3_MEM_W32, .byte_offset = 8, .slot = 1},
    {0x5f, 0x02, 0x54, 0x02, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x04,
     0x00, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false, {.width = AGX3_MEM_W8},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x00, 0x00,
     0x40, 0x00, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false,
    {.width = AGX3_MEM_W16, .byte_offset = 4},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x80, 0x00,
     0x40, 0x00, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false,
    {.width = AGX3_MEM_W32, .byte_offset = 8},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x01,
     0x40, 0x00, 0x00}, 14},

   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 1, .width = AGX3_MEM_W8, .slot = 8},
    {0x27, 0x00, 0x01, 0x09, 0x32, 0x07, 0xa8, 0x02, 0x00, 0x00, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x00}, 16},
   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 12, .width = AGX3_MEM_W16, .byte_offset = 4, .slot = 8},
    {0x27, 0x00, 0x01, 0x00, 0x32, 0x07, 0xa8, 0x1a, 0x00, 0x40, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x00}, 16},
   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 2, .width = AGX3_MEM_W32, .byte_offset = 8, .slot = 1},
    {0x37, 0x00, 0x01, 0x02, 0x32, 0x00, 0xa8, 0x02, 0x00, 0x80, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x00}, 16},
   {AGX_ISA_ENCODING_AGX3C, false, {.width = AGX3_MEM_W8},
    {0x27, 0x00, 0x01, 0x08, 0x32, 0x00, 0xa9, 0x02, 0x00, 0x00, 0x00,
     0x00, 0x02, 0x84}, 14},
   {AGX_ISA_ENCODING_AGX3C, false,
    {.width = AGX3_MEM_W16, .byte_offset = 4},
    {0x27, 0x00, 0x01, 0x00, 0x32, 0x00, 0xa9, 0x02, 0x00, 0x40, 0x00,
     0x00, 0x02, 0x84}, 14},
   {AGX_ISA_ENCODING_AGX3C, false,
    {.data = 2, .width = AGX3_MEM_W32, .byte_offset = 8},
    {0x37, 0x00, 0x01, 0x02, 0x32, 0x00, 0xa9, 0x02, 0x00, 0x80, 0x00,
     0x00, 0x02, 0x84}, 14},

   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 6, .width = AGX3_MEM_W32, .slot = 6},
    {0x5f, 0x02, 0x54, 0x06, 0x00, 0x00, 0x00, 0x00, 0x40, 0x0a, 0x00,
     0x00, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 8, .width = AGX3_MEM_W32, .byte_offset = 4, .slot = 6},
    {0x5f, 0x02, 0x54, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40, 0x0a, 0x02,
     0x00, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 4, .width = AGX3_MEM_W32, .byte_offset = 16, .slot = 1},
    {0x5f, 0x02, 0x54, 0x04, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x08,
     0x00, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, true,
    {.data = 2, .width = AGX3_MEM_W32, .byte_offset = 0x6000, .slot = 2},
    {0x5f, 0x02, 0x54, 0x02, 0x00, 0x00, 0x00, 0x00, 0x40, 0x02, 0x00,
     0x30, 0x02, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false, {.width = AGX3_MEM_W32},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00,
     0x40, 0x00, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false,
    {.width = AGX3_MEM_W32, .byte_offset = 4},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x80, 0x00,
     0x40, 0x00, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false,
    {.width = AGX3_MEM_W32, .byte_offset = 16},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x02,
     0x40, 0x00, 0x00}, 14},
   {AGX_ISA_ENCODING_AGX3B, false,
    {.width = AGX3_MEM_W32, .byte_offset = 0x6000},
    {0xdf, 0x02, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00,
     0x4c, 0x00, 0x00}, 14},

   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 6, .width = AGX3_MEM_W32, .slot = 8},
    {0x37, 0x00, 0x01, 0x02, 0x32, 0x07, 0xa8, 0x0a, 0x00, 0x00, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x00}, 16},
   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 8, .width = AGX3_MEM_W32, .byte_offset = 4, .slot = 8},
    {0x27, 0x00, 0x01, 0x02, 0x32, 0x07, 0xa8, 0x12, 0x00, 0x40, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x00}, 16},
   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 4, .width = AGX3_MEM_W32, .byte_offset = 16, .slot = 1},
    {0x27, 0x00, 0x01, 0x02, 0x32, 0x00, 0xa8, 0x0a, 0x10, 0x00, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x00}, 16},
   {AGX_ISA_ENCODING_AGX3C, true,
    {.data = 2, .width = AGX3_MEM_W32, .byte_offset = 0x6000, .slot = 2,
     .c_load_marker = true},
    {0x37, 0x00, 0x01, 0x22, 0x12, 0x01, 0xa8, 0x02, 0x00, 0x00, 0x00,
     0x00, 0x02, 0x84, 0x00, 0x20}, 16},
   {AGX_ISA_ENCODING_AGX3C, false, {.width = AGX3_MEM_W32},
    {0x27, 0x00, 0x01, 0x02, 0x32, 0x00, 0xa9, 0x02, 0x00, 0x00, 0x00,
     0x00, 0x02, 0x84}, 14},
   {AGX_ISA_ENCODING_AGX3C, false,
    {.width = AGX3_MEM_W32, .byte_offset = 4},
    {0x27, 0x00, 0x01, 0x02, 0x32, 0x00, 0xa9, 0x02, 0x00, 0x40, 0x00,
     0x00, 0x02, 0x84}, 14},
   {AGX_ISA_ENCODING_AGX3C, false,
    {.width = AGX3_MEM_W32, .byte_offset = 16},
    {0x27, 0x00, 0x01, 0x02, 0x32, 0x00, 0xa9, 0x02, 0x10, 0x00, 0x00,
     0x00, 0x02, 0x84}, 14},
   {AGX_ISA_ENCODING_AGX3C, false,
    {.width = AGX3_MEM_W32, .byte_offset = 0x6000, .value_kill = true},
    {0x27, 0x00, 0x01, 0x22, 0x32, 0x04, 0xa9, 0x02, 0x00, 0x00, 0x01,
     0x00, 0x02, 0x84}, 14},
};

static bool
descs_equal(const struct agx3_scs_desc *a, const struct agx3_scs_desc *b)
{
   return a->data == b->data && a->width == b->width &&
          a->byte_offset == b->byte_offset && a->slot == b->slot &&
          a->wait_mask == b->wait_mask && a->value_kill == b->value_kill &&
          a->c_load_marker == b->c_load_marker;
}

static void
test_vectors(void)
{
   for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
      const struct scs_vec *v = &vectors[i];
      uint8_t encoded[AGX3_SCS_MAX_LENGTH];
      uint8_t length = 0;
      const bool ok =
         v->load ? agx3_scs_encode_load(v->enc, &v->d, encoded, &length)
                 : agx3_scs_encode_store(v->enc, &v->d, encoded, &length);
      CHECK(ok);
      CHECK(length == v->length);
      CHECK(memcmp(encoded, v->bytes, v->length) == 0);
      for (unsigned j = v->length; j < sizeof(encoded); ++j)
         CHECK(encoded[j] == 0);

      struct agx3_scs_desc decoded;
      const bool decoded_ok =
         v->load ? agx3_scs_decode_load(v->enc, v->bytes, v->length, &decoded)
                 : agx3_scs_decode_store(v->enc, v->bytes, v->length,
                                         &decoded);
      CHECK(decoded_ok);
      CHECK(descs_equal(&decoded, &v->d));
   }
}

static void
test_synthetic_roundtrip(void)
{
   const struct agx3_scs_desc descs[] = {
      {.data = 10, .width = AGX3_MEM_W16, .byte_offset = -4, .slot = 3,
       .wait_mask = (1u << 0) | (1u << 5)},
      {.data = 18, .width = AGX3_MEM_W32, .byte_offset = -8,
       .wait_mask = (1u << 0) | (1u << 7), .value_kill = true},
   };
   const enum agx_isa_encoding encs[] = {AGX_ISA_ENCODING_AGX3B,
                                         AGX_ISA_ENCODING_AGX3C};
   const bool loads[] = {true, false};

   for (unsigned i = 0; i < 2; ++i) {
      uint8_t encoded[AGX3_SCS_MAX_LENGTH];
      uint8_t length = 0;
      CHECK(loads[i] ? agx3_scs_encode_load(encs[i], &descs[i], encoded,
                                            &length)
                     : agx3_scs_encode_store(encs[i], &descs[i], encoded,
                                             &length));
      struct agx3_scs_desc decoded;
      CHECK(loads[i] ? agx3_scs_decode_load(encs[i], encoded, length, &decoded)
                     : agx3_scs_decode_store(encs[i], encoded, length,
                                             &decoded));
      CHECK(descs_equal(&descs[i], &decoded));
   }
}

static void
test_fail_closed(void)
{
   uint8_t encoded[AGX3_SCS_MAX_LENGTH];
   uint8_t length = 0xff;
   struct agx3_scs_desc bad = {
      .data = 2, .width = AGX3_MEM_W32, .slot = 1,
   };

   memset(encoded, 0xcc, sizeof(encoded));
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX2, &bad, encoded,
                               &length));
   CHECK(length == 0);
   for (unsigned i = 0; i < sizeof(encoded); ++i)
      CHECK(encoded[i] == 0);

   bad.byte_offset = 2;
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX3B, &bad, encoded,
                               &length));
   bad.byte_offset = 0;
   bad.data = 3;
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX3B, &bad, encoded,
                               &length));
   bad.data = 2;
   bad.slot = 0;
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX3B, &bad, encoded,
                               &length));
   bad.slot = 1;
   bad.wait_mask = 1u << 6;
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX3B, &bad, encoded,
                               &length));
   bad.wait_mask = 0;
   bad.value_kill = true;
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX3C, &bad, encoded,
                               &length));
   bad.value_kill = false;
   bad.c_load_marker = true;
   CHECK(!agx3_scs_encode_load(AGX_ISA_ENCODING_AGX3B, &bad, encoded,
                               &length));

   static const uint8_t b_ld64[] = {
      0x5f, 0x02, 0x54, 0x04, 0x00, 0x00, 0x00, 0x00, 0x58, 0x02, 0x08,
      0x00, 0x02, 0x00,
   };
   static const uint8_t c_dynamic_ld[] = {
      0x2f, 0x00, 0x01, 0x8c, 0x32, 0xe7, 0xa8, 0x4a,
      0x32, 0x38, 0x00, 0x00, 0x01, 0x84, 0x00, 0x00,
   };
   struct agx3_scs_desc decoded = bad;
   CHECK(!agx3_scs_decode_load(AGX_ISA_ENCODING_AGX3B, b_ld64,
                               sizeof(b_ld64), &decoded));
   CHECK(!agx3_scs_decode_load(AGX_ISA_ENCODING_AGX3C, c_dynamic_ld,
                               sizeof(c_dynamic_ld), &decoded));
   CHECK(!agx3_scs_decode_load(AGX_ISA_ENCODING_AGX3C, vectors[6].bytes,
                               vectors[6].length - 1, &decoded));
   CHECK(!agx3_scs_decode_load(AGX_ISA_ENCODING_AGX3C, NULL,
                               vectors[6].length, &decoded));
   CHECK(!agx3_scs_decode_load(AGX_ISA_ENCODING_AGX3C, vectors[6].bytes,
                               vectors[6].length, NULL));

   uint8_t changed[AGX3_SCS_MAX_LENGTH];
   memcpy(changed, vectors[6].bytes, vectors[6].length);
   changed[0] ^= 1;
   CHECK(!agx3_scs_decode_load(AGX_ISA_ENCODING_AGX3C, changed,
                               vectors[6].length, &decoded));
}

int
main(void)
{
   test_vectors();
   test_synthetic_roundtrip();
   test_fail_closed();

   if (failures != 0) {
      fprintf(stderr, "agx3_scs_core: %u/%u checks failed\n", failures,
              checks);
      return 1;
   }

   printf("agx3_scs_core: %zu controlled Apple G17P/G17S vectors and all "
          "%u checks passed\n",
          sizeof(vectors) / sizeof(vectors[0]), checks);
   return 0;
}
