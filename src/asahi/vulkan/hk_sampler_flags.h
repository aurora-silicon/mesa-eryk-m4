/* SPDX-License-Identifier: MIT */
#pragma once

enum hk_minmax_sampler_flags {
   HK_MINMAX_MAG_LINEAR = 1u << 16,
   HK_MINMAX_MIN_LINEAR = 1u << 17,
   HK_MINMAX_MIP_LINEAR = 1u << 18,
   HK_MINMAX_UNNORMALIZED = 1u << 19,
   HK_MINMAX_SEAMFUL = 1u << 20,
   /* HAL200 sampler bits 54:55 are reserved, so ordinary descriptors have
    * reduction mode zero at the same location. */
   HK_MINMAX_MODE_SHIFT = 22,
   HK_MINMAX_ANISOTROPY_SHIFT = 24,
   /* Native compare function retained when min-LOD uses software state. */
   HK_MINMAX_COMPARE_SHIFT = 29,
};
