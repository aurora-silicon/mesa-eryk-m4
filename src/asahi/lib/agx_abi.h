/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* See compiler/README.md for the ABI */

#define AGX_ABI_VIN_ATTRIB(i)           (2 * (8 + i))
#define AGX_ABI_VIN_VERTEX_ID_ZERO_BASE (2 * 4)
#define AGX_ABI_VIN_VERTEX_ID           (2 * 5)
#define AGX_ABI_VIN_INSTANCE_ID         (2 * 6)

#define AGX_ABI_FIN_SAMPLE_MASK (2)

#define AGX_ABI_FOUT_SAMPLE_MASK   (2)
#define AGX_ABI_FOUT_Z             (4)
#define AGX_ABI_FOUT_S             (6)
#define AGX_ABI_FOUT_WRITE_SAMPLES (7)
#define AGX_ABI_FOUT_COLOUR(rt)    (2 * (4 + (4 * rt)))

#define AGX_ABI_VUNI_VBO_BASE(vbo) (4 * (vbo))
/* HAL200 image access uses halves 0..15: texture/sampler heap pointers,
 * image-coordinate routing, and its clamp. Keep the complete range disjoint
 * from vertex bases/clamps, including in shaders with storage-image writes.
 * Shift the ordinary vertex ABI together, including linked prologs.
 */
#define AGX_ABI_VUNI_NATIVE_PREFIX 16
#define AGX_ABI_VUNI_VBO_CLAMP(nr_vbos, vbo)                                   \
   (AGX_ABI_VUNI_VBO_BASE(nr_vbos) + 2 * (vbo))

#define AGX_ABI_VUNI_FIRST_VERTEX(nr_vbos)                                     \
   AGX_ABI_VUNI_VBO_CLAMP(nr_vbos, nr_vbos)

#define AGX_ABI_VUNI_BASE_INSTANCE(nr_vbos)                                    \
   (AGX_ABI_VUNI_FIRST_VERTEX(nr_vbos) + 2)

#define AGX_ABI_VUNI_DRAW_ID(nr_vbos) (AGX_ABI_VUNI_FIRST_VERTEX(nr_vbos) + 4)

#define AGX_ABI_VUNI_VERTEX_PARAMS(nr_vbos)                                    \
   (AGX_ABI_VUNI_FIRST_VERTEX(nr_vbos) + 8)

#define AGX_ABI_VUNI_COUNT_GL(nr_vbos, sw)                                     \
   (sw ? (AGX_ABI_VUNI_VERTEX_PARAMS(nr_vbos) + 4)                             \
       : (AGX_ABI_VUNI_BASE_INSTANCE(nr_vbos) + 2))

#define AGX_ABI_VUNI_COUNT_VK(nr_vbos)                                         \
   ALIGN_POT(AGX_ABI_VUNI_VERTEX_PARAMS(nr_vbos) + 4, 4)

/* Native texture/sampler heap pointers occupy halves 0..7 in fragments too.
 * Shift the entire ordinary fragment ABI, including blend constants and root
 * pointers, so textured constant-colour blending cannot alias those heaps.
 */
/* Native prefix: texture/sampler pointers at0..7, query address at8..11,
 * and image-coordinate clamp at12..15. Ordinary fragment data follows. */
#define AGX_ABI_FUNI_NATIVE_PREFIX 16
#define AGX_ABI_FUNI_NATIVE_STAT_QUERY 8
#define AGX_ABI_FUNI_EMRT_HEAP (0)
#define AGX_ABI_FUNI_BLEND_R   (AGX_ABI_FUNI_EMRT_HEAP + 4)
#define AGX_ABI_FUNI_BLEND_G   (AGX_ABI_FUNI_BLEND_R + 2)
#define AGX_ABI_FUNI_BLEND_B   (AGX_ABI_FUNI_BLEND_R + 4)
#define AGX_ABI_FUNI_BLEND_A   (AGX_ABI_FUNI_BLEND_R + 6)
#define AGX_ABI_FUNI_ROOT      (12)
#define AGX_ABI_FUNI_COUNT     (16)

/* This address is in our reservation, and can be
 * addressed with only small integers in the low/high. That lets us do some
 * robustness optimization even without soft fault.
 */
#define AGX_ZERO_PAGE_ADDRESS (((uint64_t)1) << 32)
#define AGX_ZERO_PAGE_SIZE    (16384)

#define AGX_SCRATCH_PAGE_ADDRESS (AGX_ZERO_PAGE_ADDRESS + AGX_ZERO_PAGE_SIZE)

#define AGX_ABI_G17P_TEXTURE_POINTER_HALFS 8
#define AGX_ABI_AGX3_THREADGROUP_BASE_HALF 8
#define AGX_ABI_AGX3_PRECOMP_ARG_HALFS 12
