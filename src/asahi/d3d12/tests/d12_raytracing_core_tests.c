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
           "record count is size divided by stride");

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

   /* Whole records only: a partial trailing record would be indexable. */
   require(!d12_rt_shader_table_valid(tbl(0x40000, 160, 64),
                                      D12_RT_TABLE_STRIDED),
           "a table with a partial trailing record is refused");

   require(!d12_rt_shader_table_valid(
              tbl(0x40000, 8192, D12_RT_MAX_SHADER_RECORD_STRIDE + 32),
              D12_RT_TABLE_STRIDED),
           "a stride beyond the API maximum is refused");

   /* The ray-generation table is one record and cannot carry a stride. */
   require(!d12_rt_shader_table_valid(tbl(0x40000, 64, 32),
                                      D12_RT_TABLE_RAY_GENERATION),
           "a ray-generation record with a stride is refused");
   require(d12_rt_shader_table_valid(tbl(0x40000, 64, 0),
                                     D12_RT_TABLE_RAY_GENERATION),
           "a ray-generation record without a stride is legal");
   require(d12_rt_shader_table_records(tbl(0x40000, 64, 0)) == 0,
           "a strideless record reports no addressable count");

   /* A strided table needs a stride. */
   require(!d12_rt_shader_table_valid(tbl(0x40000, 64, 0),
                                      D12_RT_TABLE_STRIDED),
           "a strided table without a stride is refused");

   /* Address-space overflow. */
   require(!d12_rt_shader_table_valid(tbl(UINT64_MAX - 32, 128, 64),
                                      D12_RT_TABLE_STRIDED),
           "a table that wraps the address space is refused");

   require(!d12_rt_shader_table_valid(NULL, D12_RT_TABLE_STRIDED),
           "a null table is refused");
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

   /* Zero is legal only if it is zero in every dimension: a zero-area
    * dispatch is a no-op, a partially zero one is a mistake. */
   struct d12_rt_dispatch_rays empty = dispatch;
   empty.width = empty.height = empty.depth = 0;
   require(d12_rt_dispatch_valid(&empty), "a fully zero dispatch is legal");

   struct d12_rt_dispatch_rays partial = dispatch;
   partial.height = 0;
   require(!d12_rt_dispatch_valid(&partial),
           "a dispatch zero in only one dimension is refused");

   /* The thread limit is a product, so three individually legal dimensions
    * can still exceed it. */
   struct d12_rt_dispatch_rays huge = dispatch;
   huge.width = 65536;
   huge.height = 65536;
   huge.depth = 2;
   require(!d12_rt_dispatch_valid(&huge),
           "a dispatch whose thread product exceeds the maximum is refused");

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

int
main(void)
{
   test_api_constants();
   test_shader_table_rules();
   test_dispatch_rules();
   test_build_flags();
   test_prebuild_and_allocation();
   test_shader_identifiers();
   test_records_and_config();

   printf("d12_raytracing_core_tests: PASS (%u checks)\n", checks);
   return 0;
}
