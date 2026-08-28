/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_va_runtime_hole.h"

#include <assert.h>
#include <stdio.h>

#define PAGE UINT64_C(0x4000)
#define RUNTIME_GRAPH_HOLE_SIZE                                             \
   (AGX_G17_RUNTIME_GRAPH_VA_END - AGX_G17_RUNTIME_GRAPH_VA_START)

static bool
overlaps_runtime_graph_hole(uint64_t addr, uint64_t size)
{
   return addr < AGX_G17_RUNTIME_GRAPH_VA_END &&
          addr + size > AGX_G17_RUNTIME_GRAPH_VA_START;
}

static void
test_exact_contract(void)
{
   static_assert(AGX_G17_RUNTIME_GRAPH_VA_START == UINT64_C(0x7000000000),
                 "kernel contract start");
   static_assert(AGX_G17_RUNTIME_GRAPH_VA_END == UINT64_C(0x7100000000),
                 "kernel contract end");
   static_assert(AGX_G17_RUNTIME_GRAPH_VA_END -
                       AGX_G17_RUNTIME_GRAPH_VA_START ==
                    UINT64_C(0x100000000),
                 "kernel contract size");

   assert(!agx_va_overlaps_runtime_graph_hole(
      AGX_CHIP_G17P, AGX_G17_RUNTIME_GRAPH_VA_START - PAGE, PAGE));
   assert(agx_va_overlaps_runtime_graph_hole(
      AGX_CHIP_G17P, AGX_G17_RUNTIME_GRAPH_VA_START - PAGE, PAGE + 1));
   assert(agx_va_overlaps_runtime_graph_hole(
      AGX_CHIP_G17P, AGX_G17_RUNTIME_GRAPH_VA_START, PAGE));
   assert(!agx_va_overlaps_runtime_graph_hole(
      AGX_CHIP_G17P, AGX_G17_RUNTIME_GRAPH_VA_END, PAGE));
   assert(!agx_va_overlaps_runtime_graph_hole(
      AGX_CHIP_G17P, AGX_G17_RUNTIME_GRAPH_VA_START, 0));
   assert(!agx_va_overlaps_runtime_graph_hole(
      AGX_CHIP_G17A, AGX_G17_RUNTIME_GRAPH_VA_START, PAGE));
}

static void
test_main_and_timestamp_allocations_skip_hole(void)
{
   struct util_vma_heap main, usc;
   const uint64_t main_start = AGX_G17_RUNTIME_GRAPH_VA_START - (2 * PAGE);
   const uint64_t main_size =
      (AGX_G17_RUNTIME_GRAPH_VA_END - AGX_G17_RUNTIME_GRAPH_VA_START) +
      (4 * PAGE);
   util_vma_heap_init(&main, main_start, main_size);
   util_vma_heap_init(&usc, UINT64_C(0x1000000000), UINT64_C(0x100000000));

   assert(agx_va_reserve_runtime_graph_hole(
      AGX_CHIP_G17P, &main, main_start, main_size, UINT64_C(0x8000000000),
      &usc, UINT64_C(0x1000000000), UINT64_C(0x100000000)));
   assert(main.free_size == main_size - RUNTIME_GRAPH_HOLE_SIZE);

   assert(!util_vma_heap_alloc_addr(&main,
                                    AGX_G17_RUNTIME_GRAPH_VA_START, PAGE));
   assert(!util_vma_heap_alloc_addr(
      &main, AGX_G17_RUNTIME_GRAPH_VA_END - PAGE, PAGE));
   assert(!util_vma_heap_alloc_addr(
      &main, AGX_G17_RUNTIME_GRAPH_VA_START - PAGE, 2 * PAGE));

   const uint64_t ordinary = util_vma_heap_alloc(&main, PAGE, PAGE);
   const uint64_t timestamp = util_vma_heap_alloc(&main, PAGE, PAGE);
   assert(ordinary != 0 && !overlaps_runtime_graph_hole(ordinary, PAGE));
   assert(timestamp != 0 && !overlaps_runtime_graph_hole(timestamp, PAGE));

   util_vma_heap_finish(&usc);
   util_vma_heap_finish(&main);
}

static void
test_sparse_shadow_preimage_is_reserved(void)
{
   struct util_vma_heap main, usc;
   const uint64_t main_start = UINT64_C(0x2000000000);
   const uint64_t main_size = UINT64_C(0x4000000000);
   const uint64_t sparse_ro_offset = UINT64_C(0x4000000000);
   const uint64_t preimage =
      AGX_G17_RUNTIME_GRAPH_VA_START - sparse_ro_offset;
   util_vma_heap_init(&main, main_start, main_size);
   util_vma_heap_init(&usc, UINT64_C(0x1000000000), UINT64_C(0x100000000));

   assert(agx_va_reserve_runtime_graph_hole(
      AGX_CHIP_G17P, &main, main_start, main_size, sparse_ro_offset, &usc,
      UINT64_C(0x1000000000), UINT64_C(0x100000000)));
   assert(main.free_size == main_size - RUNTIME_GRAPH_HOLE_SIZE);

   assert(!util_vma_heap_alloc_addr(&main, preimage, PAGE));
   assert(!util_vma_heap_alloc_addr(
      &main, preimage + RUNTIME_GRAPH_HOLE_SIZE - PAGE, PAGE));
   assert(!util_vma_heap_alloc_addr(&main, preimage - PAGE, 2 * PAGE));

   const uint64_t sparse = util_vma_heap_alloc(&main, PAGE, PAGE);
   assert(sparse != 0);
   assert(!overlaps_runtime_graph_hole(sparse + sparse_ro_offset, PAGE));

   util_vma_heap_finish(&usc);
   util_vma_heap_finish(&main);
}

static void
test_fixed_usc_allocation_skips_hole(void)
{
   struct util_vma_heap main, usc;
   const uint64_t usc_start = AGX_G17_RUNTIME_GRAPH_VA_START - PAGE;
   const uint64_t usc_size =
      (AGX_G17_RUNTIME_GRAPH_VA_END - AGX_G17_RUNTIME_GRAPH_VA_START) +
      (2 * PAGE);
   util_vma_heap_init(&main, UINT64_C(0x2000000000), UINT64_C(0x100000000));
   util_vma_heap_init(&usc, usc_start, usc_size);

   assert(agx_va_reserve_runtime_graph_hole(
      AGX_CHIP_G17P, &main, UINT64_C(0x2000000000), UINT64_C(0x100000000),
      UINT64_C(0x4000000000), &usc, usc_start, usc_size));
   assert(usc.free_size == usc_size - RUNTIME_GRAPH_HOLE_SIZE);

   assert(!util_vma_heap_alloc_addr(&usc,
                                    AGX_G17_RUNTIME_GRAPH_VA_START, PAGE));
   assert(!util_vma_heap_alloc_addr(
      &usc, AGX_G17_RUNTIME_GRAPH_VA_END - PAGE, PAGE));
   const uint64_t automatic = util_vma_heap_alloc(&usc, PAGE, PAGE);
   assert(automatic != 0 && !overlaps_runtime_graph_hole(automatic, PAGE));

   util_vma_heap_finish(&usc);
   util_vma_heap_finish(&main);
}

static void
test_other_chips_keep_existing_layout(void)
{
   struct util_vma_heap main, usc;
   const uint64_t start = AGX_G17_RUNTIME_GRAPH_VA_START - PAGE;
   const uint64_t size = 3 * PAGE;
   util_vma_heap_init(&main, start, size);
   util_vma_heap_init(&usc, start, size);

   assert(agx_va_reserve_runtime_graph_hole(
      AGX_CHIP_G16C, &main, start, size, UINT64_C(0x8000000000), &usc,
      start, size));
   assert(util_vma_heap_alloc_addr(&main,
                                   AGX_G17_RUNTIME_GRAPH_VA_START, PAGE));
   assert(util_vma_heap_alloc_addr(&usc,
                                   AGX_G17_RUNTIME_GRAPH_VA_START, PAGE));

   util_vma_heap_finish(&usc);
   util_vma_heap_finish(&main);
}

int
main(void)
{
   test_exact_contract();
   test_main_and_timestamp_allocations_skip_hole();
   test_sparse_shadow_preimage_is_reserved();
   test_fixed_usc_allocation_skips_hole();
   test_other_chips_keep_existing_layout();
   puts("AGX G17 runtime-graph VA hole: all checks passed");
   return 0;
}
