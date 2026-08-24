/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_render_pass_core.h"

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

/* The published D3D12DDI values this translation must agree with. */
static void
test_enumerations_match_the_ddi(void)
{
   require(D12_RP_BEGIN_DISCARD == 0 && D12_RP_BEGIN_PRESERVE == 1 &&
              D12_RP_BEGIN_CLEAR == 2 && D12_RP_BEGIN_NO_ACCESS == 3 &&
              D12_RP_BEGIN_CONTINUED_FIRST == 4 &&
              D12_RP_BEGIN_CONTINUED_LAST == 6,
           "beginning-access values match D3D12DDI 0043 and 0053");
   require(D12_RP_END_DISCARD == 0 && D12_RP_END_PRESERVE == 1 &&
              D12_RP_END_RESOLVE == 2 && D12_RP_END_NO_ACCESS == 3 &&
              D12_RP_END_CONTINUED_FIRST == 4 &&
              D12_RP_END_CONTINUED_LAST == 6,
           "ending-access values match D3D12DDI 0043 and 0053");
}

/* The whole point of the feature on a tiler. */
static void
test_discard_removes_tile_traffic(void)
{
   struct d12_render_pass_plan plan;

   require(d12_render_pass_plan_for(D12_RP_BEGIN_DISCARD,
                                    D12_RP_END_PRESERVE, &plan),
           "discard/preserve is a legal pair");
   require(plan.load == D12_RP_LOAD_NONE,
           "a discarded entry never seeds the tile from memory");
   require(plan.store == D12_RP_STORE_PRESERVE,
           "a preserved exit still writes the tile back");

   require(d12_render_pass_plan_for(D12_RP_BEGIN_PRESERVE,
                                    D12_RP_END_DISCARD, &plan),
           "preserve/discard is a legal pair");
   require(plan.load == D12_RP_LOAD_PRESERVE,
           "a preserved entry seeds the tile from memory");
   require(plan.store == D12_RP_STORE_NONE,
           "a discarded exit never writes the tile back");

   require(d12_render_pass_plan_for(D12_RP_BEGIN_DISCARD, D12_RP_END_DISCARD,
                                    &plan),
           "discard/discard is a legal pair");
   require(plan.load == D12_RP_LOAD_NONE && plan.store == D12_RP_STORE_NONE,
           "a fully transient attachment costs no tile traffic at all");
}

static void
test_clear_and_resolve(void)
{
   struct d12_render_pass_plan plan;

   require(d12_render_pass_plan_for(D12_RP_BEGIN_CLEAR, D12_RP_END_PRESERVE,
                                    &plan),
           "clear/preserve is a legal pair");
   require(plan.load == D12_RP_LOAD_CLEAR,
           "a cleared entry seeds the tile from a constant, not memory");
   require(!plan.resolve, "a preserved exit does not resolve");

   require(d12_render_pass_plan_for(D12_RP_BEGIN_CLEAR, D12_RP_END_RESOLVE,
                                    &plan),
           "clear/resolve is a legal pair");
   require(plan.resolve, "a resolving exit is marked as such");
   require(plan.store == D12_RP_STORE_PRESERVE,
           "a resolve must still store: the resolve reads what the pass "
           "produced");
}

/* A suspended pass is continued later, so its contents have to survive. */
static void
test_suspend_and_resume_preserve(void)
{
   struct d12_render_pass_plan plan;

   require(d12_render_pass_plan_for(D12_RP_BEGIN_CONTINUED_FIRST,
                                    D12_RP_END_CONTINUED_FIRST, &plan),
           "resume/suspend is a legal pair");
   require(plan.load == D12_RP_LOAD_PRESERVE,
           "a resuming pass loads what the suspending pass stored");
   require(plan.store == D12_RP_STORE_PRESERVE,
           "a suspending pass stores for the pass that resumes it");

   require(d12_render_pass_plan_for(D12_RP_BEGIN_CONTINUED_LAST,
                                    D12_RP_END_CONTINUED_LAST, &plan),
           "local-read resume/suspend is a legal pair");
   require(plan.load == D12_RP_LOAD_PRESERVE &&
              plan.store == D12_RP_STORE_PRESERVE,
           "local-read suspension is conservative in both directions");
}

static void
test_no_access_must_pair(void)
{
   struct d12_render_pass_plan plan;

   require(d12_render_pass_plan_for(D12_RP_BEGIN_NO_ACCESS,
                                    D12_RP_END_NO_ACCESS, &plan),
           "no-access on both halves is legal");
   require(plan.no_access, "a fully untouched attachment is marked");
   require(plan.load == D12_RP_LOAD_NONE && plan.store == D12_RP_STORE_NONE,
           "an untouched attachment costs no tile traffic");

   require(!d12_render_pass_plan_for(D12_RP_BEGIN_NO_ACCESS,
                                     D12_RP_END_PRESERVE, &plan),
           "no-access paired with a real exit is refused");
   require(!d12_render_pass_plan_for(D12_RP_BEGIN_CLEAR,
                                     D12_RP_END_NO_ACCESS, &plan),
           "a real entry paired with no-access is refused");
}

static void
test_unknown_access_is_refused_not_guessed(void)
{
   struct d12_render_pass_plan plan;

   require(!d12_render_pass_plan_for(7, D12_RP_END_PRESERVE, &plan),
           "an unknown beginning access is refused");
   require(!d12_render_pass_plan_for(D12_RP_BEGIN_PRESERVE, 7, &plan),
           "an unknown ending access is refused");
   require(!d12_render_pass_plan_for(D12_RP_BEGIN_PRESERVE,
                                     D12_RP_END_PRESERVE, NULL),
           "a null plan is refused");

   /* Should a future runtime add an access type and reach the translators
    * directly, they must still choose the answer that cannot lose data. */
   require(d12_render_pass_load_for(99) == D12_RP_LOAD_PRESERVE,
           "an unknown entry falls back to loading");
   require(d12_render_pass_store_for(99) == D12_RP_STORE_PRESERVE,
           "an unknown exit falls back to storing");
}

/* Outside a render pass nothing may change: the legacy plan is what the
 * driver already did for an OMSetRenderTargets attachment. */
static void
test_legacy_plan_is_unchanged_behaviour(void)
{
   struct d12_render_pass_plan cleared = d12_render_pass_plan_legacy(true);
   struct d12_render_pass_plan loaded = d12_render_pass_plan_legacy(false);

   require(cleared.load == D12_RP_LOAD_CLEAR &&
              cleared.store == D12_RP_STORE_PRESERVE,
           "a cleared legacy attachment clears and stores");
   require(loaded.load == D12_RP_LOAD_PRESERVE &&
              loaded.store == D12_RP_STORE_PRESERVE,
           "an uncleared legacy attachment loads and stores");
   require(!cleared.resolve && !loaded.resolve && !cleared.no_access &&
              !loaded.no_access,
           "the legacy plan never resolves and is never untouched");
}

static void
test_every_legal_pair(void)
{
   for (uint32_t begin = 0; begin <= D12_RP_BEGIN_CONTINUED_LAST;
        begin++) {
      for (uint32_t end = 0; end <= D12_RP_END_CONTINUED_LAST; end++) {
         struct d12_render_pass_plan plan;
         const bool paired = (begin == D12_RP_BEGIN_NO_ACCESS) ==
                             (end == D12_RP_END_NO_ACCESS);

         require(d12_render_pass_plan_for(begin, end, &plan) == paired,
                 "only a correctly paired no-access is accepted");
         if (!paired)
            continue;

         /* A resolve reads the pass result, so it can never be paired with
          * a plan that does not store. */
         require(!plan.resolve || plan.store == D12_RP_STORE_PRESERVE,
                 "a resolving attachment always stores");
         /* NO_ACCESS is the only way to get no traffic in both directions
          * other than an explicit discard on both halves. */
         require(!plan.no_access ||
                    (plan.load == D12_RP_LOAD_NONE &&
                     plan.store == D12_RP_STORE_NONE),
                 "an untouched attachment never moves a tile");
      }
   }
}

int
main(void)
{
   test_enumerations_match_the_ddi();
   test_discard_removes_tile_traffic();
   test_clear_and_resolve();
   test_suspend_and_resume_preserve();
   test_no_access_must_pair();
   test_unknown_access_is_refused_not_guessed();
   test_legacy_plan_is_unchanged_behaviour();
   test_every_legal_pair();

   printf("d12_render_pass_core_tests: PASS (%u checks)\n", checks);
   return 0;
}
