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

struct d12_sampler_reduction_policy {
   /* The reduction the shader must perform, or NONE to leave the hardware
    * sample alone. */
   enum d12_sampler_reduction reduction;

   /* True when the native sampler must select a single mip level.  The shader
    * reduction gathers one 2x2 footprint, so the level the gather lands on has
    * to be the level the hardware chose; a fractional level would reduce over
    * a footprint that no filter ever selected. */
   bool force_mip_nearest;
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

#ifdef __cplusplus
}
#endif

#endif
