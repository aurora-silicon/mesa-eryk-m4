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

static void
pack_gen5_header(const uint64_t words[8], uint8_t packed[64])
{
   for (unsigned word = 0; word < 8; ++word) {
      for (unsigned byte = 0; byte < 8; ++byte)
         packed[(word * 8) + byte] = (uint8_t)(words[word] >> (byte * 8));
   }
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
   pack_gen5_header(words, packed);

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

   require(!d12_rt_agx_bvh_gen5_decode_child_layout(NULL, &layout),
           "DXR refuses a missing Gen5 header");
   require(!d12_rt_agx_bvh_gen5_decode_child_layout(packed, NULL),
           "DXR refuses a missing Gen5 layout destination");

   /* Change C from three occupied leaves to two.  The occupancy sum is now
    * five while the header still owns six leaves, so Apple's accessor returns
    * its sentinel 4.  The DXR wrapper must not expose that as a fourth QTB. */
   words[6] = UINT64_C(0x00290064);
   pack_gen5_header(words, packed);
   memset(&layout, 0xa5, sizeof(layout));
   require(!d12_rt_agx_bvh_gen5_decode_child_layout(packed, &layout),
           "DXR fails closed on Apple's invalid-QTB sentinel");
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
   test_agx_gen5_child_layout();

   printf("d12_raytracing_core_tests: PASS (%u checks)\n", checks);
   return 0;
}
