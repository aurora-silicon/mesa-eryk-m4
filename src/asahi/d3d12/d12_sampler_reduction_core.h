/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Which D3D12 filters need a shader-side MINIMUM/MAXIMUM reduction, and what
 * that costs the native sampler state.
 *
 * Keep this free of Windows and Mesa headers.  The D3D12_FILTER bit layout is
 * a fixed public ABI, so the policy is expressible as arithmetic and covered
 * by host tests rather than only by a compiled shader.
 */

#ifndef D12_SAMPLER_REDUCTION_CORE_H
#define D12_SAMPLER_REDUCTION_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* D3D12_FILTER field layout (d3d12.h). */
#define D12_FILTER_TYPE_MASK          UINT32_C(0x3)
#define D12_FILTER_MIN_SHIFT          4u
#define D12_FILTER_MAG_SHIFT          2u
#define D12_FILTER_MIP_SHIFT          0u
#define D12_FILTER_REDUCTION_SHIFT    7u
#define D12_FILTER_REDUCTION_MASK     UINT32_C(0x3)
#define D12_FILTER_ANISOTROPIC_BIT    UINT32_C(0x40)

#define D12_FILTER_TYPE_POINT         0u
#define D12_FILTER_TYPE_LINEAR        1u

#define D12_FILTER_REDUCTION_STANDARD   0u
#define D12_FILTER_REDUCTION_COMPARISON 1u
#define D12_FILTER_REDUCTION_MINIMUM    2u
#define D12_FILTER_REDUCTION_MAXIMUM    3u

enum d12_sampler_reduction {
   D12_SAMPLER_REDUCE_NONE = 0,
   D12_SAMPLER_REDUCE_MINIMUM,
   D12_SAMPLER_REDUCE_MAXIMUM,
};

/* HAL300 / M5 SamplerGen5 reduction-mode values.
 *
 * These are no longer inferred from field width. Apple's Gen5 constructor
 * copies descriptor values 1 and 2 directly to the two-bit field at 39:40,
 * and the macOS 26 Metal SDK names those values Minimum and Maximum. Value
 * zero is weighted-average filtering. A18 Pro remains on the HAL200 sampler
 * and therefore does not have this field. */
enum d12_sampler_gen5_reduction_mode {
   D12_SAMPLER_GEN5_REDUCTION_WEIGHTED_AVERAGE = 0,
   D12_SAMPLER_GEN5_REDUCTION_MINIMUM = 1,
   D12_SAMPLER_GEN5_REDUCTION_MAXIMUM = 2,
};

struct d12_sampler_reduction_policy {
   /* The reduction the shader must perform, or NONE to leave the hardware
    * sample alone. */
   enum d12_sampler_reduction reduction;

   /* True when the native sampler must select a single mip level.  The shader
    * reduction gathers one 2x2 footprint, so the level the gather lands on has
    * to be the level the hardware chose; a fractional level would reduce over
    * a footprint that no filter ever selected. */
   bool force_mip_nearest;

   /* Non-zero only when SamplerGen5 can perform the complete D3D12
    * reduction. The descriptor packer writes this value to bits 39:40 and
    * leaves bit 42 in reduction-mode selection. */
   enum d12_sampler_gen5_reduction_mode native_gen5_mode;
};

static inline uint32_t
d12_filter_min(uint32_t filter)
{
   return (filter >> D12_FILTER_MIN_SHIFT) & D12_FILTER_TYPE_MASK;
}

static inline uint32_t
d12_filter_mag(uint32_t filter)
{
   return (filter >> D12_FILTER_MAG_SHIFT) & D12_FILTER_TYPE_MASK;
}

static inline uint32_t
d12_filter_mip(uint32_t filter)
{
   return (filter >> D12_FILTER_MIP_SHIFT) & D12_FILTER_TYPE_MASK;
}

static inline uint32_t
d12_filter_reduction(uint32_t filter)
{
   return (filter >> D12_FILTER_REDUCTION_SHIFT) & D12_FILTER_REDUCTION_MASK;
}

static inline bool
d12_filter_is_anisotropic(uint32_t filter)
{
   return (filter & D12_FILTER_ANISOTROPIC_BIT) != 0;
}

/*
 * A MINIMUM/MAXIMUM filter reduces over the texel set the equivalent standard
 * filter would weight.  Where that set is a single texel -- every
 * MIN_MAG_MIP_POINT variant -- the reduction is the identity, so those
 * samplers keep the untouched hardware path and cost nothing.
 *
 * COMPARISON is a distinct reduction type and never combines with MIN/MAX.
 */
static inline struct d12_sampler_reduction_policy
d12_sampler_reduction_for_filter(uint32_t filter)
{
   struct d12_sampler_reduction_policy policy = {
      .reduction = D12_SAMPLER_REDUCE_NONE,
      .force_mip_nearest = false,
      .native_gen5_mode = D12_SAMPLER_GEN5_REDUCTION_WEIGHTED_AVERAGE,
   };

   const uint32_t reduction = d12_filter_reduction(filter);
   if (reduction != D12_FILTER_REDUCTION_MINIMUM &&
       reduction != D12_FILTER_REDUCTION_MAXIMUM)
      return policy;

   const bool wide_footprint =
      d12_filter_is_anisotropic(filter) ||
      d12_filter_min(filter) == D12_FILTER_TYPE_LINEAR ||
      d12_filter_mag(filter) == D12_FILTER_TYPE_LINEAR;
   if (!wide_footprint)
      return policy;

   policy.reduction = reduction == D12_FILTER_REDUCTION_MINIMUM
                         ? D12_SAMPLER_REDUCE_MINIMUM
                         : D12_SAMPLER_REDUCE_MAXIMUM;
   policy.force_mip_nearest = true;
   return policy;
}

/*
 * M5 can execute MINIMUM/MAXIMUM in SamplerGen5, but Metal's public contract
 * says that reductionMode is ignored if min, mag, or mip is nearest. D3D12
 * exposes mixed filter spellings, so only the all-linear (including the
 * anisotropic spelling) subset maps completely to hardware. Mixed filters
 * retain the shader policy above; silently selecting native mode for them
 * would produce a weighted average for one of the two LOD regimes.
 */
static inline struct d12_sampler_reduction_policy
d12_sampler_reduction_for_filter_gen5(uint32_t filter)
{
   struct d12_sampler_reduction_policy policy =
      d12_sampler_reduction_for_filter(filter);

   if (policy.reduction == D12_SAMPLER_REDUCE_NONE)
      return policy;

   const bool all_linear =
      d12_filter_is_anisotropic(filter) ||
      (d12_filter_min(filter) == D12_FILTER_TYPE_LINEAR &&
       d12_filter_mag(filter) == D12_FILTER_TYPE_LINEAR &&
       d12_filter_mip(filter) == D12_FILTER_TYPE_LINEAR);
   if (!all_linear)
      return policy;

   policy.native_gen5_mode =
      policy.reduction == D12_SAMPLER_REDUCE_MINIMUM
         ? D12_SAMPLER_GEN5_REDUCTION_MINIMUM
         : D12_SAMPLER_GEN5_REDUCTION_MAXIMUM;
   policy.reduction = D12_SAMPLER_REDUCE_NONE;
   policy.force_mip_nearest = false;
   return policy;
}

#ifdef __cplusplus
}
#endif

#endif
