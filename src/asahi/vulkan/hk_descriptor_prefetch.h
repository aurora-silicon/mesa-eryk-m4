/* SPDX-License-Identifier: MIT */
#pragma once

#include "nir.h"
#include "asahi/lib/agx_chip.h"
#include "util/u_debug.h"

/* Enable the bounded input-loader path on G15S. Keep the existing
 * diagnostic overrides and preserve other architectures' defaults. Compilation
 * and pipeline UUIDs must use the same policy. */
static inline bool
hk_ubo_prefetch_enabled(enum agx_chip chip)
{
   return debug_get_bool_option("HK_M4_PREFETCH_UBOS", chip == AGX_CHIP_G15S);
}

static inline bool
hk_ubo_prefetch_variant_enabled(enum agx_chip chip)
{
   return debug_get_bool_option("HK_M4_PREFETCH_VARIANT", chip == AGX_CHIP_G15S);
}

static inline bool
hk_plain_samplers_enabled(enum agx_chip chip)
{
   return debug_get_bool_option("HK_M4_PLAIN_SAMPLERS", chip == AGX_CHIP_G15S);
}

static inline bool
hk_vertex_ubo_prefetch_enabled(enum agx_chip chip)
{
   return chip == AGX_CHIP_G15S &&
          debug_get_bool_option("HK_M3_VERTEX_PREFETCH_UBOS", true);
}

/* Reuse the ordinary descriptor predicates for the hardware vertex variant.
 * Scaled formats, special LOD handling and mutable bindings keep the general
 * executable. Other chips retain their existing policy. */
static inline bool
hk_vertex_plain_samplers_enabled(enum agx_chip chip)
{
   return chip == AGX_CHIP_G15S &&
          debug_get_bool_option("HK_M3_VERTEX_PLAIN_SAMPLERS", true);
}

static inline bool
hk_vertex_uniform_cse_enabled(enum agx_chip chip)
{
   return agx_chip_uses_g15_g16_shader_abi(chip) &&
          debug_get_bool_option("AGX_NATIVE_VERTEX_UNIFORM_CSE", chip == AGX_CHIP_G15S);
}

static inline bool
hk_vertex_norm8_enabled(enum agx_chip chip)
{
   return agx_chip_uses_g15_g16_shader_abi(chip) &&
          debug_get_bool_option("HK_NATIVE_VERTEX_NORM8", false);
}

static inline bool
hk_short_sample_loops_enabled(enum agx_chip chip)
{
   return agx_chip_uses_g15_g16_shader_abi(chip) &&
          debug_get_bool_option("HK_NATIVE_SHORT_SAMPLE_LOOPS", chip == AGX_CHIP_G15S);
}

/* Cache resource-program structure, while patching each draw's GPU addresses.
 * Other chips retain the opt-in policy until separately qualified. */
static inline bool
hk_ioto_templates_enabled(enum agx_chip chip)
{
   return debug_get_bool_option("HK_M4_IOTO_TEMPLATES", chip == AGX_CHIP_G15S);
}

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
                         unsigned *uniform_end, unsigned uniform_limit,
                         struct hk_ubo_prefetch *loads,
                         unsigned *load_count, hk_ubo_prefetch_allowed allowed,
                         void *data, bool direct);
