/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_raytracing_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void
require(int condition, const char *message)
{
   checks++;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static struct d12_rt_shader_table
strided(uint64_t address, uint64_t size, uint64_t stride)
{
   return (struct d12_rt_shader_table){
      .start_address = address, .size_bytes = size, .stride_bytes = stride,
   };
}

/* Rotating scratch so several tables can be described inline in one
 * expression without each needing a named local. */
static const struct d12_rt_shader_table *
tbl(uint64_t address, uint64_t size, uint64_t stride)
{
   static struct d12_rt_shader_table scratch[8];
   static unsigned next;
   struct d12_rt_shader_table *slot = &scratch[next++ % 8];

   *slot = strided(address, size, stride);
   return slot;
}

static void
test_api_constants(void)
{
   require(D12_RT_SHADER_IDENTIFIER_SIZE == 32,
           "a shader identifier is 32 bytes");
   require(D12_RT_SHADER_RECORD_BYTE_ALIGNMENT == 32,
           "records align to 32 bytes");
   require(D12_RT_SHADER_TABLE_BYTE_ALIGNMENT == 64,
           "tables align to 64 bytes");
   require(D12_RT_MAX_SHADER_RECORD_STRIDE == 4096,
           "a record stride is at most 4096 bytes");
   require(D12_RT_AS_BYTE_ALIGNMENT == 256,
           "an acceleration structure aligns to 256 bytes");
   require(sizeof(struct d12_rt_shader_identifier) ==
              D12_RT_SHADER_IDENTIFIER_SIZE,
           "the identifier structure is exactly the API's 32 bytes");
}

static void
test_shader_table_rules(void)
{
   /* An absent table is legal: a pipeline may have no callable shaders. */
   struct d12_rt_shader_table absent = {0};
   require(d12_rt_shader_table_valid(&absent, D12_RT_TABLE_STRIDED),
           "an entirely absent table is legal");
   require(d12_rt_shader_table_valid(&absent, D12_RT_TABLE_RAY_GENERATION),
           "an absent ray-generation record is legal to validate");

   struct d12_rt_shader_table good = strided(0x40000, 128, 64);
   require(d12_rt_shader_table_valid(&good, D12_RT_TABLE_STRIDED),
           "an aligned table of whole records is legal");
   require(d12_rt_shader_table_records(&good) == 2,
           "two minimum-sized records are addressable");

   /* Alignment. */
   require(!d12_rt_shader_table_valid(tbl(0x40020, 128, 64),
                                      D12_RT_TABLE_STRIDED),
           "a table not on a 64-byte boundary is refused");
   require(!d12_rt_shader_table_valid(tbl(0x40000, 128, 48),
                                      D12_RT_TABLE_STRIDED),
           "a stride not on a 32-byte boundary is refused");

   /* A record has to be able to hold an identifier. */
   require(!d12_rt_shader_table_valid(tbl(0x40000, 128, 16),
                                      D12_RT_TABLE_STRIDED),
           "a stride smaller than an identifier is refused");
   require(!d12_rt_shader_table_valid(tbl(0x40000, 16, 0),
                                      D12_RT_TABLE_RAY_GENERATION),
           "a ray-generation record smaller than an identifier is refused");

   require(!d12_rt_shader_table_valid(
              tbl(0x40000, 8192, D12_RT_MAX_SHADER_RECORD_STRIDE + 32),
              D12_RT_TABLE_STRIDED),
           "a stride beyond the API maximum is refused");

   require(!d12_rt_shader_table_valid(
              tbl(0x40000, D12_RT_MAX_SHADER_RECORD_STRIDE + 1, 0),
              D12_RT_TABLE_RAY_GENERATION),
           "a ray-generation record beyond the API maximum is refused");

   /* The ray-generation table is one record and cannot carry a stride. */
   require(!d12_rt_shader_table_valid(tbl(0x40000, 64, 32),
                                      D12_RT_TABLE_RAY_GENERATION),
           "a ray-generation record with a stride is refused");
   require(d12_rt_shader_table_valid(tbl(0x40000, 64, 0),
                                     D12_RT_TABLE_RAY_GENERATION),
           "a ray-generation record without a stride is legal");
   require(d12_rt_shader_table_records(tbl(0x40000, 64, 0)) == 1,
           "a strideless table reports its one physical record");

   /* A zero stride is explicitly legal and makes every logical index alias
    * the first physical record. */
   require(d12_rt_shader_table_valid(tbl(0x40000, 64, 0),
                                     D12_RT_TABLE_STRIDED),
           "a zero-stride shader table is legal");

   /* SizeInBytes is a range, not a record count. A final minimum-sized
    * record may occupy only part of the last stride. */
   require(d12_rt_shader_table_valid(tbl(0x40000, 96, 64),
                                     D12_RT_TABLE_STRIDED),
           "a table range need not be a multiple of its stride");
   require(d12_rt_shader_table_records(tbl(0x40000, 96, 64)) == 2,
           "the final identifier-sized record is counted");
   require(d12_rt_shader_table_records(tbl(0x40000, 96, 48)) == 0,
           "a malformed table has no safely addressable records");

   /* Address-space overflow. */
   require(!d12_rt_shader_table_valid(tbl(UINT64_MAX - 32, 128, 64),
                                      D12_RT_TABLE_STRIDED),
           "a table that wraps the address space is refused");

   require(!d12_rt_shader_table_valid(NULL, D12_RT_TABLE_STRIDED),
           "a null table is refused");
   require(!d12_rt_shader_table_valid(
              &good, (enum d12_rt_table_kind)99),
           "an unknown table kind is refused");
}

static void
test_shader_record_addresses(void)
{
   const struct d12_rt_shader_table table = strided(0x40000, 160, 64);
   uint64_t address = 0;

   require(d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 0,
                                        64, &address) &&
              address == 0x40000,
           "the first full record resolves");
   require(d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 1,
                                        64, &address) &&
              address == 0x40040,
           "a middle full record resolves");
   require(d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 2,
                                        32, &address) &&
              address == 0x40080,
           "a final identifier-only record resolves inside the range");
   require(!d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 2,
                                         64, &address),
           "a final record extending beyond the range is refused");
   require(!d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 1,
                                         96, &address),
           "a record larger than its stride is refused");

   const struct d12_rt_shader_table alias = strided(0x50000, 64, 0);
   require(d12_rt_shader_record_address(&alias, D12_RT_TABLE_STRIDED,
                                        UINT64_MAX, 64, &address) &&
              address == 0x50000,
           "all indices of a zero-stride table resolve to one record");

   const struct d12_rt_shader_table raygen = strided(0x60000, 48, 0);
   require(d12_rt_shader_record_address(&raygen,
                                        D12_RT_TABLE_RAY_GENERATION, 0, 48,
                                        &address) &&
              address == 0x60000,
           "the sole ray-generation record resolves");
   require(!d12_rt_shader_record_address(&raygen,
                                         D12_RT_TABLE_RAY_GENERATION, 1, 48,
                                         &address),
           "a ray-generation record cannot be indexed past zero");
   require(!d12_rt_shader_record_address(&raygen,
                                         D12_RT_TABLE_RAY_GENERATION, 0, 64,
                                         &address),
           "a ray-generation record cannot exceed its range");

   require(!d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 0,
                                         16, &address),
           "a record smaller than an identifier is refused");
   require(!d12_rt_shader_record_address(&table, D12_RT_TABLE_STRIDED, 0,
                                         64, NULL),
           "resolving without an output address is refused");

   const struct d12_rt_shader_table wrapping =
      strided(UINT64_MAX - 63, 64, 0);
   require(!d12_rt_shader_record_address(&wrapping, D12_RT_TABLE_STRIDED, 0,
                                         64, &address),
           "a table range that wraps cannot resolve a record");
}

static void
test_dispatch_rules(void)
{
   struct d12_rt_dispatch_rays dispatch = {
      .ray_generation = strided(0x40000, 64, 0),
      .miss = strided(0x41000, 128, 64),
      .hit_group = strided(0x42000, 256, 64),
      .width = 1920,
      .height = 1080,
      .depth = 1,
   };
   require(d12_rt_dispatch_valid(&dispatch), "an ordinary dispatch is legal");

   /* Rays have to come from somewhere. */
   struct d12_rt_dispatch_rays no_raygen = dispatch;
   no_raygen.ray_generation = (struct d12_rt_shader_table){0};
   require(!d12_rt_dispatch_valid(&no_raygen),
           "a dispatch with no ray-generation record is refused");

   /* Any zero dimension makes the dispatch a no-op. */
   struct d12_rt_dispatch_rays empty = dispatch;
   empty.width = empty.height = empty.depth = 0;
   require(d12_rt_dispatch_valid(&empty), "a fully zero dispatch is legal");

   struct d12_rt_dispatch_rays partial = dispatch;
   partial.height = 0;
   require(d12_rt_dispatch_valid(&partial),
           "any zero dispatch dimension makes a legal no-op");

   partial.ray_generation = (struct d12_rt_shader_table){0};
   partial.miss = strided(UINT64_MAX - 32, 128, 64);
   require(d12_rt_dispatch_valid(&partial),
           "a no-op dispatch does not access or require shader tables");

   /* The thread limit is a product, so three individually legal dimensions
    * can still exceed it. */
   struct d12_rt_dispatch_rays huge = dispatch;
   huge.width = 65536;
   huge.height = 65536;
   huge.depth = 2;
   require(!d12_rt_dispatch_valid(&huge),
           "a dispatch whose thread product exceeds the maximum is refused");

   huge.width = huge.height = huge.depth = UINT32_MAX;
   require(!d12_rt_dispatch_valid(&huge),
           "a 96-bit dimension product is refused without wrapping");

   struct d12_rt_dispatch_rays at_limit = dispatch;
   at_limit.width = D12_RT_MAX_RAYGEN_THREADS;
   at_limit.height = 1;
   at_limit.depth = 1;
   require(d12_rt_dispatch_valid(&at_limit),
           "a dispatch exactly at the thread maximum is legal");

   /* A malformed subordinate table fails the whole dispatch. */
   struct d12_rt_dispatch_rays bad_miss = dispatch;
   bad_miss.miss = strided(0x41020, 128, 64);
   require(!d12_rt_dispatch_valid(&bad_miss),
           "a misaligned miss table fails the dispatch");

   require(!d12_rt_dispatch_valid(NULL), "a null dispatch is refused");
}

static void
test_build_flags(void)
{
   require(d12_rt_build_flags_valid(0), "no flags is legal");
   require(d12_rt_build_flags_valid(D12_RT_BUILD_FLAG_PREFER_FAST_TRACE),
           "preferring fast trace is legal");
   require(d12_rt_build_flags_valid(D12_RT_BUILD_FLAG_ALLOW_UPDATE |
                                    D12_RT_BUILD_FLAG_PERFORM_UPDATE),
           "an update of a structure built to allow it is legal");

   require(!d12_rt_build_flags_valid(D12_RT_BUILD_FLAG_PREFER_FAST_TRACE |
                                     D12_RT_BUILD_FLAG_PREFER_FAST_BUILD),
           "asking for both fast trace and fast build is refused");
   require(!d12_rt_build_flags_valid(D12_RT_BUILD_FLAG_PERFORM_UPDATE),
           "updating a structure not built to allow it is refused");
   require(!d12_rt_build_flags_valid(0x40u),
           "an unknown build flag is refused");
}

static void
test_prebuild_and_allocation(void)
{
   const struct d12_rt_prebuild_info info = {
      .result_max_size = 4096,
      .scratch_size = 2048,
      .update_scratch_size = 512,
   };

   require(d12_rt_build_allocation_valid(&info, 0x100000, 4096, 0x200000,
                                         2048, false),
           "an allocation matching the prebuild result is legal");
   require(d12_rt_build_allocation_valid(&info, 0x100000, 8192, 0x200000,
                                         4096, false),
           "a larger allocation is legal");

   require(!d12_rt_build_allocation_valid(&info, 0x100000, 2048, 0x200000,
                                          2048, false),
           "a destination smaller than the result size is refused");
   require(!d12_rt_build_allocation_valid(&info, 0x100000, 4096, 0x200000,
                                          1024, false),
           "scratch smaller than the build requires is refused");

   /* An update needs the update scratch size, which is smaller. */
   require(d12_rt_build_allocation_valid(&info, 0x100000, 4096, 0x200000,
                                         512, true),
           "an update needs only the update scratch size");
   require(!d12_rt_build_allocation_valid(&info, 0x100000, 4096, 0x200000,
                                          256, true),
           "scratch smaller than the update requires is refused");

   /* Both addresses carry the acceleration-structure alignment. */
   require(!d12_rt_build_allocation_valid(&info, 0x100010, 4096, 0x200000,
                                          2048, false),
           "an unaligned destination is refused");
   require(!d12_rt_build_allocation_valid(&info, 0x100000, 4096, 0x200010,
                                          2048, false),
           "unaligned scratch is refused");
   require(!d12_rt_build_allocation_valid(&info, 0, 4096, 0x200000, 2048,
                                          false),
           "a null destination is refused");

   /* Sizing rounds to the structure alignment. */
   require(d12_rt_align_as(1) == D12_RT_AS_BYTE_ALIGNMENT,
           "any nonzero size rounds up to the alignment");
   require(d12_rt_align_as(256) == 256, "an aligned size is unchanged");
   require(d12_rt_align_as(257) == 512, "a size past the boundary rounds up");
   require(d12_rt_align_as(UINT64_MAX) == 0,
           "a size that cannot be aligned reports failure");
}

static void
test_shader_identifiers(void)
{
   struct d12_rt_shader_identifier id;
   uint32_t object = 0, index = 0;
   enum d12_rt_export_kind kind = D12_RT_EXPORT_MISS;

   require(d12_rt_identifier_pack(7, 3, D12_RT_EXPORT_HIT_GROUP, &id),
           "an identifier packs");
   require(d12_rt_identifier_unpack(&id, &object, &index, &kind),
           "a packed identifier unpacks");
   require(object == 7 && index == 3 && kind == D12_RT_EXPORT_HIT_GROUP,
           "the identifier round-trips");

   /* These bytes live in application memory, so anything that is not one of
    * ours has to be rejected rather than interpreted. */
   struct d12_rt_shader_identifier zeroed;
   memset(&zeroed, 0, sizeof(zeroed));
   require(!d12_rt_identifier_unpack(&zeroed, NULL, NULL, NULL),
           "an uninitialised record is not a valid identifier");

   struct d12_rt_shader_identifier corrupt = id;
   corrupt.magic ^= 1u;
   require(!d12_rt_identifier_unpack(&corrupt, NULL, NULL, NULL),
           "a record with the wrong tag is refused");

   struct d12_rt_shader_identifier dirty = id;
   dirty.reserved[1] = 1;
   require(!d12_rt_identifier_unpack(&dirty, NULL, NULL, NULL),
           "a record with a non-zero reserved field is refused");

   struct d12_rt_shader_identifier bad_kind = id;
   bad_kind.kind = 99;
   require(!d12_rt_identifier_unpack(&bad_kind, NULL, NULL, NULL),
           "a record naming an unknown export kind is refused");

   require(!d12_rt_identifier_pack(0, 0, (enum d12_rt_export_kind)99, &id),
           "packing an unknown export kind is refused");
   require(!d12_rt_identifier_pack(0, 0, D12_RT_EXPORT_MISS, NULL),
           "packing into nothing is refused");

   /* Distinct exports must not collide, or a shader table would dispatch the
    * wrong shader. */
   struct d12_rt_shader_identifier a, b;
   require(d12_rt_identifier_pack(1, 0, D12_RT_EXPORT_MISS, &a) &&
              d12_rt_identifier_pack(1, 1, D12_RT_EXPORT_MISS, &b) &&
              memcmp(&a, &b, sizeof(a)) != 0,
           "different exports of one state object differ");
   require(d12_rt_identifier_pack(1, 0, D12_RT_EXPORT_MISS, &a) &&
              d12_rt_identifier_pack(2, 0, D12_RT_EXPORT_MISS, &b) &&
              memcmp(&a, &b, sizeof(a)) != 0,
           "the same export of different state objects differs");
   require(d12_rt_identifier_pack(1, 0, D12_RT_EXPORT_MISS, &a) &&
              d12_rt_identifier_pack(1, 0, D12_RT_EXPORT_CALLABLE, &b) &&
              memcmp(&a, &b, sizeof(a)) != 0,
           "the same index in different export kinds differs");
   require(d12_rt_identifier_pack(1, 0, D12_RT_EXPORT_MISS, &a) &&
              d12_rt_identifier_pack(1, 0, D12_RT_EXPORT_MISS, &b) &&
              memcmp(&a, &b, sizeof(a)) == 0,
           "the same export is stable across queries");
}

static void
test_records_and_config(void)
{
   require(d12_rt_record_fits(64, 32), "a record holds identifier plus root");
   require(d12_rt_record_fits(32, 0), "a bare identifier fits a 32-byte record");
   require(!d12_rt_record_fits(32, 8),
           "a record with no room for its root arguments is refused");
   require(!d12_rt_record_fits(64, UINT64_MAX),
           "an argument size that overflows the sum is refused");

   require(d12_rt_pipeline_config_valid(1, 8),
           "one level of recursion with barycentric attributes is legal");
   require(d12_rt_pipeline_config_valid(D12_RT_MAX_DECLARABLE_RECURSION,
                                        D12_RT_MAX_ATTRIBUTE_SIZE),
           "the declared maxima are legal");
   require(!d12_rt_pipeline_config_valid(D12_RT_MAX_DECLARABLE_RECURSION + 1,
                                         8),
           "recursion beyond the declarable maximum is refused");
   require(!d12_rt_pipeline_config_valid(1, D12_RT_MAX_ATTRIBUTE_SIZE + 1),
           "an attribute larger than the maximum is refused");
   require(!d12_rt_pipeline_config_valid(1, 4),
           "an attribute too small for barycentrics is refused");

   require(d12_rt_blas_counts_valid(1, 1), "a one-triangle blas is legal");
   require(d12_rt_blas_counts_valid(D12_RT_MAX_GEOMETRIES_PER_BLAS,
                                    D12_RT_MAX_PRIMITIVES_PER_BLAS),
           "the declared blas maxima are legal");
   require(!d12_rt_blas_counts_valid(D12_RT_MAX_GEOMETRIES_PER_BLAS + 1, 1),
           "too many geometries is refused");
   require(!d12_rt_blas_counts_valid(1, D12_RT_MAX_PRIMITIVES_PER_BLAS + 1),
           "too many primitives is refused");
   require(d12_rt_tlas_counts_valid(D12_RT_MAX_INSTANCES_PER_TLAS),
           "the declared tlas maximum is legal");
   require(!d12_rt_tlas_counts_valid(D12_RT_MAX_INSTANCES_PER_TLAS + 1),
           "too many instances is refused");
}

static void
test_agx3_canonical_update_node(void)
{
   static const uint8_t agx3b[] = {0xdf, 0x05, 0x54, 0x00};
   static const uint8_t agx3c[] = {
      0x27, 0x00, 0x01, 0x00, 0x22, 0x00, 0xad, 0x02,
   };

   require(d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_B, agx3b, sizeof(agx3b)),
           "the exact AGX3b canonical update_node operation is recognised");
   require(d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_C, agx3c, sizeof(agx3c)),
           "the exact AGX3c canonical update_node operation is recognised");

   require(!d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_B, agx3c, sizeof(agx3c)) &&
              !d12_rt_agx3_canonical_update_node_valid(
                 D12_RT_AGX3_ENCODING_C, agx3b, sizeof(agx3b)),
           "canonical update_node encodings are not interchangeable");

   uint8_t corrupt_b[sizeof(agx3b)];
   memcpy(corrupt_b, agx3b, sizeof(corrupt_b));
   corrupt_b[2] ^= 0x01;
   require(!d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_B, corrupt_b, sizeof(corrupt_b)),
           "a changed AGX3b update_node bit is refused");

   uint8_t corrupt_c[sizeof(agx3c)];
   memcpy(corrupt_c, agx3c, sizeof(corrupt_c));
   corrupt_c[7] ^= 0x80;
   require(!d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_C, corrupt_c, sizeof(corrupt_c)),
           "a changed AGX3c update_node bit is refused");

   require(!d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_B, agx3b, sizeof(agx3b) - 1) &&
              !d12_rt_agx3_canonical_update_node_valid(
                 D12_RT_AGX3_ENCODING_C, agx3c, sizeof(agx3c) - 1),
           "truncated canonical update_node operations are refused");
   require(!d12_rt_agx3_canonical_update_node_valid(
              (enum d12_rt_agx3_encoding)99, agx3b, sizeof(agx3b)),
           "an unknown AGX3 encoding is refused");
   require(!d12_rt_agx3_canonical_update_node_valid(
              D12_RT_AGX3_ENCODING_B, NULL, sizeof(agx3b)),
           "a missing canonical update_node operation is refused");
}

static void
test_agx3_update_node_expansion(void)
{
   static const uint8_t agx3b[] = {
      0x07, 0x04, 0x54, 0x08, 0x09, 0x00, 0xdf, 0x05, 0x54, 0x00,
   };
   static const uint8_t agx3c[] = {
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00, 0x27,
      0x00, 0x01, 0x00, 0x22, 0x00, 0xad, 0x02,
   };
   const struct {
      enum d12_rt_agx3_encoding encoding;
      const uint8_t *expected;
      uint32_t length;
   } vectors[] = {
      {D12_RT_AGX3_ENCODING_B, agx3b, sizeof(agx3b)},
      {D12_RT_AGX3_ENCODING_C, agx3c, sizeof(agx3c)},
   };

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      struct d12_rt_agx3_update_node_expansion generated;
      memset(&generated, 0xff, sizeof(generated));
      require(d12_rt_agx3_update_node_expansion_generate(
                 vectors[v].encoding, &generated),
              "DXR generates a complete canonical update_node expansion");
      require(generated.length == vectors[v].length,
              "DXR receives the exact update_node expansion length");
      require(memcmp(generated.bytes, vectors[v].expected,
                     vectors[v].length) == 0,
              "DXR receives the exact barrier-plus-update_node bytes");
      for (unsigned i = vectors[v].length; i < sizeof(generated.bytes); ++i)
         require(generated.bytes[i] == 0,
                 "DXR zeroes unused update_node expansion storage");

      require(d12_rt_agx3_update_node_expansion_valid(
                 vectors[v].encoding, vectors[v].expected,
                 vectors[v].length),
              "DXR accepts the exact complete update_node expansion");

      /* Both the barrier and trailing operation are fixed.  No unknown bit
       * or operand-bearing update form may pass this validator. */
      for (unsigned bit = 0; bit < vectors[v].length * 8; ++bit) {
         uint8_t malformed[AGX3_UPDATE_NODE_MAX_LENGTH] = {0};
         memcpy(malformed, vectors[v].expected, vectors[v].length);
         malformed[bit / 8] ^= 1u << (bit % 8);
         require(!d12_rt_agx3_update_node_expansion_valid(
                    vectors[v].encoding, malformed, vectors[v].length),
                 "DXR refuses every changed update_node expansion bit");
      }

      for (uint32_t length = 0; length < vectors[v].length; ++length) {
         require(!d12_rt_agx3_update_node_expansion_valid(
                    vectors[v].encoding, vectors[v].expected, length),
                 "DXR refuses every truncated update_node expansion");
      }

      uint8_t missing_barrier[AGX3_UPDATE_NODE_MAX_LENGTH] = {0};
      memcpy(missing_barrier, vectors[v].expected + 6,
             vectors[v].length - 6);
      require(!d12_rt_agx3_update_node_expansion_valid(
                 vectors[v].encoding, missing_barrier, vectors[v].length),
              "DXR refuses a full-sized expansion with its barrier missing");
      require(!d12_rt_agx3_update_node_expansion_valid(
                 vectors[v].encoding, vectors[v].expected + 6,
                 vectors[v].length - 6),
              "DXR refuses the canonical trailing operation by itself");

      uint8_t oversized[AGX3_UPDATE_NODE_MAX_LENGTH + 1] = {0};
      memcpy(oversized, vectors[v].expected, vectors[v].length);
      require(!d12_rt_agx3_update_node_expansion_valid(
                 vectors[v].encoding, oversized, vectors[v].length + 1),
              "DXR refuses trailing bytes after a complete expansion");
   }

   require(!d12_rt_agx3_update_node_expansion_valid(
              D12_RT_AGX3_ENCODING_B, agx3c, sizeof(agx3c)) &&
              !d12_rt_agx3_update_node_expansion_valid(
                 D12_RT_AGX3_ENCODING_C, agx3b, sizeof(agx3b)),
           "DXR refuses cross-generation update_node expansions");

   struct d12_rt_agx3_update_node_expansion rejected;
   memset(&rejected, 0xff, sizeof(rejected));
   require(!d12_rt_agx3_update_node_expansion_generate(
              (enum d12_rt_agx3_encoding)99, &rejected),
           "DXR refuses to generate an unknown update_node expansion");
   require(rejected.length == 0,
           "a rejected update_node generation clears its length");
   for (unsigned i = 0; i < sizeof(rejected.bytes); ++i)
      require(rejected.bytes[i] == 0,
              "a rejected update_node generation clears all output bytes");
   require(!d12_rt_agx3_update_node_expansion_generate(
              D12_RT_AGX3_ENCODING_B, NULL),
           "DXR refuses a missing update_node generation destination");
   require(!d12_rt_agx3_update_node_expansion_valid(
              (enum d12_rt_agx3_encoding)99, agx3b, sizeof(agx3b)),
           "DXR refuses an unknown update_node expansion encoding");
   require(!d12_rt_agx3_update_node_expansion_valid(
              D12_RT_AGX3_ENCODING_B, NULL, sizeof(agx3b)),
           "DXR refuses a missing update_node expansion");
}

static bool
continue_ray_target_supported(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
      return true;
   default:
      return false;
   }
}

static void
test_agx3_continue_ray_fixed_first_site_witness(void)
{
   static const uint8_t expected_b[] = {
      0x5f, 0x11, 0x54, 0x00, 0x00, 0x00,
   };
   static const uint8_t expected_c[] = {
      0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x02,
   };
   static const uint8_t second_site_b[] = {
      0x5f, 0x11, 0x54, 0x02, 0x00, 0x00,
   };
   static const uint8_t second_site_c[] = {
      0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x42,
   };
   const struct {
      enum agx_isa_encoding encoding;
      enum agx_chip representative;
      const uint8_t *expected;
      const uint8_t *second_site;
      uint32_t length;
   } vectors[] = {
      {AGX_ISA_ENCODING_AGX3B, AGX_CHIP_G15G, expected_b, second_site_b,
       sizeof(expected_b)},
      {AGX_ISA_ENCODING_AGX3C, AGX_CHIP_G17G, expected_c, second_site_c,
       sizeof(expected_c)},
   };

   for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
      struct agx3_encoded_continue_ray_fixed_first_site_witness shared;
      uint8_t shared_length = 0;
      require(agx3_pack_continue_ray_fixed_first_site_witness(
                 vectors[v].encoding, &shared),
              "the shared packer provides a continue_ray first-site witness");
      require(agx3_unpack_continue_ray_fixed_first_site_witness(
                 vectors[v].encoding, shared.bytes, shared.length,
                 &shared_length),
              "the shared unpacker accepts its continue_ray first-site bytes");
      require(shared_length == vectors[v].length &&
                 shared.length == vectors[v].length &&
                 memcmp(shared.bytes, vectors[v].expected,
                        vectors[v].length) == 0,
              "the shared continue_ray contract agrees with the pinned bytes");

      struct d12_rt_agx3_continue_ray_fixed_first_site_witness generated;
      memset(&generated, 0xff, sizeof(generated));
      require(d12_rt_agx3_continue_ray_fixed_first_site_witness_generate(
                 vectors[v].representative, &generated),
              "DXR generates a compiler-witnessed continue_ray first site");
      require(generated.length == vectors[v].length &&
                 memcmp(generated.bytes, vectors[v].expected,
                        vectors[v].length) == 0 &&
                 memcmp(generated.bytes, shared.bytes,
                        vectors[v].length) == 0,
              "DXR and the shared packer agree on continue_ray first-site bytes");
      for (unsigned i = vectors[v].length; i < sizeof(generated.bytes); ++i)
         require(generated.bytes[i] == 0,
                 "DXR zeroes unused continue_ray witness storage");
      require(d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                 vectors[v].representative, generated.bytes,
                 generated.length),
              "DXR validates the exact continue_ray first-site witness");

      for (unsigned bit = 0; bit < vectors[v].length * 8; ++bit) {
         uint8_t malformed[
            AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
         memcpy(malformed, vectors[v].expected, vectors[v].length);
         malformed[bit / 8] ^= 1u << (bit % 8);
         require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                    vectors[v].representative, malformed,
                    vectors[v].length),
                 "DXR refuses every changed continue_ray first-site bit");
      }

      for (uint32_t length = 0; length < vectors[v].length; ++length) {
         require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                    vectors[v].representative, vectors[v].expected, length),
                 "DXR refuses every truncated continue_ray first-site witness");
      }

      uint8_t appended[
         AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH + 1];
      memcpy(appended, vectors[v].expected, vectors[v].length);
      appended[vectors[v].length] = 0xff;
      require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                 vectors[v].representative, appended,
                 vectors[v].length + 1),
              "DXR refuses bytes trailing the continue_ray first-site witness");
      require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                 vectors[v].representative, vectors[v].second_site,
                 vectors[v].length),
              "DXR refuses the compiler's second continue_ray site");
   }

   require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
              AGX_CHIP_G15G, expected_c, sizeof(expected_c)) &&
              !d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                 AGX_CHIP_G17G, expected_b, sizeof(expected_b)),
           "DXR refuses cross-generation continue_ray first-site witnesses");

   /* Every compiler-witnessed target receives its generation's exact bytes. */
   for (enum agx_chip chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN;
        chip = (enum agx_chip)(chip + 1)) {
      struct d12_rt_agx3_continue_ray_fixed_first_site_witness generated;
      memset(&generated, 0xff, sizeof(generated));

      if (continue_ray_target_supported(chip)) {
         const enum agx_isa_encoding encoding =
            d12_rt_agx3_continue_ray_fixed_first_site_witness_encoding(chip);
         const uint8_t *expected = encoding == AGX_ISA_ENCODING_AGX3B
                                      ? expected_b
                                      : expected_c;
         const uint32_t length = encoding == AGX_ISA_ENCODING_AGX3B
                                    ? sizeof(expected_b)
                                    : sizeof(expected_c);
         require(d12_rt_agx3_continue_ray_fixed_first_site_witness_generate(
                    chip, &generated),
                 "DXR admits every compiler-witnessed continue_ray target");
         require(generated.length == length &&
                    memcmp(generated.bytes, expected, length) == 0 &&
                    d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                       chip, generated.bytes, generated.length),
                 "every admitted continue_ray target uses its exact encoding");
      } else {
         require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_generate(
                    chip, &generated),
                 "DXR refuses every unwitnessed continue_ray target");
         require(generated.length == 0,
                 "a rejected continue_ray target clears the output length");
         for (unsigned i = 0; i < sizeof(generated.bytes); ++i)
            require(generated.bytes[i] == 0,
                    "a rejected continue_ray target clears every output byte");
         require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                    chip, expected_b, sizeof(expected_b)) &&
                    !d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
                       chip, expected_c, sizeof(expected_c)),
                 "DXR rejects both continue_ray encodings on unwitnessed targets");
      }
   }

   struct d12_rt_agx3_continue_ray_fixed_first_site_witness rejected;
   memset(&rejected, 0xff, sizeof(rejected));
   require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_generate(
              (enum agx_chip)99, &rejected),
           "DXR refuses an invalid continue_ray generation target");
   require(rejected.length == 0,
           "an invalid continue_ray target clears the output length");
   for (unsigned i = 0; i < sizeof(rejected.bytes); ++i)
      require(rejected.bytes[i] == 0,
              "an invalid continue_ray target clears every output byte");
   require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
              (enum agx_chip)99, expected_c, sizeof(expected_c)),
           "DXR refuses an invalid continue_ray validation target");
   require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_generate(
              AGX_CHIP_G17S, NULL),
           "DXR refuses a missing continue_ray generation destination");
   require(!d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
              AGX_CHIP_G17S, NULL, sizeof(expected_c)),
           "DXR refuses a missing continue_ray first-site witness");
}

static void
test_agx3_update_bvh_fixed_witness(void)
{
   static const uint8_t expected[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH] = {
      /* barrier */
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00,
      /* update_bvh */
      0x27, 0x00, 0x21, 0x00, 0x22, 0x80, 0xac, 0x42,
   };
   static const uint8_t update_node_agx3c[AGX3_UPDATE_NODE_MAX_LENGTH] = {
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00, 0x27,
      0x00, 0x01, 0x00, 0x22, 0x00, 0xad, 0x02,
   };

   struct agx3_encoded_update_bvh_fixed_witness shared;
   uint8_t shared_length = 0;
   require(agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX3C,
                                               &shared),
           "the shared packer provides the fixed update_bvh witness");
   require(agx3_unpack_update_bvh_fixed_witness(
              AGX_ISA_ENCODING_AGX3C, shared.bytes, shared.length,
              &shared_length),
           "the shared unpacker accepts its fixed update_bvh witness");
   require(shared_length == sizeof(expected) &&
              shared.length == sizeof(expected) &&
              memcmp(shared.bytes, expected, sizeof(expected)) == 0,
           "the shared update_bvh contract agrees with the pinned bytes");

   struct d12_rt_agx3_update_bvh_fixed_witness generated;
   memset(&generated, 0xff, sizeof(generated));
   require(d12_rt_agx3_update_bvh_fixed_witness_generate(AGX_CHIP_G17S,
                                                          &generated),
           "DXR generates the exact G17S update_bvh fixed witness");
   require(generated.length == sizeof(expected) &&
              memcmp(generated.bytes, expected, sizeof(expected)) == 0 &&
              memcmp(generated.bytes, shared.bytes, sizeof(expected)) == 0,
           "DXR and the shared packer produce identical update_bvh bytes");
   require(d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, generated.bytes, generated.length),
           "DXR validates the complete G17S update_bvh fixed witness");

   /* Every bit in both the barrier and operation is canonical. */
   for (unsigned bit = 0; bit < sizeof(expected) * 8; ++bit) {
      uint8_t malformed[sizeof(expected)];
      memcpy(malformed, expected, sizeof(malformed));
      malformed[bit / 8] ^= 1u << (bit % 8);
      require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
                 AGX_CHIP_G17S, malformed, sizeof(malformed)),
              "DXR refuses every changed update_bvh witness bit");
   }

   for (uint32_t length = 0; length < sizeof(expected); ++length) {
      require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
                 AGX_CHIP_G17S, expected, length),
              "DXR refuses every truncated update_bvh fixed witness");
   }

   uint8_t appended[sizeof(expected) + 1];
   memcpy(appended, expected, sizeof(expected));
   appended[sizeof(expected)] = 0xff;
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, appended, sizeof(appended)),
           "DXR refuses trailing bytes after the update_bvh witness");

   uint8_t missing_barrier[sizeof(expected)] = {0};
   memcpy(missing_barrier, expected + 6, sizeof(expected) - 6);
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, missing_barrier, sizeof(missing_barrier)),
           "DXR refuses a full-sized update_bvh witness without its barrier");
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, expected, 6),
           "DXR refuses the update_bvh barrier by itself");
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, expected + 6, sizeof(expected) - 6),
           "DXR refuses the update_bvh operation by itself");
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, update_node_agx3c,
              sizeof(update_node_agx3c)),
           "DXR does not confuse update_node with update_bvh");

   /* AGX encoding identity is insufficient: only the compiler-witnessed
    * G17S target is admitted, including for generation output. */
   for (enum agx_chip chip = AGX_CHIP_G13G; chip <= AGX_CHIP_UNKNOWN;
        chip = (enum agx_chip)(chip + 1)) {
      if (chip == AGX_CHIP_G17S)
         continue;

      memset(&generated, 0xff, sizeof(generated));
      require(!d12_rt_agx3_update_bvh_fixed_witness_generate(chip,
                                                              &generated),
              "DXR refuses every non-G17S update_bvh generation target");
      require(generated.length == 0,
              "a rejected update_bvh generation clears its length");
      for (unsigned i = 0; i < sizeof(generated.bytes); ++i)
         require(generated.bytes[i] == 0,
                 "a rejected update_bvh generation clears every byte");
      require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
                 chip, expected, sizeof(expected)),
              "DXR refuses the G17S update_bvh bytes on every other target");
   }

   memset(&generated, 0xff, sizeof(generated));
   require(!d12_rt_agx3_update_bvh_fixed_witness_generate(
              (enum agx_chip)99, &generated),
           "DXR refuses an invalid update_bvh generation target");
   require(generated.length == 0,
           "an invalid update_bvh target clears the output length");
   for (unsigned i = 0; i < sizeof(generated.bytes); ++i)
      require(generated.bytes[i] == 0,
              "an invalid update_bvh target clears every output byte");
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              (enum agx_chip)99, expected, sizeof(expected)),
           "DXR refuses an invalid update_bvh validation target");
   require(!d12_rt_agx3_update_bvh_fixed_witness_generate(AGX_CHIP_G17S,
                                                           NULL),
           "DXR refuses a missing update_bvh generation destination");
   require(!d12_rt_agx3_update_bvh_fixed_witness_valid(
              AGX_CHIP_G17S, NULL, sizeof(expected)),
           "DXR refuses a missing update_bvh fixed witness");
}

static void
pack_bvh_header(const uint64_t words[8], uint8_t packed[64])
{
   for (unsigned word = 0; word < 8; ++word) {
      for (unsigned byte = 0; byte < 8; ++byte)
         packed[(word * 8) + byte] = (uint8_t)(words[word] >> (byte * 8));
   }
}

static uint64_t
pack_gen1_quantized_bounds(unsigned child)
{
   static const unsigned shifts[6] = {57, 50, 43, 36, 29, 22};
   uint64_t word = 0;

   for (unsigned component = 0; component < 6; ++component) {
      const uint64_t value = (child * 11u + component * 7u) & 0x7fu;
      word |= value << shifts[component];
   }

   return word;
}

static uint32_t
rt_float_bits(float value)
{
   uint32_t bits;
   memcpy(&bits, &value, sizeof(bits));
   return bits;
}

static void
test_agx_gen1_hw_triangle_leaf(void)
{
   static const uint8_t captured[AGX_RT_BVH_GEN1_HW_TRIANGLE_LEAF_SIZE] = {
      0x50, 0x00, 0x00, 0x00, 0x9c, 0x54, 0xce, 0xbe,
      0x08, 0xec, 0xd2, 0xbd, 0x9c, 0x54, 0xce, 0xbe,
      0x9c, 0x54, 0xce, 0xbe, 0x05, 0xc0, 0x7d, 0xbf,
      0x05, 0xc0, 0x7d, 0xbf, 0x38, 0xf3, 0x30, 0xbf,
      0x38, 0xf3, 0x30, 0xbf, 0x33, 0x33, 0xb3, 0x3e,
      0x33, 0x33, 0xb3, 0x3e, 0x66, 0x66, 0xe6, 0x3e,
      0x66, 0x66, 0xe6, 0x3e, 0x00, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
   };
   static const uint32_t expected_vertex_bits[4][3] = {
      {0xbece549c, 0xbf7dc005, 0x3eb33333},
      {0xbdd2ec08, 0xbf7dc005, 0x3eb33333},
      {0xbece549c, 0xbf30f338, 0x3ee66666},
      {0xbece549c, 0xbf30f338, 0x3ee66666},
   };

   struct d12_rt_agx_bvh_gen1_hw_triangle_leaf leaf;
   require(d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
              captured, sizeof(captured), &leaf),
           "DXR decodes the captured Gen1 hardware triangle leaf");
   for (unsigned vertex = 0; vertex < 4; ++vertex) {
      for (unsigned axis = 0; axis < 3; ++axis) {
         require(rt_float_bits(leaf.vertex[vertex][axis]) ==
                    expected_vertex_bits[vertex][axis],
                 "DXR receives every triangle vertex coordinate exactly");
      }
   }
   require(leaf.geometry_word == 0 && leaf.geometry_id == 0 &&
              leaf.primitive_word[0] == 7 &&
              leaf.primitive_word[1] == 7 && leaf.primitive_id[0] == 7 &&
              leaf.primitive_id[1] == 7,
           "DXR receives the captured raw and low-30-bit triangle IDs");

   uint8_t flagged[sizeof(captured)];
   memcpy(flagged, captured, sizeof(flagged));
   flagged[55] = 0xc0;
   flagged[59] = 0x80;
   flagged[63] = 0x40;
   require(d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
              flagged, sizeof(flagged), &leaf),
           "DXR preserves a triangle leaf with unknown high ID bits");
   require(leaf.geometry_word == UINT32_C(0xc0000000) &&
              leaf.primitive_word[0] == UINT32_C(0x80000007) &&
              leaf.primitive_word[1] == UINT32_C(0x40000007) &&
              leaf.geometry_id == 0 && leaf.primitive_id[0] == 7 &&
              leaf.primitive_id[1] == 7,
           "DXR separates raw triangle ID words from their proved low bits");

   const struct d12_rt_agx_bvh_gen1_hw_triangle_leaf zero = {0};
   memset(&leaf, 0xa5, sizeof(leaf));
   require(!d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
              captured, sizeof(captured) - 1, &leaf),
           "DXR rejects a truncated Gen1 hardware triangle leaf");
   require(memcmp(&leaf, &zero, sizeof(leaf)) == 0,
           "a truncated DXR triangle leaf leaves no stale data");

   uint8_t wrong_marker[sizeof(captured)];
   memcpy(wrong_marker, captured, sizeof(wrong_marker));
   wrong_marker[0] ^= 1;
   memset(&leaf, 0xa5, sizeof(leaf));
   require(!d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
              wrong_marker, sizeof(wrong_marker), &leaf),
           "DXR rejects a wrong Gen1 hardware triangle marker");
   require(memcmp(&leaf, &zero, sizeof(leaf)) == 0,
           "a wrong DXR triangle marker leaves no stale data");
   require(!d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
              NULL, sizeof(captured), &leaf),
           "DXR rejects a missing Gen1 hardware triangle leaf");
   require(!d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
              captured, sizeof(captured), NULL),
           "DXR rejects a missing triangle-leaf destination");
}

static void
test_agx_gen1_child_layout(void)
{
   /* Two inner children followed by three leaf boxes.  Their decoded leaf
    * ends are 9, 17 and 4, giving 17 leaves and three six-leaf slots. */
   uint64_t words[8] = {
      UINT64_C(6) << 1,
      0,
      UINT64_C(5) << 16,
      (UINT64_C(15) << 16) | (UINT64_C(2) << 20),
      0,
      0,
      100,
      (UINT64_C(26) << 8) | ((UINT64_C(3) << 3) | 2),
   };

   for (unsigned child = 0; child < 8; ++child)
      words[child] |= pack_gen1_quantized_bounds(child);

   uint8_t packed[AGX_RT_BVH_GEN1_HEADER_SIZE];
   pack_bvh_header(words, packed);

   struct d12_rt_agx_bvh_gen1_child_layout layout;
   require(d12_rt_agx_bvh_gen1_decode_child_layout(packed, &layout),
           "a proved AGX Gen1 child layout decodes for DXR");
   require(layout.child_space_slot == 100 &&
              layout.inner_child_offset == 103 &&
              layout.inner_child_count == 2 && layout.leaf_box_count == 3,
           "DXR receives the exact Gen1 child-space and branch layout");
   require(layout.leaf_count == 17 && layout.leaf_storage_slots == 3 &&
              layout.leaf_slot_capacity == 6 && layout.leaf_type == 26,
           "DXR receives the exact Gen1 leaf layout");

   for (unsigned child = 0; child < 8; ++child) {
      for (unsigned component = 0; component < 6; ++component) {
         require(layout.quantized_bounds[child][component] ==
                    ((child * 11u + component * 7u) & 0x7fu),
                 "DXR preserves every Gen1 quantized component");
      }
   }

   for (uint32_t version = 1; version <= 4; ++version) {
      struct d12_rt_agx_bvh_child_layout dispatched;
      require(d12_rt_agx_bvh_decode_child_layout(version, packed,
                                                  &dispatched),
              "DXR dispatches every BVH version 1-4 to Gen1");
      require(dispatched.format == D12_RT_AGX_BVH_NODE_FORMAT_GEN1 &&
                 memcmp(&dispatched.layout.gen1, &layout, sizeof(layout)) == 0,
              "version-based Gen1 dispatch preserves the decoded layout");
   }

   /* Indexed mode counts boxes rather than taking their maximum end. */
   words[0] |= 1;
   pack_bvh_header(words, packed);
   require(d12_rt_agx_bvh_gen1_decode_child_layout(packed, &layout),
           "a Gen1 indexed-leaf layout decodes for DXR");
   require(layout.leaf_count == 3 && layout.leaf_storage_slots == 1 &&
              layout.inner_child_offset == 101,
           "DXR applies the exact Gen1 indexed-leaf sizing rule");

   /* The two endpoint controls are special encodings, not ordinary counts. */
   words[0] &= ~UINT64_C(1);
   words[7] = (words[7] & ~UINT64_C(0x3f));
   pack_bvh_header(words, packed);
   require(d12_rt_agx_bvh_gen1_decode_child_layout(packed, &layout) &&
              layout.inner_child_count == 8 && layout.leaf_box_count == 0 &&
              layout.leaf_count == 0 && layout.inner_child_offset == 100,
           "DXR decodes the Gen1 all-inner endpoint");

   words[7] = (words[7] & ~UINT64_C(0x3f)) | UINT64_C(0x3f);
   pack_bvh_header(words, packed);
   require(d12_rt_agx_bvh_gen1_decode_child_layout(packed, &layout) &&
              layout.inner_child_count == 0 && layout.leaf_box_count == 8,
           "DXR decodes the Gen1 all-leaf endpoint");

   require(!d12_rt_agx_bvh_gen1_decode_child_layout(NULL, &layout),
           "DXR refuses a missing Gen1 header");
   require(!d12_rt_agx_bvh_gen1_decode_child_layout(packed, NULL),
           "DXR refuses a missing Gen1 layout destination");

   /* Ordinary count fields may encode more than eight total children. */
   words[7] = (words[7] & ~UINT64_C(0x3f)) |
              ((UINT64_C(6) << 3) | UINT64_C(7));
   pack_bvh_header(words, packed);
   memset(&layout, 0xa5, sizeof(layout));
   require(!d12_rt_agx_bvh_gen1_decode_child_layout(packed, &layout),
           "DXR rejects an overfull Gen1 branching layout");
   const struct d12_rt_agx_bvh_gen1_child_layout zero = {0};
   require(memcmp(&layout, &zero, sizeof(layout)) == 0,
           "a rejected Gen1 layout leaves no stale addressing data");
}

static void
test_agx_gen5_child_layout(void)
{
   /* Configuration 4; six leaves, four inner slots, three QTBs and 352
    * bytes of VRBs.  The encoded child-space slot is 100. */
   uint64_t words[8] = {
      UINT64_C(0x0011001c), UINT64_C(0x000b0000),
      UINT64_C(0x00020000), UINT64_C(0x00010000),
      UINT64_C(0x00170000), UINT64_C(0x00080000),
      UINT64_C(0x00390064), UINT64_C(0x00000000),
   };
   uint8_t packed[AGX_RT_BVH_GEN5_HEADER_SIZE];
   pack_bvh_header(words, packed);

   struct d12_rt_agx_bvh_gen5_child_layout layout;
   require(d12_rt_agx_bvh_gen5_decode_child_layout(packed, &layout),
           "a proved AGX Gen5 child layout decodes for DXR");
   require(layout.child_space_slot == 100 && layout.leaf_count == 6 &&
              layout.inner_slots == 4 && layout.qtb_count == 3,
           "DXR receives the exact child, leaf, inner and QTB counts");
   require(layout.vrb_size_bytes == 352,
           "DXR receives the exact configuration-4 VRB extent");
   require(layout.child_qtbs_offset_bytes == 6656 &&
              layout.child_hw_leaves_offset_bytes == 6656 &&
              layout.vrbs_offset_bytes == 6848 &&
              layout.lhb_offset_bytes == 7200 &&
              layout.child_leaves_offset_bytes == 7224,
           "DXR receives all five exact child-section offsets");

   struct d12_rt_agx_bvh_child_layout dispatched;
   require(d12_rt_agx_bvh_decode_child_layout(5, packed, &dispatched),
           "DXR dispatches BVH version 5 to Gen5");
   require(dispatched.format == D12_RT_AGX_BVH_NODE_FORMAT_GEN5 &&
              memcmp(&dispatched.layout.gen5, &layout, sizeof(layout)) == 0,
           "version-based Gen5 dispatch preserves the decoded layout");

   static const uint32_t invalid_versions[] = {0, 6, UINT32_MAX};
   for (unsigned i = 0;
        i < sizeof(invalid_versions) / sizeof(invalid_versions[0]); ++i) {
      memset(&dispatched, 0xa5, sizeof(dispatched));
      require(!d12_rt_agx_bvh_decode_child_layout(
                 invalid_versions[i], packed, &dispatched),
              "DXR rejects an unknown BVH node version");
      const struct d12_rt_agx_bvh_child_layout zero_dispatch = {0};
      require(memcmp(&dispatched, &zero_dispatch, sizeof(dispatched)) == 0,
              "an unknown BVH version leaves no stale layout data");
   }
   require(!d12_rt_agx_bvh_decode_child_layout(5, packed, NULL),
           "DXR refuses a missing versioned layout destination");

   memset(&layout, 0xa5, sizeof(layout));
   require(!d12_rt_agx_bvh_gen5_decode_child_layout(NULL, &layout),
           "DXR refuses a missing Gen5 header");
   const struct d12_rt_agx_bvh_gen5_child_layout zero = {0};
   require(memcmp(&layout, &zero, sizeof(layout)) == 0,
           "a missing Gen5 header leaves no stale addressing data");
   require(!d12_rt_agx_bvh_gen5_decode_child_layout(packed, NULL),
           "DXR refuses a missing Gen5 layout destination");

   /* Change C from three occupied leaves to two.  The occupancy sum is now
    * five while the header still owns six leaves, so Apple's accessor returns
    * its sentinel 4.  The DXR wrapper must not expose that as a fourth QTB. */
   words[6] = UINT64_C(0x00290064);
   pack_bvh_header(words, packed);
   memset(&layout, 0xa5, sizeof(layout));
   require(!d12_rt_agx_bvh_gen5_decode_child_layout(packed, &layout),
           "DXR fails closed on Apple's invalid-QTB sentinel");
   require(memcmp(&layout, &zero, sizeof(layout)) == 0,
           "an invalid Gen5 QTB layout leaves no stale addressing data");
   memset(&dispatched, 0xa5, sizeof(dispatched));
   require(!d12_rt_agx_bvh_decode_child_layout(5, packed, &dispatched),
           "version-based DXR dispatch propagates an invalid Gen5 layout");
   const struct d12_rt_agx_bvh_child_layout zero_dispatch = {0};
   require(memcmp(&dispatched, &zero_dispatch, sizeof(dispatched)) == 0,
           "failed version-based dispatch leaves no stale layout data");
}

int
main(void)
{
   test_api_constants();
   test_shader_table_rules();
   test_shader_record_addresses();
   test_dispatch_rules();
   test_build_flags();
   test_prebuild_and_allocation();
   test_shader_identifiers();
   test_records_and_config();
   test_agx3_canonical_update_node();
   test_agx3_update_node_expansion();
   test_agx3_continue_ray_fixed_first_site_witness();
   test_agx3_update_bvh_fixed_witness();
   test_agx_gen1_hw_triangle_leaf();
   test_agx_gen1_child_layout();
   test_agx_gen5_child_layout();

   printf("d12_raytracing_core_tests: PASS (%u checks)\n", checks);
   return 0;
}
