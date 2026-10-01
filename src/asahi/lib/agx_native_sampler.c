/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_native_sampler.h"

#include "util/u_math.h"

#include <math.h>
#include <string.h>

bool
agx_sampler_reduction_supported(enum agx_sampler_generation generation,
                               enum agx_native_sampler_reduction_mode reduction_mode)
{
   if (reduction_mode == AGX_SAMPLER_REDUCTION_WEIGHTED_AVERAGE)
      return generation != AGX_SAMPLER_GENERATION_UNKNOWN;

   return generation == AGX_SAMPLER_GENERATION_GEN5 &&
          (reduction_mode == AGX_SAMPLER_REDUCTION_MINIMUM ||
           reduction_mode == AGX_SAMPLER_REDUCTION_MAXIMUM);
}

static bool
agx_sampler_contract_valid(enum agx_sampler_generation generation,
                          unsigned element_size)
{
   switch (generation) {
   case AGX_SAMPLER_GENERATION_GEN4:
      return element_size == AGX_SAMPLER_LENGTH ||
             element_size == AGX_NATIVE_SAMPLER_MAX_ELEMENT_SIZE;
   case AGX_SAMPLER_GENERATION_GEN4_HAL200:
   case AGX_SAMPLER_GENERATION_GEN5:
      return element_size == AGX_NATIVE_SAMPLER_MAX_ELEMENT_SIZE;
   case AGX_SAMPLER_GENERATION_UNKNOWN:
   default:
      return false;
   }
}

bool
agx_pack_native_sampler(enum agx_chip chip,
                       struct agx_native_sampler_descriptor *out,
                       const struct agx_native_sampler_state *state)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!state || !util_is_power_of_two_nonzero(state->maximum_anisotropy) ||
       state->maximum_anisotropy > 16)
      return false;

   const enum agx_sampler_generation generation =
      agx_chip_sampler_generation(chip);
   const unsigned element_size = agx_chip_sampler_heap_element_size(chip);
   if (!agx_sampler_contract_valid(generation, element_size) ||
       !agx_sampler_reduction_supported(generation, state->reduction_mode) ||
       (state->compare_enable &&
        state->reduction_mode != AGX_SAMPLER_REDUCTION_WEIGHTED_AVERAGE))
      return false;

   switch (generation) {
   case AGX_SAMPLER_GENERATION_GEN4:
      agx_pack(out->opaque, SAMPLER, cfg) {
         cfg.minimum_lod = state->minimum_lod;
         cfg.maximum_lod = state->maximum_lod;
         cfg.maximum_anisotropy = state->maximum_anisotropy;
         cfg.magnify = state->magnify;
         cfg.minify = state->minify;
         cfg.mip_filter = state->mip_filter;
         cfg.wrap_s = state->wrap_s;
         cfg.wrap_t = state->wrap_t;
         cfg.wrap_r = state->wrap_r;
         cfg.pixel_coordinates = state->pixel_coordinates;
         if (state->compare_enable) {
            cfg.compare_func = state->compare_func;
            cfg.compare_enable = true;
         }
         cfg.border_colour = state->border_colour;
         cfg.seamful_cube_maps = state->seamful_cube_maps;
      }
      return true;

   case AGX_SAMPLER_GENERATION_GEN4_HAL200:
      agx_pack(out->opaque, SAMPLER_HAL200, cfg) {
         cfg.minimum_lod = state->minimum_lod;
         cfg.maximum_lod = state->maximum_lod;
         cfg.maximum_anisotropy = state->maximum_anisotropy;
         cfg.magnify = state->magnify;
         cfg.minify = state->minify;
         cfg.mip_filter = state->mip_filter;
         cfg.wrap_s = state->wrap_s;
         cfg.wrap_t = state->wrap_t;
         cfg.wrap_r = state->wrap_r;
         cfg.pixel_coordinates = state->pixel_coordinates;
         if (state->compare_enable) {
            cfg.compare_func = state->compare_func;
            cfg.compare_enable = true;
         }
         cfg.lod_bias = state->lod_bias;
         cfg.border_colour = state->border_colour;
         cfg.seamful_cube_maps = state->seamful_cube_maps;
      }
      return true;

   case AGX_SAMPLER_GENERATION_GEN5:
      agx_pack(out->opaque, SAMPLER_GEN5, cfg) {
         cfg.minimum_lod = state->minimum_lod;
         cfg.maximum_lod = state->maximum_lod;
         cfg.maximum_anisotropy = state->maximum_anisotropy;
         cfg.magnify = state->magnify;
         cfg.minify = state->minify;
         cfg.mip_filter = state->mip_filter;
         cfg.wrap_s = state->wrap_s;
         cfg.wrap_t = state->wrap_t;
         cfg.wrap_r = state->wrap_r;
         cfg.pixel_coordinates = state->pixel_coordinates;
         cfg.lod_bias = state->lod_bias;
         cfg.border_colour = state->border_colour;
         cfg.seamful_cube_maps = state->seamful_cube_maps;

         if (state->reduction_mode == AGX_SAMPLER_REDUCTION_MINIMUM) {
            cfg.reduction_mode = AGX_SAMPLER_REDUCTION_MODE_MINIMUM;
         } else if (state->reduction_mode == AGX_SAMPLER_REDUCTION_MAXIMUM) {
            cfg.reduction_mode = AGX_SAMPLER_REDUCTION_MODE_MAXIMUM;
         } else {
            /* Grounded by the Gen5 sampler constructor: bit 42 selects the
             * compare-function interpretation of the overlapping bits. */
            cfg.compare_mode = true;
            cfg.compare_func = state->compare_func;
         }
      }
      return true;

   case AGX_SAMPLER_GENERATION_UNKNOWN:
   default:
      return false;
   }
}

bool
agx_pack_native_txf_sampler(enum agx_chip chip,
                           struct agx_native_sampler_descriptor *out)
{
   const struct agx_native_sampler_state state = {
      .minimum_lod = 0.0f,
      .maximum_lod = INFINITY,
      .maximum_anisotropy = 1,
      .mip_filter = AGX_MIP_FILTER_NEAREST,
      .wrap_s = AGX_WRAP_CLAMP_TO_BORDER,
      .wrap_t = AGX_WRAP_CLAMP_TO_BORDER,
      .wrap_r = AGX_WRAP_CLAMP_TO_BORDER,
      .compare_func = AGX_COMPARE_FUNC_LEQUAL,
      .border_colour = AGX_BORDER_COLOUR_TRANSPARENT_BLACK,
      .reduction_mode = AGX_SAMPLER_REDUCTION_WEIGHTED_AVERAGE,
   };

   return agx_pack_native_sampler(chip, out, &state);
}
