/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

static inline bool
poly_heap_allocation_fits(uint32_t offset, uint32_t size,
                          uint32_t heap_size)
{
   return size <= heap_size && offset <= heap_size - size;
}
