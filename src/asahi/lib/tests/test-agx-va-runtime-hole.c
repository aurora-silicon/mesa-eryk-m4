/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_va_runtime_hole.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

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

/*
 * The user-window split, driven by the two kernel geometries that exist.
 *
 * Inputs are what agx_open_device() computes before the reduction:
 *   shader_base  = ALIGN_POT(MAX2(vm_start, 2^36 + 16384), 4 GiB) = 0x1100000000
 *   user_start   = shader_base + 4 GiB                            = 0x1200000000
 *   kernel_start = vm_end - MAX2(vm_kernel_min_size, 32 GiB)
 *   user_size    = kernel_start - user_start
 * with vm_start = UAT_PGSZ = 0x4000 on both geometries (kernel mmu.rs:145
 * IOVA_USER_BASE, and g17_drm.rs:24 T8140_USER_START), and vm_end two pages
 * below the root's translation top: 1 << 39 for AGX2 (mmu.rs:149), 1 << 42 for
 * AGX3 (g17_drm.rs:25).
 */
static void
check_split(const char *what, uint64_t user_start, uint64_t user_size,
            uint64_t expect_start, uint64_t expect_size)
{
   uint64_t start = 0, size = 0;
   const bool ok = agx_va_split_user_window(user_start, user_size, &start, &size);

   if (!ok || start != expect_start || size != expect_size) {
      fprintf(stderr,
              "%s: split(%#" PRIx64 ", %#" PRIx64 ") -> ok=%d start=%#" PRIx64
              " size=%#" PRIx64 ", expected start=%#" PRIx64 " size=%#" PRIx64
              "\n",
              what, user_start, user_size, ok, start, size, expect_start,
              expect_size);
      abort();
   }

   /* The invariant the whole scheme rests on: a sparse buffer's read-only
    * alias is `addr + size`, so every address in the window must have that
    * bit clear or the alias carries out of its own half. */
   assert((start & size) == 0);
   assert(((start + (size - 1)) & size) == 0);
   assert(start > size);
}

static void
test_user_window_split(void)
{
   const uint64_t vm_start = PAGE;
   const uint64_t shader_base = UINT64_C(0x1100000000);
   const uint64_t user_start = shader_base + UINT64_C(0x100000000);
   const uint64_t kernel_reserve = UINT64_C(32) << 30;
   assert(user_start > vm_start);

   /* AGX2 / G13-G14: 39-bit root. Upstream's single "quarters" fixup is
    * exactly enough here, and this case pins that behaviour unchanged. */
   const uint64_t agx2_vm_end = (UINT64_C(1) << 39) - 2 * PAGE;
   check_split("AGX2 39-bit", user_start,
               (agx2_vm_end - kernel_reserve) - user_start,
               UINT64_C(0x2000000000), UINT64_C(0x1000000000));

   /* AGX3 / T8140 G17P: 42-bit root. The window is eight times the rounded
    * start, so one fixup leaves 2^40 -- which straddles the sparse bit and
    * used to trip the assert in agx_open_device. */
   const uint64_t agx3_vm_end = (UINT64_C(1) << 42) - 2 * PAGE;
   check_split("AGX3 42-bit", user_start,
               (agx3_vm_end - kernel_reserve) - user_start,
               UINT64_C(0x2000000000), UINT64_C(0x1000000000));

   /* The AGX3 window and its read-only alias must both stay below the kernel
    * heap the same open() reserves at the top of the address space. */
   const uint64_t split_start = UINT64_C(0x2000000000);
   const uint64_t split_size = UINT64_C(0x1000000000);
   assert(split_start + 2 * split_size <= agx3_vm_end - kernel_reserve);
   /* ...and above the USC/shader heap, which the split does not move. */
   assert(split_start >= shader_base + UINT64_C(0x100000000));

   /* Degenerate inputs are refused, not asserted on. */
   uint64_t o1, o2;
   assert(!agx_va_split_user_window(0, 1 << 20, &o1, &o2));
   assert(!agx_va_split_user_window(1 << 20, 0, &o1, &o2));
   assert(!agx_va_split_user_window(1 << 20, 1 << 20, NULL, &o2));

   /* A window smaller than one page of slack still splits or is refused --
    * never returns a bit that the window straddles. */
   for (unsigned k = 1; k < 48; ++k) {
      uint64_t start = UINT64_C(1) << k, size = 0, out = 0;
      if (agx_va_split_user_window(start, start, &size, &out)) {
         assert((size & out) == 0);
         assert(((size + (out - 1)) & out) == 0);
      }
   }
}


/*
 * G17P's user heap and its sparse read-only alias must both fit below 2^40.
 *
 * The VDM PPP State Update carries its payload pointer in a 40-bit field
 * (agx_vdm_ppp_state_core.h: word 0 bits 0..7 are pointer bits 32..39, read
 * off Apple's own G15/HAL200/HAL300 emitters). G17P's USC window sits at
 * exactly 2^40, so a user heap placed AFTER the window -- which is what every
 * other chip does, and what G17P used to do -- puts every command-stream
 * allocation out of reach of that field. agx_ppp_fini() then refused every
 * payload and vkEndCommandBuffer() failed before a render command buffer
 * could be recorded.
 *
 * agx_device.c now derives G17P's window from [reservation, shader_base).
 * This checks the arithmetic that placement depends on.
 */
static void
test_g17p_user_window_fits_ppp_pointer_field(void)
{
   const uint64_t ppp_pointer_limit = UINT64_C(1) << 40;
   const uint64_t shader_base = UINT64_C(0x10000000000); /* AGX_G17P_USC_WINDOW_BASE */
   const uint64_t reservation = UINT64_C(1) << 36;

   assert(shader_base == ppp_pointer_limit &&
          "the USC window sits exactly at the PPP pointer limit");

   /* agx_device.c starts the low heap at shader_base/2 -- as high as it can
    * go -- because the split needs size < start and the alias needs a second
    * window of the same size. */
   uint64_t start = shader_base / 2;
   assert(start >= reservation);
   uint64_t size = shader_base - start;
   uint64_t out_start = 0, out_size = 0;
   assert(agx_va_split_user_window(start, size, &out_start, &out_size));

   /* Whatever the split picks, halving until the alias fits must terminate
    * with a non-empty window -- the same loop agx_device.c runs. */
   while (out_size > 0 && out_start + 2 * out_size > shader_base)
      out_size /= 2;
   assert(out_size > 0);

   /* The heap itself, and the read-only alias above it, are both addressable
    * by a 40-bit PPP payload pointer. */
   assert(out_start >= reservation);
   /* And the layout is not wastefully small: it uses a quarter of the range
    * below the window, which is the most this split admits. */
   assert(out_size == shader_base / 4);
   assert(out_start + out_size <= ppp_pointer_limit);
   assert(out_start + 2 * out_size <= ppp_pointer_limit);

   /* And it does not run into the USC window. */
   assert(out_start + 2 * out_size <= shader_base);
}

int
main(void)
{
   test_exact_contract();
   test_user_window_split();
   test_main_and_timestamp_allocations_skip_hole();
   test_sparse_shadow_preimage_is_reserved();
   test_fixed_usc_allocation_skips_hole();
   test_other_chips_keep_existing_layout();
   test_g17p_user_window_fits_ppp_pointer_field();
   puts("AGX VA layout: user-window split and G17 runtime-graph hole checks passed");
   return 0;
}
