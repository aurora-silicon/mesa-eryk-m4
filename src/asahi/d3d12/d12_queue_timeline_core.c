/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_queue_timeline_core.h"

#include <limits.h>

void
d12_queue_timeline_init(struct d12_queue_timeline_state *state)
{
   if (state)
      *state = (struct d12_queue_timeline_state){0};
}

bool
d12_queue_timeline_next_submit(struct d12_queue_timeline_state *state,
                               uint64_t *in_point, uint64_t *out_point)
{
   if (!state || !in_point || !out_point || state->submitted == UINT64_MAX)
      return false;

   *in_point = state->submitted;
   *out_point = ++state->submitted;
   return true;
}

uint64_t
d12_queue_timeline_marker(const struct d12_queue_timeline_state *state)
{
   return state ? state->submitted : 0;
}

bool
d12_queue_timeline_retire(struct d12_queue_timeline_state *state,
                          uint64_t point)
{
   if (!state || point < state->retired || point > state->submitted)
      return false;

   state->retired = point;
   return true;
}

uint64_t
d12_queue_timeline_inflight(const struct d12_queue_timeline_state *state)
{
   if (!state || state->retired > state->submitted)
      return 0;

   return state->submitted - state->retired;
}

bool
d12_queue_timeline_has_capacity(const struct d12_queue_timeline_state *state,
                                uint64_t max_inflight)
{
   return state && max_inflight > 0 &&
          d12_queue_timeline_inflight(state) < max_inflight;
}
