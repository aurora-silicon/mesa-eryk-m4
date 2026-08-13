/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_queue_timeline_core.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void
test_submission_pipeline(void)
{
   struct d12_queue_timeline_state state;
   uint64_t in = UINT64_MAX;
   uint64_t out = UINT64_MAX;

   d12_queue_timeline_init(&state);
   assert(state.submitted == 0);
   assert(state.retired == 0);
   assert(d12_queue_timeline_inflight(&state) == 0);

   assert(d12_queue_timeline_next_submit(&state, &in, &out));
   assert(in == 0 && out == 1);
   assert(d12_queue_timeline_next_submit(&state, &in, &out));
   assert(in == 1 && out == 2);
   assert(d12_queue_timeline_next_submit(&state, &in, &out));
   assert(in == 2 && out == 3);
   assert(d12_queue_timeline_inflight(&state) == 3);
   assert(d12_queue_timeline_has_capacity(&state, 4));
   assert(!d12_queue_timeline_has_capacity(&state, 3));

   assert(d12_queue_timeline_retire(&state, 1));
   assert(d12_queue_timeline_inflight(&state) == 2);
   assert(d12_queue_timeline_has_capacity(&state, 3));
   assert(d12_queue_timeline_retire(&state, 3));
   assert(d12_queue_timeline_inflight(&state) == 0);
}

static void
test_markers_do_not_serialize_submission(void)
{
   struct d12_queue_timeline_state state;
   uint64_t in;
   uint64_t out;

   d12_queue_timeline_init(&state);
   assert(d12_queue_timeline_marker(&state) == 0);
   assert(d12_queue_timeline_next_submit(&state, &in, &out));
   assert(d12_queue_timeline_marker(&state) == 1);

   /* Multiple fence signals snapshot the same completion point. */
   assert(d12_queue_timeline_marker(&state) == 1);
   assert(d12_queue_timeline_marker(&state) == 1);

   /* Work after Signal may be submitted before the marker retires. */
   assert(d12_queue_timeline_next_submit(&state, &in, &out));
   assert(in == 1 && out == 2);
   assert(state.retired == 0);

   assert(d12_queue_timeline_retire(&state, 1));
   assert(d12_queue_timeline_retire(&state, 1));
   assert(d12_queue_timeline_retire(&state, 2));
}

static void
test_invalid_transitions(void)
{
   struct d12_queue_timeline_state state = {
      .submitted = UINT64_MAX,
      .retired = UINT64_MAX - 1,
   };
   uint64_t in = 7;
   uint64_t out = 9;

   assert(!d12_queue_timeline_next_submit(&state, &in, &out));
   assert(in == 7 && out == 9);
   assert(!d12_queue_timeline_retire(&state, UINT64_MAX - 2));
   assert(d12_queue_timeline_retire(&state, UINT64_MAX));

   d12_queue_timeline_init(&state);
   assert(!d12_queue_timeline_retire(&state, 1));
   assert(!d12_queue_timeline_next_submit(NULL, &in, &out));
   assert(!d12_queue_timeline_next_submit(&state, NULL, &out));
   assert(!d12_queue_timeline_next_submit(&state, &in, NULL));
   assert(!d12_queue_timeline_retire(NULL, 0));
   assert(d12_queue_timeline_marker(NULL) == 0);
   assert(d12_queue_timeline_inflight(NULL) == 0);
   assert(!d12_queue_timeline_has_capacity(NULL, 1));
   assert(!d12_queue_timeline_has_capacity(&state, 0));
}

int
main(void)
{
   test_submission_pipeline();
   test_markers_do_not_serialize_submission();
   test_invalid_transitions();
   puts("d12 queue timeline core tests: pass");
   return 0;
}
