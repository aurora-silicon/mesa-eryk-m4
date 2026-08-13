/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral, one-shot D3D12 device-removal publication.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*d12_device_removal_callback)(void *userdata, int32_t reason);

struct d12_device_removal_state {
   int32_t reason;
   d12_device_removal_callback callback;
   void *userdata;
};

void d12_device_removal_state_init(
   struct d12_device_removal_state *state);

/* Register before any queue worker can report removal. If removal already
 * happened, the callback is invoked synchronously with the retained reason. */
bool d12_device_removal_set_callback(
   struct d12_device_removal_state *state,
   d12_device_removal_callback callback, void *userdata);

/* First failure wins and invokes the registered callback exactly once.
 * Returns true only for the thread that published the transition. */
bool d12_device_removal_report(
   struct d12_device_removal_state *state, int32_t reason);

int32_t d12_device_removal_reason(
   const struct d12_device_removal_state *state);
