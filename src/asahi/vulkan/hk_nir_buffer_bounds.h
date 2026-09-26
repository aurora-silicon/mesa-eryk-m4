/* SPDX-License-Identifier: MIT */
#ifndef HK_NIR_BUFFER_BOUNDS_H
#define HK_NIR_BUFFER_BOUNDS_H

#include "compiler/nir/nir_builder.h"

static inline nir_def *
hk_nir_buffer_offset_in_bounds(nir_builder *b, nir_def *offset,
                               nir_def *bound, unsigned load_size)
{
   assert(offset->bit_size == 32 && bound->bit_size == 32 && load_size > 0);
   /* offset + load_size <= bound, without overflowing the address offset.
    * Share the adjusted descriptor limit across independent load offsets. */
   nir_def *last_start = nir_usub_sat(b, bound, nir_imm_int(b, load_size - 1));
   return nir_ult(b, offset, last_start);
}

bool hk_nir_coalesce_bounded(nir_shader *nir);

#endif
