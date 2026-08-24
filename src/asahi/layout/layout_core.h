/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

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

#ifdef __cplusplus
}
#endif
