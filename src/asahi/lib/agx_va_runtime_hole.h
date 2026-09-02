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

/* G17P's compact VDM pointer is a 39-bit offset from 0x1000000000.
 * Keep the ordinary allocation and sparse-shadow windows below this exclusive
 * limit so every VDM control stream and payload is directly representable. */
#define AGX_G17P_VDM_USER_BASE  UINT64_C(0x1000000000)
#define AGX_G17P_VDM_USER_SIZE  (UINT64_C(1) << 39)
#define AGX_G17P_VDM_USER_LIMIT                                            \
   (AGX_G17P_VDM_USER_BASE + AGX_G17P_VDM_USER_SIZE)

/*
 * Reduce the kernel-reported user VA window to the power-of-two window the
 * driver actually allocates from, and derive the address bit that tells a
 * sparse buffer's read-only alias apart from its primary mapping.
 *
 * A sparse buffer is bound a second time at `addr + sparse_ro_offset`
 * (hk_buffer.c:95, d12_resource.c:1118).  For that to be a pure bit-set with
 * no carry -- and for the alias window to stay disjoint from the primary one
 * -- every address in [*out_start, *out_start + *out_size) must have the
 * sparse bit clear.  Since both are powers of two once rounded, that reduces
 * to *out_start > *out_size.  On success `*out_size` is both the window size
 * and the sparse read-only offset.
 *
 * Returns false if no usable split exists.
 */
bool agx_va_split_user_window(uint64_t user_start, uint64_t user_size,
                              uint64_t *out_start, uint64_t *out_size);

bool agx_va_overlaps_runtime_graph_hole(enum agx_chip chip, uint64_t addr,
                                        uint64_t size);

bool agx_va_reserve_runtime_graph_hole(
   enum agx_chip chip, struct util_vma_heap *main_heap, uint64_t main_start,
   uint64_t main_size, uint64_t sparse_ro_offset,
   struct util_vma_heap *usc_heap, uint64_t usc_start, uint64_t usc_size);
