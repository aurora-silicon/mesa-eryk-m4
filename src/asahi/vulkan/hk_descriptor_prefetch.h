/* SPDX-License-Identifier: MIT */
#pragma once

#include "nir.h"

/* One contiguous, immutable-at-execution descriptor span per Vulkan set.
 * A zero size means ordinary shader loads remain in use. */
struct hk_descriptor_prefetch {
   uint32_t offset_B;
   uint32_t size_B;
   uint16_t uniform_half;
};

bool hk_nir_prefetch_descriptors(nir_shader *nir, unsigned set_base,
                                unsigned set_count, unsigned *uniform_end,
                                struct hk_descriptor_prefetch *spans);
