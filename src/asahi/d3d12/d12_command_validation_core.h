/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

static inline bool
d12_command_has_graphics_pipeline(bool has_pipeline, bool pipeline_is_compute)
{
   return has_pipeline && !pipeline_is_compute;
}

static inline bool
d12_command_has_compute_pipeline(bool has_pipeline, bool pipeline_is_compute)
{
   return has_pipeline && pipeline_is_compute;
}
