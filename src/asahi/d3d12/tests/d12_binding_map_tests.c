/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Root-signature binding map: rooted descriptor-array resolution and dense
 * slot exhaustion.  Both frontends and both shader translators share this
 * map, so its register arithmetic is covered here rather than only through
 * a compiled shader.
 */

#include "dxil/d12_dxil.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

static void
require(int condition, const char *message)
{
   checks++;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

/* Resolve as the shader translators do. */
static bool
rooted(const struct d12_binding_map *map, unsigned klass, unsigned space,
       unsigned reg, unsigned *slot)
{
   unsigned base_reg = 0;
   unsigned base_slot = ~0u;
   bool found =
      d12_binding_map_rooted_base(map, klass, space, reg, &base_slot,
                                  &base_reg);
   if (found && slot)
      *slot = base_slot;
   return found;
}

static void
test_sized_range_ends(void)
{
   struct d12_binding_map map = {0};
   map.visibility_filter = UINT32_MAX;

   /* t0..t511 in one root range, rooted for slot economy. */
   d12_binding_map_add_rooted_visible(&map, D12_RES_SRV, 0, 0, 0, 0, 512);

   require(rooted(&map, D12_RES_SRV, 0, 0, NULL),
           "sized rooted range covers its lower bound");
   require(rooted(&map, D12_RES_SRV, 0, 511, NULL),
           "sized rooted range covers its last register");
   require(!rooted(&map, D12_RES_SRV, 0, 512, NULL),
           "sized rooted range does not cover one past its end");
   require(!rooted(&map, D12_RES_SRV, 0, 100000, NULL),
           "sized rooted range does not swallow the rest of the heap");
   require(!rooted(&map, D12_RES_UAV, 0, 0, NULL),
           "rooted resolution is class-exact");
   require(!rooted(&map, D12_RES_SRV, 1, 0, NULL),
           "rooted resolution is space-exact");
}

static void
test_unbounded_range_is_open_ended(void)
{
   struct d12_binding_map map = {0};
   map.visibility_filter = UINT32_MAX;

   /* An API range declared with NumDescriptors == UINT32_MAX. */
   d12_binding_map_add_unbounded_visible(&map, D12_RES_SRV, 0, 0, 0, 0);

   require(rooted(&map, D12_RES_SRV, 0, 0, NULL),
           "unbounded range covers its lower bound");
   require(rooted(&map, D12_RES_SRV, 0, 100000, NULL),
           "unbounded range stays open-ended");
}

/*
 * The regression this file exists for.  A sized range large enough to be
 * rooted used to be treated as extending forever, so a register belonging to
 * a later root parameter resolved against it and addressed an unrelated
 * descriptor in the heap.
 */
static void
test_later_parameter_is_not_captured(void)
{
   struct d12_binding_map map = {0};
   map.visibility_filter = UINT32_MAX;

   d12_binding_map_add_rooted_visible(&map, D12_RES_SRV, 0, 0, 0, 0, 256);
   unsigned later =
      d12_binding_map_add_range_visible(&map, D12_RES_SRV, 0, 300, 1, 0);

   require(later != ~0u, "later finite binding is assigned a slot");
   require(!rooted(&map, D12_RES_SRV, 0, 300, NULL),
           "a register past a sized rooted range is not captured by it");
   require(d12_binding_map_find(&map, D12_RES_SRV, 0, 300) == later,
           "that register still resolves to its own dense slot");
}

static void
test_nearest_base_wins(void)
{
   struct d12_binding_map map = {0};
   map.visibility_filter = UINT32_MAX;

   unsigned outer =
      d12_binding_map_add_rooted_visible(&map, D12_RES_SRV, 0, 0, 0, 0, 1000);
   unsigned inner = d12_binding_map_add_rooted_visible(&map, D12_RES_SRV, 0,
                                                       100, 1, 0, 50);
   unsigned slot = ~0u;

   require(outer != inner, "distinct rooted ranges take distinct slots");
   require(rooted(&map, D12_RES_SRV, 0, 120, &slot) && slot == inner,
           "a register inside the nearer range uses that range's base");
   require(rooted(&map, D12_RES_SRV, 0, 200, &slot) && slot == outer,
           "a register past the nearer range falls back to the outer base");
   require(rooted(&map, D12_RES_SRV, 0, 50, &slot) && slot == outer,
           "a register below the nearer range uses the outer base");
}

static void
test_visibility_filter(void)
{
   struct d12_binding_map map = {0};
   map.visibility_filter = UINT32_MAX;

   /* Two stages may legally root the same register in disjoint tables. */
   unsigned vs = d12_binding_map_add_rooted_visible(&map, D12_RES_SRV, 0, 0,
                                                    0, 1 /* VERTEX */, 64);
   unsigned ps = d12_binding_map_add_rooted_visible(&map, D12_RES_SRV, 0, 0,
                                                    1, 5 /* PIXEL */, 64);
   unsigned slot = ~0u;

   require(vs != ps, "disjoint stages take distinct rooted slots");

   map.visibility_filter = 5;
   require(rooted(&map, D12_RES_SRV, 0, 8, &slot) && slot == ps,
           "a filtered map resolves to the visible stage's base");

   map.visibility_filter = 1;
   require(rooted(&map, D12_RES_SRV, 0, 8, &slot) && slot == vs,
           "the other stage resolves to its own base");
}

static void
test_slot_exhaustion_is_reported(void)
{
   struct d12_binding_map *map = calloc(1, sizeof(*map));
   require(map != NULL, "binding map allocation");
   map->visibility_filter = UINT32_MAX;

   for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++) {
      require(d12_binding_map_add_range_visible(map, D12_RES_SRV, 0, i, 0,
                                                0) != ~0u,
              "every binding up to capacity is assigned a slot");
   }
   require(!map->overflow, "a map filled exactly to capacity has not overflowed");

   require(d12_binding_map_add_range_visible(
              map, D12_RES_SRV, 0, D12_MAX_RESOURCE_BINDINGS, 0, 0) == ~0u,
           "one binding past capacity is refused");
   require(map->overflow,
           "refusing a binding records the overflow instead of dropping it "
           "silently");

   /* A repeat of an already-assigned binding is not an overflow. */
   struct d12_binding_map small = {0};
   small.visibility_filter = UINT32_MAX;
   unsigned first =
      d12_binding_map_add_range_visible(&small, D12_RES_CBV, 0, 0, 0, 0);
   require(d12_binding_map_add_range_visible(&small, D12_RES_CBV, 0, 0, 0,
                                             0) == first,
           "re-adding a binding returns its existing slot");
   require(!small.overflow, "re-adding a binding is not an overflow");

   free(map);
}

int
main(void)
{
   test_sized_range_ends();
   test_unbounded_range_is_open_ended();
   test_later_parameter_is_not_captured();
   test_nearest_base_wins();
   test_visibility_filter();
   test_slot_exhaustion_is_reported();

   printf("d12_binding_map_tests: PASS (%u checks)\n", checks);
   return 0;
}
