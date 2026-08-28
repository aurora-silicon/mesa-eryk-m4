/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_chip.h"
#include "util/vma.h"

#include <stdbool.h>
#include <stdint.h>

/* T8140's per-VM kernel runtime-graph range, expressed as [start, end). */
#define AGX_G17_RUNTIME_GRAPH_VA_START UINT64_C(0x7000000000)
#define AGX_G17_RUNTIME_GRAPH_VA_END   UINT64_C(0x7100000000)

bool agx_va_overlaps_runtime_graph_hole(enum agx_chip chip, uint64_t addr,
                                        uint64_t size);

bool agx_va_reserve_runtime_graph_hole(
   enum agx_chip chip, struct util_vma_heap *main_heap, uint64_t main_start,
   uint64_t main_size, uint64_t sparse_ro_offset,
   struct util_vma_heap *usc_heap, uint64_t usc_start, uint64_t usc_size);
