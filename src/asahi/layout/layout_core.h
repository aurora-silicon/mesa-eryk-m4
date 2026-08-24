/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Represents the dimensions of a single tile. Width and height are in units
 * of elements, not pixels. Both dimensions are powers of two.
 */
struct ail_tile {
   unsigned width_el, height_el;
};

static inline unsigned
ail_effective_width_sa(unsigned width_px, unsigned sample_count_sa)
{
   return width_px * (sample_count_sa >= 4 ? 2 : 1);
}

static inline unsigned
ail_effective_height_sa(unsigned height_px, unsigned sample_count_sa)
{
   return height_px * (sample_count_sa >= 8 ? 4 : sample_count_sa >= 2 ? 2 : 1);
}

struct ail_tile ail_get_max_tile_size(unsigned blocksize_B);

/*
 * Grounded M5 layout policy primitives.  They are intentionally separate
 * from ail_make_miptree until every API frontend carries an explicit layout
 * generation; the measured M5 policy must not be inferred for M3/M4 or
 * silently applied to admitted AGX2 parts.
 */
uint64_t ail_m5_layer_stride_B(uint64_t unaligned_size_B, unsigned layers,
                              bool has_compression_metadata);

/* Compression metadata uses 16x16 tiles through 8 B/px and 16x8 at 16 B/px. */
struct ail_tile ail_m5_compression_tile_size(unsigned blocksize_B);

#ifdef __cplusplus
}
#endif
