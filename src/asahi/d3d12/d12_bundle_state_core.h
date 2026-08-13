/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct d12_bundle_dynamic_state {
   bool stencil_ref_set;
   bool blend_factor_set;
   uint8_t stencil_ref_front;
   uint8_t stencil_ref_back;
   float blend_factor[4];
};

static inline void
d12_bundle_dynamic_state_set_stencil(
   struct d12_bundle_dynamic_state *state, uint8_t front, uint8_t back)
{
   state->stencil_ref_set = true;
   state->stencil_ref_front = front;
   state->stencil_ref_back = back;
}

static inline void
d12_bundle_dynamic_state_set_blend(struct d12_bundle_dynamic_state *state,
                                   const float factor[4])
{
   state->blend_factor_set = true;
   memcpy(state->blend_factor, factor, sizeof(state->blend_factor));
}

static inline void
d12_bundle_dynamic_state_apply(const struct d12_bundle_dynamic_state *state,
                               uint8_t *front, uint8_t *back,
                               float factor[4])
{
   if (state->stencil_ref_set) {
      *front = state->stencil_ref_front;
      *back = state->stencil_ref_back;
   }

   if (state->blend_factor_set)
      memcpy(factor, state->blend_factor, sizeof(state->blend_factor));
}
