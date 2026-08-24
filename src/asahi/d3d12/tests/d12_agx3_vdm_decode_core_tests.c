/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_agx3_vdm_decode_core.h"

#include "asahi/lib/agx_vdm_ppp_state_core.h"
#include "asahi/lib/agx_vdm_stream_link_core.h"

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

static const enum agx_chip targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static bool
record_is_cleared(const struct agx_vdm_decoded_record *record)
{
   const uint8_t *bytes = (const uint8_t *)record;
   for (size_t i = 0; i < sizeof(*record); ++i) {
      if (bytes[i] != 0)
         return false;
   }
   return true;
}

static int
test_all_record_kinds(void)
{
   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const enum agx_chip chip = targets[t];
      struct agx_vdm_decoded_record record;

      struct agx_vdm_ppp_state_core ppp;
      CHECK(agx_vdm_ppp_state_core_pack(chip, UINT64_C(0xef12345000) + t * 4,
                                        0x40 + t, &ppp));
      CHECK(d12_agx3_decode_vdm_record(chip, ppp.bytes, ppp.length, &record));
      CHECK(record.kind == AGX_VDM_RECORD_PPP_STATE_UPDATE);
      CHECK(record.length == ppp.length);
      CHECK(record.data.ppp_state_update.pointer ==
            UINT64_C(0xef12345000) + t * 4);
      CHECK(record.data.ppp_state_update.size_words == 0x40 + t);

      const struct agx_vdm_state_update_info state_info = {
         .raw_vertex_shader_word0 = UINT32_C(0x11223344) + t,
         .pipeline = UINT64_C(0xab12345680) + t * 64,
         .raw_pipeline_template = UINT32_C(0x00123456) ^ t,
         .raw_vertex_outputs = {UINT32_C(0x55667788) + t,
                                UINT32_C(0x99aabbcc) - t},
         .raw_vertex_unknown = UINT32_C(0xddeeff00) ^ t,
      };
      struct agx_vdm_state_update state;
      CHECK(agx_vdm_state_update_pack(chip, &state_info, &state));
      CHECK(
         d12_agx3_decode_vdm_record(chip, state.bytes, state.length, &record));
      CHECK(record.kind == AGX_VDM_RECORD_STATE_UPDATE);
      CHECK(record.length == state.length);
      CHECK(record.data.state_update.pipeline == state_info.pipeline);
      CHECK(record.data.state_update.raw_pipeline_template ==
            state_info.raw_pipeline_template);

      const struct agx_vdm_index_list_direct_nonindexed_info nonindexed_info = {
         .primitive = 13,
         .count = UINT32_C(0x12345678) + t,
         .instances = 2 + t,
         .start = UINT32_C(0x23456789) ^ t,
      };
      struct agx_vdm_index_list_direct_nonindexed nonindexed;
      CHECK(agx_vdm_index_list_direct_nonindexed_pack(chip, &nonindexed_info,
                                                      &nonindexed));
      CHECK(d12_agx3_decode_vdm_record(chip, nonindexed.bytes,
                                       nonindexed.length, &record));
      CHECK(record.kind == AGX_VDM_RECORD_INDEX_LIST_NONINDEXED);
      CHECK(record.length == nonindexed.length);
      CHECK(record.data.index_list_nonindexed.primitive ==
            nonindexed_info.primitive);
      CHECK(record.data.index_list_nonindexed.count == nonindexed_info.count);

      const struct agx_vdm_index_list_direct_indexed_info indexed_info = {
         .primitive = 15,
         .restart_enable = true,
         .index_size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U32,
         .index_buffer = UINT64_C(0xab1234567890) + t * 4,
         .count = UINT32_C(0x3456789a) + t,
         .instances = 3 + t,
         .start = UINT32_C(0x456789ab) ^ t,
         .index_buffer_size_word = UINT32_C(0x56789abc) + t,
      };
      struct agx_vdm_index_list_direct_indexed indexed;
      CHECK(
         agx_vdm_index_list_direct_indexed_pack(chip, &indexed_info, &indexed));
      CHECK(d12_agx3_decode_vdm_record(chip, indexed.bytes, indexed.length,
                                       &record));
      CHECK(record.kind == AGX_VDM_RECORD_INDEX_LIST_INDEXED);
      CHECK(record.length == indexed.length);
      CHECK(record.data.index_list_indexed.index_buffer ==
            indexed_info.index_buffer);
      CHECK(record.data.index_list_indexed.index_size ==
            indexed_info.index_size);
      CHECK(record.data.index_list_indexed.restart_enable);

      struct agx_vdm_stream_link_core link;
      CHECK(agx_vdm_stream_link_core_pack(chip, UINT64_C(0xcd12345678) + t,
                                          &link));
      uint8_t larger[AGX_VDM_STREAM_LINK_CORE_LENGTH + 4];
      memcpy(larger, link.bytes, link.length);
      memset(larger + link.length, 0xa5, 4);
      CHECK(d12_agx3_decode_vdm_record(chip, larger, sizeof(larger), &record));
      CHECK(record.kind == AGX_VDM_RECORD_STREAM_LINK);
      CHECK(record.length == link.length);
      CHECK(record.chip == chip);
      CHECK(record.data.stream_link.target == UINT64_C(0xcd12345678) + t);
   }

   return 0;
}

static int
test_rejection(void)
{
   struct agx_vdm_ppp_state_core ppp;
   CHECK(agx_vdm_ppp_state_core_pack(AGX_CHIP_G17S, UINT64_C(0x123456780), 8,
                                     &ppp));

   for (uint32_t available = 0; available < ppp.length; ++available) {
      struct agx_vdm_decoded_record record;
      memset(&record, 0xff, sizeof(record));
      CHECK(!d12_agx3_decode_vdm_record(AGX_CHIP_G17S, ppp.bytes, available,
                                        &record));
      CHECK(record_is_cleared(&record));
   }

   for (enum agx_chip chip = AGX_CHIP_G13G; chip <= AGX_CHIP_G17C; ++chip) {
      bool supported = false;
      for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t)
         supported |= chip == targets[t];
      if (supported)
         continue;

      struct agx_vdm_decoded_record record;
      memset(&record, 0xff, sizeof(record));
      CHECK(!d12_agx3_decode_vdm_record(chip, ppp.bytes, ppp.length, &record));
      CHECK(record_is_cleared(&record));
   }

   const uint8_t unknown[4] = {0, 0, 0, 0xe0};
   struct agx_vdm_decoded_record record;
   memset(&record, 0xff, sizeof(record));
   CHECK(!d12_agx3_decode_vdm_record(AGX_CHIP_G17S, unknown, sizeof(unknown),
                                     &record));
   CHECK(record_is_cleared(&record));
   CHECK(
      !d12_agx3_decode_vdm_record(AGX_CHIP_G17S, ppp.bytes, ppp.length, NULL));
   return 0;
}

int
main(void)
{
   CHECK(test_all_record_kinds() == 0);
   CHECK(test_rejection() == 0);

   printf("d12 AGX3 VDM decode: %u checks passed\n", checks);
   return 0;
}
