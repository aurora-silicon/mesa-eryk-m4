/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Command-list state which is dynamic relative to a graphics PSO.  Keeping
 * this POD independent of D3D/Mesa types makes the bundle inheritance rules
 * directly host-testable. */
struct d12_dynamic_state_values {
   uint8_t stencil_ref_front;
   uint8_t stencil_ref_back;
   float blend_factor[4];
   float depth_bias;
   float depth_bias_clamp;
   float slope_scaled_depth_bias;
   uint32_t strip_cut;
};

/* A bundle inherits non-PSO state from its caller.  These bits distinguish an
 * inherited value from one established by the bundle.  Binding a graphics PSO
 * establishes both dynamic PSO defaults, just like SetPipelineState does on a
 * direct list. */
struct d12_bundle_dynamic_state {
   bool stencil_ref_set;
   bool blend_factor_set;
   bool depth_bias_set;
   bool strip_cut_set;
   struct d12_dynamic_state_values values;
};

static inline struct d12_dynamic_state_values
d12_dynamic_state_reset(bool has_graphics_pso, float pso_depth_bias,
                        float pso_depth_bias_clamp,
                        float pso_slope_scaled_depth_bias,
                        uint32_t pso_strip_cut)
{
   struct d12_dynamic_state_values state = {0};

   if (has_graphics_pso) {
      state.depth_bias = pso_depth_bias;
      state.depth_bias_clamp = pso_depth_bias_clamp;
      state.slope_scaled_depth_bias = pso_slope_scaled_depth_bias;
      state.strip_cut = pso_strip_cut;
   }

   return state;
}

static inline void
d12_bundle_dynamic_state_set_stencil(
   struct d12_bundle_dynamic_state *state, uint8_t front, uint8_t back)
{
   state->stencil_ref_set = true;
   state->values.stencil_ref_front = front;
   state->values.stencil_ref_back = back;
}

static inline void
d12_bundle_dynamic_state_set_blend(struct d12_bundle_dynamic_state *state,
                                   const float factor[4])
{
   state->blend_factor_set = true;
   memcpy(state->values.blend_factor, factor,
          sizeof(state->values.blend_factor));
}

static inline void
d12_bundle_dynamic_state_set_pipeline(
   struct d12_bundle_dynamic_state *state,
   const struct d12_dynamic_state_values *values)
{
   state->depth_bias_set = true;
   state->strip_cut_set = true;
   state->values.depth_bias = values->depth_bias;
   state->values.depth_bias_clamp = values->depth_bias_clamp;
   state->values.slope_scaled_depth_bias =
      values->slope_scaled_depth_bias;
   state->values.strip_cut = values->strip_cut;
}

static inline void
d12_bundle_dynamic_state_set_depth_bias(
   struct d12_bundle_dynamic_state *state, float depth_bias,
   float depth_bias_clamp, float slope_scaled_depth_bias)
{
   state->depth_bias_set = true;
   state->values.depth_bias = depth_bias;
   state->values.depth_bias_clamp = depth_bias_clamp;
   state->values.slope_scaled_depth_bias = slope_scaled_depth_bias;
}

static inline void
d12_bundle_dynamic_state_set_strip_cut(
   struct d12_bundle_dynamic_state *state, uint32_t strip_cut)
{
   state->strip_cut_set = true;
   state->values.strip_cut = strip_cut;
}

static inline void
d12_bundle_dynamic_state_apply(const struct d12_bundle_dynamic_state *state,
                               struct d12_dynamic_state_values *values)
{
   if (state->stencil_ref_set) {
      values->stencil_ref_front = state->values.stencil_ref_front;
      values->stencil_ref_back = state->values.stencil_ref_back;
   }

   if (state->blend_factor_set)
      memcpy(values->blend_factor, state->values.blend_factor,
             sizeof(values->blend_factor));

   if (state->depth_bias_set) {
      values->depth_bias = state->values.depth_bias;
      values->depth_bias_clamp = state->values.depth_bias_clamp;
      values->slope_scaled_depth_bias =
         state->values.slope_scaled_depth_bias;
   }

   if (state->strip_cut_set)
      values->strip_cut = state->values.strip_cut;
}

/* Reset after a recording error is special: once Close reports that error,
 * the command list may never be reset, and Reset must replay the same failure.
 * HRESULT is represented as signed 32-bit here to keep the rule host-only. */
static inline bool
d12_command_list_failed_close_reset(bool is_closed, bool had_error,
                                    int32_t record_error,
                                    int32_t generic_failure,
                                    int32_t *result)
{
   if (!is_closed || !had_error)
      return false;

   *result = record_error < 0 ? record_error : generic_failure;
   return true;
}
