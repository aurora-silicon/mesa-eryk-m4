/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "asahi/lib/agx_vdm_decode_core.h"
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

static const enum agx_chip supported_targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static const enum agx_chip unsupported_targets[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
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
check_rejected(enum agx_chip chip, const void *bytes, size_t available)
{
   struct agx_vdm_decoded_record record;
   memset(&record, 0xff, sizeof(record));
   CHECK(!agx_vdm_decode_one(chip, bytes, available, &record));
   CHECK(record_is_cleared(&record));
   return 0;
}

static int
test_ppp_state_update(void)
{
   for (unsigned t = 0;
        t < sizeof(supported_targets) / sizeof(supported_targets[0]); ++t) {
      const enum agx_chip chip = supported_targets[t];

      for (uint32_t size_words = 0; size_words <= 0xff; ++size_words) {
         const uint64_t pointer =
            ((UINT64_C(0x81) + t) << 32) | (UINT64_C(0x12345000) + t * 4);
         struct agx_vdm_ppp_state_core packed;
         CHECK(agx_vdm_ppp_state_core_pack(chip, pointer, size_words, &packed));

         uint8_t stream[AGX_VDM_PPP_STATE_CORE_LENGTH + 4];
         memcpy(stream, packed.bytes, packed.length);
         memset(stream + packed.length, 0xa5, 4);

         struct agx_vdm_decoded_record record;
         CHECK(agx_vdm_decode_one(chip, stream, sizeof(stream), &record));
         CHECK(record.kind == AGX_VDM_RECORD_PPP_STATE_UPDATE);
         CHECK(record.length == packed.length);
         CHECK(record.chip == chip);
         CHECK(record.data.ppp_state_update.pointer == pointer);
         CHECK(record.data.ppp_state_update.size_words == size_words);
      }
   }

   return 0;
}

static int
test_state_update(void)
{
   for (unsigned t = 0;
        t < sizeof(supported_targets) / sizeof(supported_targets[0]); ++t) {
      const enum agx_chip chip = supported_targets[t];
      const struct agx_vdm_state_update_info info = {
         .raw_vertex_shader_word0 = UINT32_C(0x10203040) + t,
         .pipeline = UINT64_C(0xa123456780) + t * 64,
         .raw_pipeline_template = UINT32_C(0x002abcde) ^ t,
         .raw_vertex_outputs = {UINT32_C(0x55667788) + t,
                                UINT32_C(0x99aabbcc) - t},
         .raw_vertex_unknown = UINT32_C(0xddeeff00) ^ t,
      };
      struct agx_vdm_state_update packed;
      CHECK(agx_vdm_state_update_pack(chip, &info, &packed));

      uint8_t stream[AGX_VDM_STATE_UPDATE_LENGTH + 8];
      memcpy(stream, packed.bytes, packed.length);
      memset(stream + packed.length, 0x5a, 8);

      struct agx_vdm_decoded_record record;
      CHECK(agx_vdm_decode_one(chip, stream, sizeof(stream), &record));
      CHECK(record.kind == AGX_VDM_RECORD_STATE_UPDATE);
      CHECK(record.length == packed.length);
      CHECK(record.chip == chip);
      CHECK(record.data.state_update.raw_vertex_shader_word0 ==
            info.raw_vertex_shader_word0);
      CHECK(record.data.state_update.pipeline == info.pipeline);
      CHECK(record.data.state_update.raw_pipeline_template ==
            info.raw_pipeline_template);
      CHECK(memcmp(record.data.state_update.raw_vertex_outputs,
                   info.raw_vertex_outputs,
                   sizeof(info.raw_vertex_outputs)) == 0);
      CHECK(record.data.state_update.raw_vertex_unknown ==
            info.raw_vertex_unknown);
   }

   return 0;
}

static int
test_index_lists(void)
{
   static const uint8_t primitives[] = {0, 1, 3, 5, 6, 9, 11, 13, 14, 15};

   for (unsigned t = 0;
        t < sizeof(supported_targets) / sizeof(supported_targets[0]); ++t) {
      const enum agx_chip chip = supported_targets[t];

      for (unsigned p = 0; p < sizeof(primitives); ++p) {
         const struct agx_vdm_index_list_direct_nonindexed_info nonindexed = {
            .primitive = primitives[p],
            .count = UINT32_C(0x11223344) + p,
            .instances = UINT32_C(0x102) + t,
            .start = UINT32_C(0x55667788) ^ p,
         };
         struct agx_vdm_index_list_direct_nonindexed packed_nonindexed;
         CHECK(agx_vdm_index_list_direct_nonindexed_pack(chip, &nonindexed,
                                                         &packed_nonindexed));

         uint8_t
            nonindexed_stream[AGX_VDM_INDEX_LIST_DIRECT_NONINDEXED_MAX_LENGTH +
                              32];
         memcpy(nonindexed_stream, packed_nonindexed.bytes,
                packed_nonindexed.length);
         memset(nonindexed_stream + packed_nonindexed.length, 0xa5, 32);

         struct agx_vdm_decoded_record record;
         CHECK(agx_vdm_decode_one(chip, nonindexed_stream,
                                  sizeof(nonindexed_stream), &record));
         CHECK(record.kind == AGX_VDM_RECORD_INDEX_LIST_NONINDEXED);
         CHECK(record.length == packed_nonindexed.length);
         CHECK(record.chip == chip);
         CHECK(record.data.index_list_nonindexed.primitive ==
               nonindexed.primitive);
         CHECK(record.data.index_list_nonindexed.count == nonindexed.count);
         CHECK(record.data.index_list_nonindexed.instances ==
               nonindexed.instances);
         CHECK(record.data.index_list_nonindexed.start == nonindexed.start);

         for (unsigned size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U16;
              size <= AGX_VDM_INDEX_LIST_INDEX_SIZE_U32; ++size) {
            const struct agx_vdm_index_list_direct_indexed_info indexed = {
               .primitive = primitives[p],
               .restart_enable = (p & 1) != 0,
               .index_size = (enum agx_vdm_index_list_index_size)size,
               .index_buffer = UINT64_C(0xab1234567800) + t * 4,
               .count = UINT32_C(0x23456789) + p,
               .instances = UINT32_C(0x203) + t,
               .start = UINT32_C(0x3456789a) ^ p,
               .index_buffer_size_word = UINT32_C(0x456789ab) + p,
            };
            struct agx_vdm_index_list_direct_indexed packed_indexed;
            CHECK(agx_vdm_index_list_direct_indexed_pack(chip, &indexed,
                                                         &packed_indexed));

            uint8_t
               indexed_stream[AGX_VDM_INDEX_LIST_DIRECT_INDEXED_MAX_LENGTH + 8];
            memcpy(indexed_stream, packed_indexed.bytes, packed_indexed.length);
            memset(indexed_stream + packed_indexed.length, 0x5a, 8);

            CHECK(agx_vdm_decode_one(chip, indexed_stream,
                                     sizeof(indexed_stream), &record));
            CHECK(record.kind == AGX_VDM_RECORD_INDEX_LIST_INDEXED);
            CHECK(record.length == packed_indexed.length);
            CHECK(record.chip == chip);
            CHECK(record.data.index_list_indexed.primitive ==
                  indexed.primitive);
            CHECK(record.data.index_list_indexed.restart_enable ==
                  indexed.restart_enable);
            CHECK(record.data.index_list_indexed.index_size ==
                  indexed.index_size);
            CHECK(record.data.index_list_indexed.index_buffer ==
                  indexed.index_buffer);
            CHECK(record.data.index_list_indexed.count == indexed.count);
            CHECK(record.data.index_list_indexed.instances ==
                  indexed.instances);
            CHECK(record.data.index_list_indexed.start == indexed.start);
            CHECK(record.data.index_list_indexed.index_buffer_size_word ==
                  indexed.index_buffer_size_word);
         }
      }
   }

   return 0;
}

static int
test_stream_link(void)
{
   for (unsigned t = 0;
        t < sizeof(supported_targets) / sizeof(supported_targets[0]); ++t) {
      const enum agx_chip chip = supported_targets[t];
      const uint64_t target = UINT64_C(0xcd12345678) + t;
      struct agx_vdm_stream_link_core packed;
      CHECK(agx_vdm_stream_link_core_pack(chip, target, &packed));

      uint8_t stream[AGX_VDM_STREAM_LINK_CORE_LENGTH + 4];
      memcpy(stream, packed.bytes, packed.length);
      memset(stream + packed.length, 0x5a, 4);

      struct agx_vdm_decoded_record record;
      CHECK(agx_vdm_decode_one(chip, stream, sizeof(stream), &record));
      CHECK(record.kind == AGX_VDM_RECORD_STREAM_LINK);
      CHECK(record.length == packed.length);
      CHECK(record.chip == chip);
      CHECK(record.data.stream_link.target == target);
   }

   return 0;
}

static int
test_rejection(void)
{
   const enum agx_chip chip = AGX_CHIP_G17S;
   struct agx_vdm_ppp_state_core ppp;
   struct agx_vdm_state_update state;
   struct agx_vdm_index_list_direct_nonindexed nonindexed;
   struct agx_vdm_index_list_direct_indexed indexed;
   struct agx_vdm_stream_link_core link;
   const struct agx_vdm_state_update_info state_info = {
      .pipeline = UINT64_C(0x123456780),
   };
   const struct agx_vdm_index_list_direct_nonindexed_info nonindexed_info = {
      .primitive = 3,
      .count = 7,
      .instances = 1,
      .start = 2,
   };
   const struct agx_vdm_index_list_direct_indexed_info indexed_info = {
      .primitive = 3,
      .index_size = AGX_VDM_INDEX_LIST_INDEX_SIZE_U16,
      .index_buffer = UINT64_C(0x123456780),
      .count = 7,
      .instances = 1,
      .start = 2,
      .index_buffer_size_word = 9,
   };

   CHECK(agx_vdm_ppp_state_core_pack(chip, UINT64_C(0x123456780), 7, &ppp));
   CHECK(agx_vdm_state_update_pack(chip, &state_info, &state));
   CHECK(agx_vdm_index_list_direct_nonindexed_pack(chip, &nonindexed_info,
                                                   &nonindexed));
   CHECK(agx_vdm_index_list_direct_indexed_pack(chip, &indexed_info, &indexed));
   CHECK(agx_vdm_stream_link_core_pack(chip, UINT64_C(0x123456789), &link));

   const struct {
      const uint8_t *bytes;
      size_t length;
   } records[] = {
      {ppp.bytes, ppp.length},
      {state.bytes, state.length},
      {nonindexed.bytes, nonindexed.length},
      {indexed.bytes, indexed.length},
      {link.bytes, link.length},
   };

   for (unsigned r = 0; r < sizeof(records) / sizeof(records[0]); ++r) {
      for (size_t available = 0; available < records[r].length; ++available)
         CHECK(check_rejected(chip, records[r].bytes, available) == 0);

      for (unsigned t = 0;
           t < sizeof(unsupported_targets) / sizeof(unsupported_targets[0]);
           ++t) {
         CHECK(check_rejected(unsupported_targets[t], records[r].bytes,
                              records[r].length) == 0);
      }
   }

   for (uint32_t type = 1; type < 8; ++type) {
      if (type == 2 || type == 3 || type == 4)
         continue;
      const uint8_t word[4] = {0, 0, 0, (uint8_t)(type << 5)};
      CHECK(check_rejected(chip, word, sizeof(word)) == 0);
   }

   uint8_t malformed_ppp[AGX_VDM_PPP_STATE_CORE_LENGTH];
   memcpy(malformed_ppp, ppp.bytes, sizeof(malformed_ppp));
   malformed_ppp[2] = 1;
   CHECK(check_rejected(chip, malformed_ppp, sizeof(malformed_ppp)) == 0);

   uint8_t malformed_state[AGX_VDM_STATE_UPDATE_LENGTH];
   memcpy(malformed_state, state.bytes, sizeof(malformed_state));
   malformed_state[0] ^= 1;
   CHECK(check_rejected(chip, malformed_state, sizeof(malformed_state)) == 0);

   uint8_t malformed_link[AGX_VDM_STREAM_LINK_CORE_LENGTH];
   memcpy(malformed_link, link.bytes, sizeof(malformed_link));
   malformed_link[2] = 1;
   CHECK(check_rejected(chip, malformed_link, sizeof(malformed_link)) == 0);

   struct agx_vdm_decoded_record record;
   CHECK(!agx_vdm_decode_one(chip, ppp.bytes, ppp.length, NULL));
   CHECK(!agx_vdm_decode_one(chip, NULL, ppp.length, &record));
   return 0;
}

int
main(void)
{
   CHECK(test_ppp_state_update() == 0);
   CHECK(test_state_update() == 0);
   CHECK(test_index_lists() == 0);
   CHECK(test_stream_link() == 0);
   CHECK(test_rejection() == 0);

   printf("agx_vdm_decode_core: %u checks passed\n", checks);
   return 0;
}
