/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_sampler_reduction_core.h"

#include <stdio.h>
#include <stdlib.h>

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

static uint32_t
basic(uint32_t min, uint32_t mag, uint32_t mip, uint32_t reduction)
{
   return (min << D12_FILTER_MIN_SHIFT) | (mag << D12_FILTER_MAG_SHIFT) |
          (mip << D12_FILTER_MIP_SHIFT) |
          (reduction << D12_FILTER_REDUCTION_SHIFT);
}

static uint32_t
anisotropic(uint32_t reduction)
{
   return D12_FILTER_ANISOTROPIC_BIT |
          basic(D12_FILTER_TYPE_LINEAR, D12_FILTER_TYPE_LINEAR,
                D12_FILTER_TYPE_LINEAR, reduction);
}

/* The published D3D12_FILTER values this policy must agree with. */
#define D3D12_FILTER_MIN_MAG_MIP_POINT                    0x0
#define D3D12_FILTER_MIN_MAG_MIP_LINEAR                   0x15
#define D3D12_FILTER_ANISOTROPIC                          0x55
#define D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR        0x95
#define D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT            0x100
#define D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR           0x115
#define D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT     0x114
#define D3D12_FILTER_MINIMUM_ANISOTROPIC                  0x155
#define D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT            0x180
#define D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR           0x195
#define D3D12_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT 0x184
#define D3D12_FILTER_MAXIMUM_ANISOTROPIC                  0x1d5

static void
test_field_decode_matches_published_values(void)
{
   require(basic(D12_FILTER_TYPE_LINEAR, D12_FILTER_TYPE_LINEAR,
                 D12_FILTER_TYPE_LINEAR, D12_FILTER_REDUCTION_STANDARD) ==
              D3D12_FILTER_MIN_MAG_MIP_LINEAR,
           "basic linear encoding matches D3D12_FILTER_MIN_MAG_MIP_LINEAR");
   require(anisotropic(D12_FILTER_REDUCTION_STANDARD) ==
              D3D12_FILTER_ANISOTROPIC,
           "anisotropic encoding matches D3D12_FILTER_ANISOTROPIC");
   require(basic(D12_FILTER_TYPE_LINEAR, D12_FILTER_TYPE_LINEAR,
                 D12_FILTER_TYPE_LINEAR, D12_FILTER_REDUCTION_MINIMUM) ==
              D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR,
           "minimum linear encoding matches its published value");
   require(anisotropic(D12_FILTER_REDUCTION_MAXIMUM) ==
              D3D12_FILTER_MAXIMUM_ANISOTROPIC,
           "maximum anisotropic encoding matches its published value");

   require(d12_filter_reduction(D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR) ==
              D12_FILTER_REDUCTION_COMPARISON,
           "comparison filters decode as the comparison reduction");
   require(d12_filter_is_anisotropic(D3D12_FILTER_MINIMUM_ANISOTROPIC),
           "minimum anisotropic decodes as anisotropic");
}

static void
test_standard_and_comparison_never_reduce(void)
{
   const uint32_t filters[] = {
      D3D12_FILTER_MIN_MAG_MIP_POINT,
      D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      D3D12_FILTER_ANISOTROPIC,
      D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
   };

   for (unsigned i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
      struct d12_sampler_reduction_policy policy =
         d12_sampler_reduction_for_filter(filters[i]);
      require(policy.reduction == D12_SAMPLER_REDUCE_NONE,
              "a standard or comparison filter never reduces");
      require(!policy.force_mip_nearest,
              "a filter that does not reduce keeps its mip filter");
   }
}

/* A single-texel footprint reduces to itself, so those samplers must stay on
 * the untouched hardware path rather than paying for gathers. */
static void
test_point_reduction_is_the_identity(void)
{
   const uint32_t filters[] = {
      D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT,
      D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT,
   };

   for (unsigned i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
      struct d12_sampler_reduction_policy policy =
         d12_sampler_reduction_for_filter(filters[i]);
      require(policy.reduction == D12_SAMPLER_REDUCE_NONE,
              "an all-point MINIMUM/MAXIMUM filter needs no shader work");
      require(!policy.force_mip_nearest,
              "an all-point reduction leaves the mip filter alone");
   }
}

static void
test_wide_footprints_reduce(void)
{
   struct {
      uint32_t filter;
      enum d12_sampler_reduction expected;
      const char *name;
   } cases[] = {
      {D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D12_SAMPLER_REDUCE_MINIMUM,
       "minimum trilinear"},
      {D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT,
       D12_SAMPLER_REDUCE_MINIMUM, "minimum bilinear"},
      {D3D12_FILTER_MINIMUM_ANISOTROPIC, D12_SAMPLER_REDUCE_MINIMUM,
       "minimum anisotropic"},
      {D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D12_SAMPLER_REDUCE_MAXIMUM,
       "maximum trilinear"},
      {D3D12_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
       D12_SAMPLER_REDUCE_MAXIMUM, "maximum with only mag linear"},
      {D3D12_FILTER_MAXIMUM_ANISOTROPIC, D12_SAMPLER_REDUCE_MAXIMUM,
       "maximum anisotropic"},
   };

   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      struct d12_sampler_reduction_policy policy =
         d12_sampler_reduction_for_filter(cases[i].filter);
      require(policy.reduction == cases[i].expected, cases[i].name);
      require(policy.force_mip_nearest,
              "a reducing sampler selects a single mip level");
   }
}

/* Reduction must be decided by the reduction field alone, never inferred from
 * a filter value that happens to share bits. */
static void
test_every_encodable_filter(void)
{
   for (uint32_t reduction = 0; reduction <= 3; reduction++) {
      for (uint32_t min = 0; min <= 1; min++) {
         for (uint32_t mag = 0; mag <= 1; mag++) {
            for (uint32_t mip = 0; mip <= 1; mip++) {
               for (unsigned aniso = 0; aniso <= 1; aniso++) {
                  uint32_t filter = basic(min, mag, mip, reduction);
                  if (aniso)
                     filter |= D12_FILTER_ANISOTROPIC_BIT;

                  struct d12_sampler_reduction_policy policy =
                     d12_sampler_reduction_for_filter(filter);
                  const bool reducing =
                     (reduction == D12_FILTER_REDUCTION_MINIMUM ||
                      reduction == D12_FILTER_REDUCTION_MAXIMUM) &&
                     (aniso || min == D12_FILTER_TYPE_LINEAR ||
                      mag == D12_FILTER_TYPE_LINEAR);

                  require((policy.reduction != D12_SAMPLER_REDUCE_NONE) ==
                             reducing,
                          "reduction is decided by the reduction field and "
                          "footprint width only");
                  require(policy.force_mip_nearest == reducing,
                          "mip selection is forced exactly when reducing");
                  if (reducing) {
                     require((policy.reduction ==
                              D12_SAMPLER_REDUCE_MINIMUM) ==
                                (reduction == D12_FILTER_REDUCTION_MINIMUM),
                             "the reduction direction round-trips");
                  }
               }
            }
         }
      }
   }
}

int
main(void)
{
   test_field_decode_matches_published_values();
   test_standard_and_comparison_never_reduce();
   test_point_reduction_is_the_identity();
   test_wide_footprints_reduce();
   test_every_encodable_filter();

   printf("d12_sampler_reduction_core_tests: PASS (%u checks)\n", checks);
   return 0;
}
