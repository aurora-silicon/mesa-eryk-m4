/* SPDX-License-Identifier: MIT */
#pragma once

#include "nir.h"

/* One contiguous, immutable-at-execution descriptor span per Vulkan set.
 * A zero size means ordinary shader loads remain in use. */
struct hk_descriptor_prefetch {
   uint32_t offset_B;
   uint32_t size_B;
   uint16_t uniform_half;
   uint16_t set;
};

bool hk_nir_prefetch_descriptors(nir_shader *nir, unsigned set_base,
                                unsigned set_count, unsigned *uniform_end,
                                struct hk_descriptor_prefetch *spans);

#define HK_DESCRIPTOR_PREFETCH_MAX 32
bool hk_nir_prefetch_descriptors_compact(nir_shader *nir, unsigned set_base,
   unsigned set_count, unsigned *uniform_end,
   struct hk_descriptor_prefetch spans[HK_DESCRIPTOR_PREFETCH_MAX],
   unsigned *span_count);

#define HK_PLAIN_SAMPLER_MAX 8
struct hk_plain_sampler {
   uint32_t descriptor_offset_B;
   uint16_t set;
};
bool hk_nir_specialize_plain_samplers(nir_shader *nir, unsigned set_base,
   unsigned set_count, struct hk_plain_sampler *samplers, unsigned *count);

/* A bounded optimization budget, not an admission limit: unmatched loads keep
 * their original shader path. Each entry reserves an aligned uniform quad. */
#define HK_UBO_PREFETCH_MAX (512u / 4u)
/* Dynamic buffer descriptors live in the immutable per-draw root table. */
#define HK_UBO_PREFETCH_ROOT_SET UINT16_MAX
struct hk_ubo_prefetch {
   uint32_t descriptor_offset_B;
   uint32_t offset_B;
   uint16_t set;
   uint16_t uniform_half;
   uint16_t size_B;
   uint16_t direct_half;
};
typedef bool (*hk_ubo_prefetch_allowed)(void *data, unsigned set, unsigned offset);
bool hk_nir_prefetch_ubos(nir_shader *nir, unsigned set_base, unsigned set_count,
                         unsigned *uniform_end, struct hk_ubo_prefetch *loads,
                         unsigned *load_count, hk_ubo_prefetch_allowed allowed,
                         void *data);
