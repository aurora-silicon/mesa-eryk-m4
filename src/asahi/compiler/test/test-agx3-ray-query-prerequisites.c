/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_nir_ray_query.h"

#include <stdio.h>
#include <string.h>

#include "asahi/lib/agx_rt_result.h"
#include "util/ralloc.h"

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

struct result_vector {
   nir_ray_query_value value;
   uint16_t offset;
   uint8_t components;
   uint8_t storage_bits;
   enum agx_nir_ray_query_result_kind kind;
};

static const struct result_vector result_vectors[] = {
   {nir_ray_query_value_intersection_type, AGX_RT_HIT_OFF_TYPE, 1, 32,
    AGX_NIR_RAY_QUERY_RESULT_U32},
   {nir_ray_query_value_intersection_t, AGX_RT_HIT_OFF_T, 1, 32,
    AGX_NIR_RAY_QUERY_RESULT_F32},
   {nir_ray_query_value_intersection_instance_custom_index,
    AGX_RT_HIT_OFF_USER_INSTANCE_ID, 1, 32, AGX_NIR_RAY_QUERY_RESULT_U32},
   {nir_ray_query_value_intersection_instance_id, AGX_RT_HIT_OFF_INSTANCE_ID, 1,
    32, AGX_NIR_RAY_QUERY_RESULT_U32},
   {nir_ray_query_value_intersection_geometry_index, AGX_RT_HIT_OFF_GEOMETRY_ID,
    1, 32, AGX_NIR_RAY_QUERY_RESULT_U32},
   {nir_ray_query_value_intersection_primitive_index,
    AGX_RT_HIT_OFF_PRIMITIVE_ID, 1, 32, AGX_NIR_RAY_QUERY_RESULT_U32},
   {nir_ray_query_value_intersection_barycentrics, AGX_RT_HIT_OFF_BARYCENTRICS,
    2, 32, AGX_NIR_RAY_QUERY_RESULT_F32},
   {nir_ray_query_value_intersection_front_face, AGX_RT_HIT_OFF_FRONT_FACE, 1,
    8, AGX_NIR_RAY_QUERY_RESULT_BOOL},
};

struct intrinsic_fixture {
   nir_shader *shader;
   nir_intrinsic_instr *intrinsic;
};

static struct intrinsic_fixture
make_load(nir_ray_query_value value, bool committed, uint32_t column)
{
   static const nir_shader_compiler_options options = {0};
   nir_shader *shader = nir_shader_create(NULL, MESA_SHADER_COMPUTE, &options);
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(shader, nir_intrinsic_rq_load);
   nir_intrinsic_set_ray_query_value(load, value);
   nir_intrinsic_set_committed(load, committed);
   nir_intrinsic_set_column(load, column);
   return (struct intrinsic_fixture){shader, load};
}

static void
test_supported_contracts(void)
{
   static const struct {
      enum agx_chip chip;
      enum agx_isa_encoding encoding;
      uint8_t intersect_size;
      uint8_t continue_size;
   } chips[] = {
      {AGX_CHIP_G15G, AGX_ISA_ENCODING_AGX3B, 12, 6},
      {AGX_CHIP_G15S, AGX_ISA_ENCODING_AGX3B, 12, 6},
      {AGX_CHIP_G15D, AGX_ISA_ENCODING_AGX3B, 12, 6},
      {AGX_CHIP_G16G, AGX_ISA_ENCODING_AGX3B, 12, 6},
      {AGX_CHIP_G16S, AGX_ISA_ENCODING_AGX3B, 12, 6},
      {AGX_CHIP_G17P, AGX_ISA_ENCODING_AGX3B, 12, 6},
      {AGX_CHIP_G17G, AGX_ISA_ENCODING_AGX3C, 14, 8},
      {AGX_CHIP_G17S, AGX_ISA_ENCODING_AGX3C, 14, 8},
   };

   for (unsigned c = 0; c < sizeof(chips) / sizeof(chips[0]); ++c) {
      for (unsigned v = 0;
           v < sizeof(result_vectors) / sizeof(result_vectors[0]); ++v) {
         struct intrinsic_fixture load =
            make_load(result_vectors[v].value, true, 0);
         struct agx_nir_ray_query_committed_prerequisites out;
         memset(&out, 0xa5, sizeof(out));
         CHECK(agx_nir_ray_query_committed_load_prerequisites(
            chips[c].chip, load.intrinsic, &out));
         CHECK(out.encoding == chips[c].encoding);
         CHECK(out.query_state_size == AGX_RT_RAY_QUERY_STATE_SIZE);
         CHECK(out.intersect_ray_witness_size == chips[c].intersect_size);
         CHECK(out.continue_ray_first_site_witness_size ==
               chips[c].continue_size);
         CHECK(out.inc_stk_witness_size == AGX3_INC_STK_FIXED_WITNESS_LENGTH);
         CHECK(out.result.byte_offset == result_vectors[v].offset);
         CHECK(out.result.components == result_vectors[v].components);
         CHECK(out.result.storage_bit_size == result_vectors[v].storage_bits);
         CHECK(out.result.kind == result_vectors[v].kind);
         ralloc_free(load.shader);
      }
   }
}

static bool
is_supported_value(nir_ray_query_value value)
{
   for (unsigned i = 0; i < sizeof(result_vectors) / sizeof(result_vectors[0]);
        ++i) {
      if (result_vectors[i].value == value)
         return true;
   }

   return false;
}

static void
check_failed_transaction(enum agx_chip chip, nir_ray_query_value value,
                         bool committed, uint32_t column)
{
   struct intrinsic_fixture load = make_load(value, committed, column);
   struct agx_nir_ray_query_committed_prerequisites out;
   struct agx_nir_ray_query_committed_prerequisites zero = {0};
   memset(&out, 0xa5, sizeof(out));
   CHECK(!agx_nir_ray_query_committed_load_prerequisites(chip, load.intrinsic,
                                                         &out));
   CHECK(memcmp(&out, &zero, sizeof(out)) == 0);
   ralloc_free(load.shader);
}

static void
test_fail_closed_boundaries(void)
{
   static const enum agx_chip unwitnessed[] = {
      AGX_CHIP_G13G, AGX_CHIP_G14G, AGX_CHIP_G15C, AGX_CHIP_G16P,
      AGX_CHIP_G16C, AGX_CHIP_G17A, AGX_CHIP_G17C, AGX_CHIP_UNKNOWN,
   };

   for (unsigned i = 0; i < sizeof(unwitnessed) / sizeof(unwitnessed[0]); ++i)
      check_failed_transaction(unwitnessed[i],
                               nir_ray_query_value_intersection_type, true, 0);

   for (nir_ray_query_value value = nir_ray_query_value_intersection_type;
        value <= nir_ray_query_value_intersection_triangle_vertex_positions;
        value++) {
      if (!is_supported_value(value))
         check_failed_transaction(AGX_CHIP_G17P, value, true, 0);
   }

   for (unsigned i = 0; i < sizeof(result_vectors) / sizeof(result_vectors[0]);
        ++i)
      check_failed_transaction(AGX_CHIP_G17S, result_vectors[i].value, true, 1);

   check_failed_transaction(AGX_CHIP_G17P,
                            nir_ray_query_value_intersection_type, false, 0);

   struct agx_nir_ray_query_committed_prerequisites out;
   struct agx_nir_ray_query_committed_prerequisites zero = {0};
   memset(&out, 0xa5, sizeof(out));
   CHECK(!agx_nir_ray_query_committed_load_prerequisites(AGX_CHIP_G17P, NULL,
                                                         &out));
   CHECK(memcmp(&out, &zero, sizeof(out)) == 0);

   static const nir_shader_compiler_options options = {0};
   nir_shader *shader = nir_shader_create(NULL, MESA_SHADER_COMPUTE, &options);
   nir_intrinsic_instr *proceed =
      nir_intrinsic_instr_create(shader, nir_intrinsic_rq_proceed);
   memset(&out, 0xa5, sizeof(out));
   CHECK(!agx_nir_ray_query_committed_load_prerequisites(AGX_CHIP_G17P, proceed,
                                                         &out));
   CHECK(memcmp(&out, &zero, sizeof(out)) == 0);
   ralloc_free(shader);

   struct intrinsic_fixture load =
      make_load(nir_ray_query_value_intersection_type, true, 0);
   CHECK(!agx_nir_ray_query_committed_load_prerequisites(AGX_CHIP_G17P,
                                                         load.intrinsic, NULL));
   ralloc_free(load.shader);
}

int
main(void)
{
   test_supported_contracts();
   test_fail_closed_boundaries();

   if (failures != 0) {
      fprintf(stderr, "agx3_ray_query_prerequisites: %u/%u checks failed\n",
              failures, checks);
      return 1;
   }

   printf("agx3_ray_query_prerequisites: %u exact committed-result and "
          "witness-gate checks passed\n",
          checks);
   return 0;
}
