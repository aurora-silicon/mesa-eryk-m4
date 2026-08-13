/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_device_removal_core.h"

#include "util/u_atomic.h"

#include <stddef.h>

void
d12_device_removal_state_init(struct d12_device_removal_state *state)
{
   if (!state)
      return;

   p_atomic_set(&state->reason, 0);
   state->callback = NULL;
   state->userdata = NULL;
}

bool
d12_device_removal_set_callback(
   struct d12_device_removal_state *state,
   d12_device_removal_callback callback, void *userdata)
{
   if (!state || !callback || state->callback)
      return false;

   state->userdata = userdata;
   state->callback = callback;

   const int32_t reason = p_atomic_read(&state->reason);
   if (reason)
      callback(userdata, reason);
   return true;
}

bool
d12_device_removal_report(struct d12_device_removal_state *state,
                          int32_t reason)
{
   if (!state || !reason)
      return false;

   if (p_atomic_cmpxchg(&state->reason, 0, reason) != 0)
      return false;

   if (state->callback)
      state->callback(state->userdata, reason);
   return true;
}

int32_t
d12_device_removal_reason(const struct d12_device_removal_state *state)
{
   return state ? p_atomic_read(&state->reason) : 0;
}
