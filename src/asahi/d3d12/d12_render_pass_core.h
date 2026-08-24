/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 render-pass access types translated into tile load/store intent.
 *
 * This is the one place the mapping lives, because it is where a tiler either
 * wins or loses: DISCARD on entry means the tile is never read from memory,
 * and DISCARD on exit means it is never written back.  Outside a render pass
 * the driver has to infer both from clear tracking and assume the worst, so
 * every attachment costs a load and a store whether or not the application
 * needed one.
 *
 * Keep this free of Windows and Mesa headers.  The D3D12 access enumerations
 * are a stable ABI, so the translation is expressible as arithmetic and is
 * covered by host tests rather than only by a running render pass.
 */

#ifndef D12_RENDER_PASS_CORE_H
#define D12_RENDER_PASS_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * D3D12DDI_RENDER_PASS_BEGINNING_ACCESS_TYPE. The 0043 and 0053 revisions
 * agree on 0..3 and differ above: 0043 spells 4 and 5 RESUME_WRITING and
 * RESUME_LOCAL_READ, while 0053 spells 4, 5 and 6 PRESERVE_LOCAL_RENDER,
 * PRESERVE_LOCAL_SRV and PRESERVE_LOCAL_UAV. Every one of them means the
 * contents matter, so they translate identically and one table serves both.
 */
#define D12_RP_BEGIN_DISCARD            0u
#define D12_RP_BEGIN_PRESERVE           1u
#define D12_RP_BEGIN_CLEAR              2u
#define D12_RP_BEGIN_NO_ACCESS          3u
#define D12_RP_BEGIN_CONTINUED_FIRST    4u
#define D12_RP_BEGIN_CONTINUED_LAST     6u

/* D3D12DDI_RENDER_PASS_ENDING_ACCESS_TYPE, with the same split above 3:
 * 0043 has SUSPEND_WRITING and SUSPEND_LOCAL_READ, 0053 has the three
 * PRESERVE_LOCAL variants. All of them must survive the pass boundary. */
#define D12_RP_END_DISCARD              0u
#define D12_RP_END_PRESERVE             1u
#define D12_RP_END_RESOLVE              2u
#define D12_RP_END_NO_ACCESS            3u
#define D12_RP_END_CONTINUED_FIRST      4u
#define D12_RP_END_CONTINUED_LAST       6u

enum d12_render_pass_load {
   /* The tile is not seeded from memory. Its contents are whatever the pass
    * writes; anything it does not write is undefined, which is exactly what
    * DISCARD promises. */
   D12_RP_LOAD_NONE = 0,
   /* Seed the tile from memory: an AGX background LOAD program, or ZLS
    * z_load/s_load for depth-stencil. */
   D12_RP_LOAD_PRESERVE,
   /* Seed the tile from a constant: an AGX background CLEAR program, or
    * ISP_BGOBJDEPTH/ISP_BGOBJVALS for depth-stencil. */
   D12_RP_LOAD_CLEAR,
};

enum d12_render_pass_store {
   /* The tile is never written back. */
   D12_RP_STORE_NONE = 0,
   /* An AGX end-of-tile STORE program, or ZLS z_store/s_store. */
   D12_RP_STORE_PRESERVE,
};

struct d12_render_pass_plan {
   enum d12_render_pass_load load;
   enum d12_render_pass_store store;
   /* ENDING_ACCESS_RESOLVE stores the multisampled attachment and then
    * resolves it into a second resource. The store is not optional: the
    * resolve reads what the pass produced. */
   bool resolve;
   /* Both halves said NO_ACCESS. The attachment is declared but untouched,
    * so it contributes no tile traffic at all. */
   bool no_access;
};

/*
 * A pass whose contents are continued -- suspended and resumed across command
 * lists, or kept live locally for a later pass -- must survive the boundary:
 * the exit stores and the entry loads. Treating those as PRESERVE is always
 * correct. It merely forgoes keeping the tile resident across the split,
 * which nothing in this driver could do anyway: each pass is its own hardware
 * render, so the contents have to go through memory regardless.
 */
static inline enum d12_render_pass_load
d12_render_pass_load_for(uint32_t beginning_access)
{
   switch (beginning_access) {
   case D12_RP_BEGIN_CLEAR:
      return D12_RP_LOAD_CLEAR;
   case D12_RP_BEGIN_DISCARD:
   case D12_RP_BEGIN_NO_ACCESS:
      return D12_RP_LOAD_NONE;
   case D12_RP_BEGIN_PRESERVE:
   default:
      /* Continued entries, and any access type a later runtime adds, load:
       * it is the answer that cannot lose data. */
      return D12_RP_LOAD_PRESERVE;
   }
}

static inline enum d12_render_pass_store
d12_render_pass_store_for(uint32_t ending_access)
{
   switch (ending_access) {
   case D12_RP_END_DISCARD:
   case D12_RP_END_NO_ACCESS:
      return D12_RP_STORE_NONE;
   case D12_RP_END_PRESERVE:
   case D12_RP_END_RESOLVE:
   default:
      /* Same rule: storing is the answer that cannot lose data. */
      return D12_RP_STORE_PRESERVE;
   }
}

/*
 * False when the pair is not one D3D12 permits. NO_ACCESS is a statement that
 * the attachment is untouched for the whole pass, so it has to appear on both
 * halves; pairing it with real access would mean the driver has to both skip
 * and perform the same tile traffic.
 */
static inline bool
d12_render_pass_access_valid(uint32_t beginning_access,
                             uint32_t ending_access)
{
   const bool begin_no_access = beginning_access == D12_RP_BEGIN_NO_ACCESS;
   const bool end_no_access = ending_access == D12_RP_END_NO_ACCESS;

   if (beginning_access > D12_RP_BEGIN_CONTINUED_LAST ||
       ending_access > D12_RP_END_CONTINUED_LAST)
      return false;

   return begin_no_access == end_no_access;
}

static inline bool
d12_render_pass_plan_for(uint32_t beginning_access, uint32_t ending_access,
                         struct d12_render_pass_plan *plan)
{
   if (!plan || !d12_render_pass_access_valid(beginning_access,
                                              ending_access))
      return false;

   *plan = (struct d12_render_pass_plan){
      .load = d12_render_pass_load_for(beginning_access),
      .store = d12_render_pass_store_for(ending_access),
      .resolve = ending_access == D12_RP_END_RESOLVE,
      .no_access = beginning_access == D12_RP_BEGIN_NO_ACCESS,
   };
   return true;
}

/*
 * What the driver does today for an attachment bound through
 * OMSetRenderTargets, so a render pass and a plain render agree on every path
 * that is not being optimised: seed from memory unless the pass cleared, and
 * always write back.
 */
static inline struct d12_render_pass_plan
d12_render_pass_plan_legacy(bool cleared)
{
   return (struct d12_render_pass_plan){
      .load = cleared ? D12_RP_LOAD_CLEAR : D12_RP_LOAD_PRESERVE,
      .store = D12_RP_STORE_PRESERVE,
      .resolve = false,
      .no_access = false,
   };
}

#ifdef __cplusplus
}
#endif

#endif
