/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_decode.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(condition)                                                       \
   do {                                                                        \
      ++checks;                                                                \
      if (!(condition)) {                                                      \
         fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,    \
                 #condition);                                                  \
         return 1;                                                             \
      }                                                                        \
   } while (0)

struct witnessed_record {
   enum agx_chip chip;
   enum agx_ioto_kind kind;
   uint8_t length;
   uint8_t bytes[AGX_IOTO_MAX_RECORD_SIZE];
};

static const struct witnessed_record witnesses[] = {
   {AGX_CHIP_G15G, AGX_IOTO_KIND_B_EP0, 10,
    {0x0f, 0x00, 0x54, 0x10, 0xfd, 0xff, 0xff, 0xff, 0xff, 0x00}},
   {AGX_CHIP_G15G, AGX_IOTO_KIND_RLD_EP1, 8,
    {0x2c, 0x8c, 0x09, 0x27, 0x60, 0x00, 0x00, 0x80}},
   {AGX_CHIP_G15G, AGX_IOTO_KIND_IADD_EP0, 10,
    {0x9f, 0x11, 0x54, 0x00, 0x02, 0x00, 0x08, 0xa8, 0x10, 0x05}},
   {AGX_CHIP_G17G, AGX_IOTO_KIND_B_EP1, 10,
    {0x5e, 0x0f, 0xf3, 0x1e, 0x1f, 0x00, 0xff, 0xc7, 0xff, 0x7f}},
   {AGX_CHIP_G17G, AGX_IOTO_KIND_LDDEV_EP5, 16,
    {0x4f, 0x08, 0x07, 0x10, 0x08, 0x00, 0xb8, 0x0a, 0x10, 0xc0, 0x41, 0x08,
     0x80, 0x00, 0x00, 0x00}},
   {AGX_CHIP_G17G, AGX_IOTO_KIND_IADD_EP0, 12,
    {0x27, 0x04, 0x04, 0x1a, 0x21, 0x00, 0xa3, 0x02, 0x88, 0x80, 0x00,
     0x00}},
};

static bool
record_is_cleared(const struct agx_ioto_record *record)
{
   const uint8_t *bytes = (const uint8_t *)record;
   for (size_t i = 0; i < sizeof(*record); ++i) {
      if (bytes[i] != 0)
         return false;
   }

   return true;
}

static int
test_witnesses(void)
{
   for (size_t i = 0; i < sizeof(witnesses) / sizeof(witnesses[0]); ++i) {
      const struct witnessed_record *witness = &witnesses[i];
      uint8_t stream[AGX_IOTO_MAX_RECORD_SIZE + 3];
      memset(stream, 0xa5, sizeof(stream));
      memcpy(stream, witness->bytes, witness->length);

      struct agx_ioto_record record;
      size_t consumed = 99;
      CHECK(hk_agx3_ioto_validate_record(
         witness->chip, witness->kind, stream, sizeof(stream), &record,
         &consumed));
      CHECK(record.kind == witness->kind);
      CHECK(record.length == witness->length);
      CHECK(consumed == witness->length);
      CHECK(memcmp(record.bytes, witness->bytes, witness->length) == 0);
      for (size_t j = witness->length; j < sizeof(record.bytes); ++j)
         CHECK(record.bytes[j] == 0);

      memset(&record, 0xff, sizeof(record));
      consumed = 99;
      CHECK(hk_agx3_ioto_decode_record(witness->chip, stream, sizeof(stream),
                                       &record, &consumed));
      CHECK(record.kind == witness->kind);
      CHECK(record.length == witness->length);
      CHECK(consumed == witness->length);
      CHECK(memcmp(record.bytes, witness->bytes, witness->length) == 0);

      for (size_t available = 0; available < witness->length; ++available) {
         memset(&record, 0xff, sizeof(record));
         consumed = 99;
         CHECK(!hk_agx3_ioto_validate_record(
            witness->chip, witness->kind, witness->bytes, available, &record,
            &consumed));
         CHECK(record_is_cleared(&record));
         CHECK(consumed == 0);

         memset(&record, 0xff, sizeof(record));
         consumed = 99;
         CHECK(!hk_agx3_ioto_decode_record(witness->chip, witness->bytes,
                                           available, &record, &consumed));
         CHECK(record_is_cleared(&record));
         CHECK(consumed == 0);
      }

      memset(&record, 0xff, sizeof(record));
      consumed = 99;
      CHECK(!hk_agx3_ioto_validate_record(
         witness->chip, AGX_IOTO_KIND_NOP_EP0, witness->bytes,
         witness->length, &record, &consumed));
      CHECK(record_is_cleared(&record));
      CHECK(consumed == 0);
   }

   return 0;
}

static int
test_unsupported_and_invalid_outputs(void)
{
   static const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   static const uint8_t nop[] = {0x06, 0x00};

   for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      struct agx_ioto_record record;
      memset(&record, 0xff, sizeof(record));
      size_t consumed = 99;
      CHECK(!hk_agx3_ioto_decode_record(rejected[i], nop, sizeof(nop),
                                        &record, &consumed));
      CHECK(record_is_cleared(&record));
      CHECK(consumed == 0);

      memset(&record, 0xff, sizeof(record));
      consumed = 99;
      CHECK(!hk_agx3_ioto_validate_record(
         rejected[i], AGX_IOTO_KIND_NOP_EP0, nop, sizeof(nop), &record,
         &consumed));
      CHECK(record_is_cleared(&record));
      CHECK(consumed == 0);
   }

   struct agx_ioto_record record;
   memset(&record, 0xff, sizeof(record));
   size_t consumed = 99;
   CHECK(!hk_agx3_ioto_decode_record(AGX_CHIP_G15G, NULL, sizeof(nop),
                                     &record, &consumed));
   CHECK(record_is_cleared(&record));
   CHECK(consumed == 0);

   memset(&record, 0xff, sizeof(record));
   consumed = 99;
   CHECK(!hk_agx3_ioto_validate_record(
      AGX_CHIP_G15G, AGX_IOTO_KIND_INVALID, nop, sizeof(nop), &record,
      &consumed));
   CHECK(record_is_cleared(&record));
   CHECK(consumed == 0);

   consumed = 99;
   CHECK(!hk_agx3_ioto_decode_record(AGX_CHIP_G15G, nop, sizeof(nop), NULL,
                                     &consumed));
   CHECK(consumed == 0);

   memset(&record, 0xff, sizeof(record));
   CHECK(!hk_agx3_ioto_decode_record(AGX_CHIP_G15G, nop, sizeof(nop), &record,
                                     NULL));
   CHECK(record_is_cleared(&record));

   return 0;
}

int
main(void)
{
   if (test_witnesses() || test_unsupported_and_invalid_outputs())
      return 1;

   printf("hk AGX3 Ioto decode: %u checks passed\n", checks);
   return 0;
}
