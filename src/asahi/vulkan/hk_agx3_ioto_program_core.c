/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_program_core.h"

#include "asahi/lib/agx_ioto_core.h"
#include "asahi/lib/agx_tilebuffer.h"

#include <string.h>

bool
hk_agx3_ioto_program_generate(
   enum agx_chip chip, const struct hk_agx3_ioto_program_desc *desc,
   void *output, size_t capacity, size_t *written)
{
   if (!desc || !agx_ioto_target_supported(chip))
      return false;

   const struct agx_ioto_profile_control profile = {
      .chip = chip,
      .payload0 = desc->profile_payload0,
      .payload1 = desc->profile_payload1,
   };

   return agx_ioto_straight_line_program_compose(
      chip, desc->program_va, desc->body, desc->body_size, &desc->tail,
      &profile, output, capacity, written);
}

bool
hk_agx3_ioto_program_generate_no_pixrel(
   enum agx_chip chip, const struct hk_agx3_ioto_program_desc *desc,
   const struct agx_ioto_preshader_desc *preshader, void *output,
   size_t capacity, size_t *written)
{
   if (!desc || !agx_ioto_target_supported(chip))
      return false;

   const struct agx_ioto_profile_control profile = {
      .chip = chip,
      .payload0 = desc->profile_payload0,
      .payload1 = desc->profile_payload1,
   };

   return agx_ioto_program_compose_no_pixrel(
      chip, desc->program_va, desc->body, desc->body_size, &desc->tail,
      &profile, preshader, output, capacity, written);
}

bool
hk_agx3_g17p_no_resource_program_generate(
   enum agx_chip chip, uint64_t program_va,
   const struct agx_ioto_state_tail_output *packed_tail, void *output,
   size_t capacity, size_t *written)
{
   if (chip != AGX_CHIP_G17P)
      return false;

   return agx_ioto_g17p_no_resource_program_compose(program_va, packed_tail,
                                                    output, capacity, written);
}

bool
hk_agx3_g17p_single_layer_rendering(uint32_t view_mask, uint32_t layer_count)
{
   return view_mask == 0 && layer_count == 1;
}

bool
hk_agx3_g17p_rt0_tile_layout_supported(struct agx_tilebuffer_layout *tib)
{
   if (!tib || tib->nr_samples != 1 || tib->layered ||
       tib->sample_size_B != 8 || tib->tile_size != 32 * 32 ||
       tib->spilled[0] || tib->logical_format[0] == PIPE_FORMAT_NONE ||
       agx_tilebuffer_physical_format(tib, 0) != PIPE_FORMAT_R8_UNORM)
      return false;

   for (unsigned rt = 1; rt < AGX_MAX_RENDER_TARGETS; ++rt) {
      if (tib->logical_format[rt] != PIPE_FORMAT_NONE)
         return false;
   }

   return true;
}

static bool
hk_agx3_g17p_graphics_state_tail(
   const struct hk_agx3_g17p_graphics_program_desc *desc,
   struct agx_ioto_state_tail_output *tail, bool ordinary)
{
   if (!desc || !tail || desc->shader_binary_size == 0 ||
       desc->nr_gprs == 0)
      return false;

   static const uint8_t vertex_speclm[AGX_IOTO_SPECLM_SIZE] = {
      0x77, 0x00, 0x2a, 0x41, 0x00, 0x00, 0x00, 0x00,
   };
   static const uint8_t fragment_speclm[AGX_IOTO_SPECLM_SIZE] = {
      0x77, 0x00, 0x2a, 0x43, 0x4b, 0x00, 0x00, 0x00,
   };

   struct agx_ioto_state ldshdr = {0};
   ldshdr.f[0] = desc->shader_va;
   /* Native G17P graphics code containers reserve an 0x80-byte framing area
    * before the 64-byte-aligned main code. LDSHDR f1 is the saturated number
    * of complete 0x80-byte spans in that padded main-code region. Valid
    * captures cover container sizes 0xc0/0x100/0x140/0x1c0/0x240 and yield
    * f1 0/1/1/2/3 respectively, including the cold 88-byte VS and 128-byte
    * EOT. Compute uses a separate state builder and remains unchanged.
    */
   const uint64_t padded_shader_size =
      ((uint64_t)desc->shader_binary_size + 63) & ~UINT64_C(63);
   const uint64_t fetch_span = padded_shader_size >> 7;
   ldshdr.f[1] = fetch_span > 3 ? 3 : fetch_span;
   ldshdr.f[23] = 1;

   struct agx_ioto_state spectpr = {0};
   /* The valid cold G1a no-vary fragment and the matched Apple Metal
    * 32023.917 constant-colour probe both omit ProgramVariant field 38.
    * FragmentProgramVariantESLState supplies D=0, whose rounded SPECTPR
    * payload is entirely zero. Mesa's nr_gprs is not this Apple metadata
    * field; using even a two-GPR shader here incorrectly sets f22=2.
    *
    * Resource fragments retain the compiler-footprint path below until their
    * ProgramVariant metadata contract is independently recovered.
   */
   const bool exact_no_resource_fragment =
      ordinary && desc->fragment && desc->uniform_range_count == 0 &&
      desc->scratch_size == 0;
   const uint32_t compiler_d =
      desc->compiler_d_valid ? desc->compiler_d : desc->nr_gprs;
   if ((desc->compiler_d_valid || !exact_no_resource_fragment) &&
       !agx_ioto_g17p_compiler_spectpr_build(desc->scratch_size,
                                             compiler_d, &spectpr)) {
      return false;
   }

   struct agx_ioto_state_tail_output staged = {
      .length = AGX_IOTO_STATE_TAIL_TYPE0_SIZE,
   };
   memcpy(staged.bytes, desc->fragment ? fragment_speclm : vertex_speclm,
          AGX_IOTO_SPECLM_SIZE);
   if (!agx_ioto_state_pack_ldshdr(AGX_CHIP_G17P, &ldshdr,
                                   staged.bytes + AGX_IOTO_SPECLM_SIZE,
                                   AGX_IOTO_LDSHDR_SIZE) ||
       !agx_ioto_state_pack_spectpr(
          AGX_CHIP_G17P, &spectpr,
          staged.bytes + AGX_IOTO_SPECLM_SIZE + AGX_IOTO_LDSHDR_SIZE,
          AGX_IOTO_SPECTPR_SIZE))
      return false;

   *tail = staged;
   return true;
}

bool
hk_agx3_g17p_basic_graphics_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_graphics_program_desc *desc,
   struct hk_agx3_g17p_graphics_program *program)
{
   if (!program || chip != AGX_CHIP_G17P || !desc ||
       desc->uniform_range_count > 3 ||
       (desc->uniform_range_count != 0 && !desc->uniform_ranges))
      return false;

   struct agx_ioto_state_tail_output tail;
   if (!hk_agx3_g17p_graphics_state_tail(desc, &tail, true))
      return false;

   struct hk_agx3_g17p_graphics_program staged = {0};
   size_t written = 0;
   if (desc->uniform_range_count == 0) {
      const bool composed =
         desc->fragment
            ? agx_ioto_g17p_fragment_no_resource_program_compose(
                 desc->program_va, &tail, staged.bytes, sizeof(staged.bytes),
                 &written)
            : agx_ioto_g17p_no_resource_program_compose(
                 desc->program_va, &tail, staged.bytes, sizeof(staged.bytes),
                 &written);
      const size_t expected =
         desc->fragment ? AGX_IOTO_G17P_FRAGMENT_NO_RESOURCE_PROGRAM_SIZE
                        : AGX_IOTO_G17P_NO_RESOURCE_PROGRAM_SIZE;
      if (!composed || written != expected)
         return false;
   } else {
      uint8_t body[384];
      struct agx_ioto_inflight_load loads[64];
      struct agx_ioto_inflight_load sort[64];
      struct agx_ioto_load_cache_entry cache[64];
      const struct agx3_ioto_uniform_program_desc load_desc = {
         .program_va = desc->program_va,
         .ranges = desc->uniform_ranges,
         .range_count = desc->uniform_range_count,
      };
      const struct agx3_ioto_uniform_program_workspace workspace = {
         .body = body,
         .body_capacity = sizeof(body),
         .loads = loads,
         .load_capacity = sizeof(loads) / sizeof(loads[0]),
         .sort = sort,
         .sort_capacity = sizeof(sort) / sizeof(sort[0]),
         .cache = cache,
         .cache_capacity = sizeof(cache) / sizeof(cache[0]),
      };

      size_t body_size, copy_offset;
      if (!agx3_ioto_uniform_program_body_generate(
             chip, &load_desc, workspace, &body_size, &copy_offset) ||
          copy_offset > body_size ||
          !(desc->fragment
               ? agx_ioto_g17p_fragment_resource_program_compose(
                    desc->program_va, body, copy_offset, body + copy_offset,
                    body_size - copy_offset, &tail, staged.bytes,
                    sizeof(staged.bytes), &written)
               : agx_ioto_g17p_resource_program_compose(
                    desc->program_va, body, copy_offset, body + copy_offset,
                    body_size - copy_offset, &tail, staged.bytes,
                    sizeof(staged.bytes), &written)))
         return false;
   }

   if (written > UINT16_MAX)
      return false;

   staged.length = (uint16_t)written;
   *program = staged;
   return true;
}

bool
hk_agx3_g17p_combined_sampler_ranges(
   const struct hk_agx3_g17p_combined_sampler_heap *heap,
   struct agx3_ioto_uniform_range ranges[2])
{
   if (!heap || !ranges || heap->descriptor_set_va == 0 ||
       heap->sampler_heap_va == 0 || (heap->texture_offset_B & 7) != 0 ||
       heap->sampler_element_size_B != 32 ||
       heap->sampler_index < HK_AGX3_G17P_BINDLESS_SAMPLER_BASE_INDEX)
      return false;

   const uint32_t native_index =
      heap->sampler_index - HK_AGX3_G17P_BINDLESS_SAMPLER_BASE_INDEX;
   if (native_index >= heap->sampler_count)
      return false;

   const uint64_t texture_va =
      heap->descriptor_set_va + heap->texture_offset_B;
   const uint64_t sampler_offset =
      (uint64_t)native_index * heap->sampler_element_size_B;
   const uint64_t sampler_va = heap->sampler_heap_va + sampler_offset;
   if (texture_va < heap->descriptor_set_va ||
       sampler_va < heap->sampler_heap_va)
      return false;

   ranges[0] = (struct agx3_ioto_uniform_range){
      .start_halfs = 0,
      .size_halfs = 16,
      .address = texture_va,
   };
   ranges[1] = (struct agx3_ioto_uniform_range){
      .start_halfs = 32,
      .size_halfs = 16,
      .address = sampler_va,
   };
   return true;
}

bool
hk_agx3_g17p_root_range(uint16_t start_halfs, uint8_t set_count,
                        uint64_t root_va,
                        struct agx3_ioto_uniform_range *range)
{
   if (!range || root_va == 0 || (start_halfs & 1) != 0 ||
       set_count > HK_AGX3_G17P_MAX_ROOT_SETS)
      return false;

   *range = (struct agx3_ioto_uniform_range){
      .start_halfs = start_halfs,
      .size_halfs = 4 * (1 + set_count),
      .address = root_va,
   };
   return true;
}

bool
hk_agx3_g17p_rt0_bg_shader_resources_match(
   enum hk_agx3_g17p_rt0_bg_op op, unsigned push_count, bool uses_txf)
{
   switch (op) {
   case HK_AGX3_G17P_RT0_BG_CLEAR:
      return push_count == HK_AGX3_G17P_RT0_CLEAR_PUSH_HALFS && !uses_txf;
   case HK_AGX3_G17P_RT0_BG_RELOAD:
      return push_count == 0 && uses_txf;
   default:
      return false;
   }
}

bool
hk_agx3_g17p_rt0_bg_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_rt0_bg_program_desc *desc,
   struct hk_agx3_g17p_rt0_bg_program *program)
{
   if (!program || chip != AGX_CHIP_G17P || !desc)
      return false;

   struct agx3_ioto_uniform_range ranges[2];
   size_t range_count;
   switch (desc->op) {
   case HK_AGX3_G17P_RT0_BG_CLEAR:
      if (!desc->clear_value_va || desc->texture_va || desc->sampler_va)
         return false;
      ranges[0] = (struct agx3_ioto_uniform_range){
         .start_halfs = 4,
         .size_halfs = HK_AGX3_G17P_RT0_CLEAR_PUSH_HALFS - 4,
         .address = desc->clear_value_va,
      };
      range_count = 1;
      break;
   case HK_AGX3_G17P_RT0_BG_RELOAD:
      if (desc->clear_value_va || !desc->texture_va || !desc->sampler_va)
         return false;
      ranges[0] = (struct agx3_ioto_uniform_range){
         .start_halfs = 0,
         .size_halfs = 16,
         .address = desc->texture_va,
      };
      ranges[1] = (struct agx3_ioto_uniform_range){
         .start_halfs = 16,
         .size_halfs = 16,
         .address = desc->sampler_va,
      };
      range_count = 2;
      break;
   default:
      return false;
   }

   const struct hk_agx3_g17p_graphics_program_desc shader = {
      .program_va = desc->program_va,
      .shader_va = desc->shader_va,
      .shader_binary_size = desc->shader_binary_size,
      .scratch_size = desc->scratch_size,
      .nr_gprs = desc->nr_gprs,
      .fragment = true,
   };
   struct agx_ioto_state_tail_output tail;
   if (!hk_agx3_g17p_graphics_state_tail(&shader, &tail, false))
      return false;

   uint8_t body[512];
   struct agx_ioto_inflight_load loads[64];
   struct agx_ioto_inflight_load sort[64];
   struct agx_ioto_load_cache_entry cache[64];
   const struct agx3_ioto_uniform_program_desc load_desc = {
      .program_va = desc->program_va,
      .ranges = ranges,
      .range_count = range_count,
   };
   const struct agx3_ioto_uniform_program_workspace workspace = {
      .body = body,
      .body_capacity = sizeof(body),
      .loads = loads,
      .load_capacity = sizeof(loads) / sizeof(loads[0]),
      .sort = sort,
      .sort_capacity = sizeof(sort) / sizeof(sort[0]),
      .cache = cache,
      .cache_capacity = sizeof(cache) / sizeof(cache[0]),
   };

   size_t body_size, copy_offset;
   if (!agx3_ioto_uniform_program_body_generate(chip, &load_desc, workspace,
                                                &body_size, &copy_offset) ||
       copy_offset > body_size)
      return false;

   /*
    * The clear program's length is a bound, not a constant. It embeds
    * clear_value_va as two LDIMM records, and agx_ioto_load_pack_ldimm() is
    * variable length: a record whose destination half is below 32 and whose
    * immediate is below 128 takes a 2-byte compact form instead of the 8-byte
    * long one. The address's high word is `clear_value_va >> 32`, so whenever
    * the clear value happens to live below the 128 GiB mark the program comes
    * out 6 bytes shorter -- 140 versus 134 with the fragment PIXREL terminal
    * protocol.
    *
    * AGX_IOTO_G17P_RT0_CLEAR_PROGRAM_SIZE is therefore the long-form maximum,
    * which is also what the caller allocates (hk_cmd_draw.c:349-351), so a
    * shorter program is written and used as-is. Demanding equality rejected
    * every clear whose colour landed at a low address, which is to say the
    * background program of an entire class of render passes.
    */
   struct hk_agx3_g17p_rt0_bg_program staged;
   size_t written = 0;
   if (!agx_ioto_g17p_fragment_resource_program_compose(
          desc->program_va, body, copy_offset, body + copy_offset,
          body_size - copy_offset, &tail, staged.bytes, sizeof(staged.bytes),
          &written) ||
       written > UINT16_MAX ||
       (desc->op == HK_AGX3_G17P_RT0_BG_CLEAR &&
        written > AGX_IOTO_G17P_RT0_CLEAR_PROGRAM_SIZE))
      return false;

   staged.length = (uint16_t)written;
   *program = staged;
   return true;
}

bool
hk_agx3_g17p_rt0_eot_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_rt0_eot_program_desc *desc,
   struct hk_agx3_g17p_rt0_eot_program *program)
{
   if (!program || chip != AGX_CHIP_G17P || !desc ||
       desc->shader_binary_size != 128 || desc->nr_gprs != 8 ||
       desc->staging_halfs != 8)
      return false;

   const struct hk_agx3_g17p_graphics_program_desc shader = {
      .program_va = desc->program_va,
      .shader_va = desc->shader_va,
      .shader_binary_size = desc->shader_binary_size,
      .scratch_size = desc->scratch_size,
      .nr_gprs = desc->nr_gprs,
      .compiler_d_valid = true,
      .compiler_d = desc->staging_halfs * 2,
      .fragment = true,
   };
   struct agx_ioto_state_tail_output tail;
   if (!hk_agx3_g17p_graphics_state_tail(&shader, &tail, false))
      return false;

   /*
    * A bound, not a constant -- the same variable-length LDIMM effect as the
    * clear program above. This one embeds pbe_argument_va, so whenever the
    * PBE argument lands at a low enough address the composed program is
    * shorter than the long-form maximum. The composer is already given
    * sizeof(staged.bytes) as its capacity, so it cannot overrun; demanding
    * exact equality only rejected valid programs, and it rejected the
    * end-of-tile store of every render pass whose argument allocation
    * happened to fall low.
    */
   struct hk_agx3_g17p_rt0_eot_program staged;
   size_t written = 0;
   if (!agx_ioto_g17p_rt0_eot_program_compose(
          desc->program_va, desc->pbe_argument_va, &tail, staged.bytes,
          sizeof(staged.bytes), &written) ||
       written > sizeof(staged.bytes))
      return false;

   staged.length = (uint16_t)written;
   *program = staged;
   return true;
}

bool
hk_agx3_g17p_direct_add3_program_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17p_direct_add3_program_desc *desc,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program)
{
   return agx3_g17p_direct_add3_program_upload_prepare(
      chip, desc, upload, written, program);
}

bool
hk_agx3_g17p_direct_add3_dispatch_append(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   struct agx3_compute_dispatch_sink *sink)
{
   return agx3_g17p_direct_add3_dispatch_append(program, grid, sink);
}

bool
hk_agx3_g17p_native_indirect_compute_dispatch_append(
   const struct agx3_compute_program *program,
   const struct agx3_g17p_native_indirect_dispatch *dispatch,
   struct agx3_compute_dispatch_sink *sink)
{
   return agx3_g17p_native_indirect_compute_dispatch_append(program, dispatch,
                                                             sink);
}

static bool
hk_agx3_g17p_compute_state_tail(
   const struct hk_agx3_g17p_compute_program_desc *desc,
   struct agx_ioto_state_tail_output *tail)
{
   if (!desc || !tail || desc->shader_binary_size == 0 ||
       desc->nr_gprs == 0 || desc->scratch_size != 0)
      return false;

   /* The direct-dispatch program kind, exactly as the add3 witness announces
    * its own compute shader.  See the graphics path above for why f1 is a
    * selector rather than a size: deriving it from `shader_binary_size` made
    * this 118-byte shader announce itself as GRID_HELPER, the kind m1n1 uses
    * for the indirect grid-setup helper, and made every shader over 256 bytes
    * fail the compose. */
   const enum agx_ioto_g17p_compute_ldshdr_f1 fetch =
      AGX_IOTO_G17P_COMPUTE_LDSHDR_F1_DIRECT_ADD3;
   struct agx_ioto_state_tail_output staged;
   if (!agx_ioto_g17p_runtime_compute_tail_build(desc->shader_va, fetch,
                                                 &staged))
      return false;

   /* Declare the shader's register footprint.
    *
    * agx_ioto_g17p_runtime_compute_tail_build() leaves SPECTPR entirely zero,
    * because it was recovered from the hand-written add3 witness, whose
    * register use fits the default allocation. Every compiler-produced shader
    * must declare its own: the graphics path directly above already does this
    * (hk_agx3_g17p_graphics_state_tail -> agx_ioto_g17p_compiler_spectpr_build)
    * and this function *validates* nr_gprs/scratch_size and then never used
    * them -- vestigial checks for a wiring that was never finished.
    *
    * Observed consequence: a 16-GPR Honeykrisp compute shader shipped the
    * add3 footprint (`f7 00 2a 00 00 00 00 00 00 00`, i.e. the nr_gprs==0
    * form that spectpr_build itself refuses to produce). The dispatch ran to
    * `stop` and faulted nowhere -- with soft faults enabled a store through a
    * register outside the declared allocation is silently discarded -- and the
    * destination buffer came back completely untouched.
    */
   struct agx_ioto_state spectpr;
   if (!agx_ioto_g17p_compiler_spectpr_build(desc->scratch_size, desc->nr_gprs,
                                             &spectpr) ||
       !agx_ioto_state_pack_spectpr(
          AGX_CHIP_G17P, &spectpr,
          staged.bytes + AGX_IOTO_SPECLM_SIZE + AGX_IOTO_LDSHDR_SIZE,
          AGX_IOTO_SPECTPR_SIZE))
      return false;

   *tail = staged;
   return true;
}

bool
hk_agx3_g17p_compute_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_compute_program_desc *desc,
   struct hk_agx3_g17p_compute_program *program)
{
   if (!program || chip != AGX_CHIP_G17P || !desc ||
       (desc->uniform_range_count != 0 && !desc->uniform_ranges))
      return false;

   struct agx_ioto_state_tail_output tail;
   if (!hk_agx3_g17p_compute_state_tail(desc, &tail))
      return false;

   struct hk_agx3_g17p_compute_program staged = {0};
   size_t written = 0;
   if (desc->uniform_range_count == 0) {
      if (!agx_ioto_g17p_no_resource_program_compose(
             desc->program_va, &tail, staged.bytes, sizeof(staged.bytes),
             &written))
         return false;
   } else {
      uint8_t body[HK_AGX3_G17P_COMPUTE_PROGRAM_MAX_SIZE];
      struct agx_ioto_inflight_load
         loads[HK_AGX3_G17P_COMPUTE_MAX_LOAD_CHUNKS];
      struct agx_ioto_inflight_load
         sort[HK_AGX3_G17P_COMPUTE_MAX_LOAD_CHUNKS];
      struct agx_ioto_load_cache_entry
         cache[HK_AGX3_G17P_COMPUTE_MAX_LOAD_CHUNKS];
      size_t body_bound, chunk_count;
      if (!agx3_ioto_uniform_program_body_bound(
             chip, desc->uniform_ranges, desc->uniform_range_count,
             &body_bound, &chunk_count) ||
          body_bound > sizeof(body) ||
          chunk_count > HK_AGX3_G17P_COMPUTE_MAX_LOAD_CHUNKS)
         return false;

      const struct agx3_ioto_uniform_program_desc load_desc = {
         .program_va = desc->program_va,
         .ranges = desc->uniform_ranges,
         .range_count = desc->uniform_range_count,
      };
      const struct agx3_ioto_uniform_program_workspace workspace = {
         .body = body,
         .body_capacity = sizeof(body),
         .loads = loads,
         .load_capacity = sizeof(loads) / sizeof(loads[0]),
         .sort = sort,
         .sort_capacity = sizeof(sort) / sizeof(sort[0]),
         .cache = cache,
         .cache_capacity = sizeof(cache) / sizeof(cache[0]),
      };
      size_t body_size, copy_offset;
      if (!agx3_ioto_uniform_program_body_generate(
             chip, &load_desc, workspace, &body_size, &copy_offset) ||
          copy_offset > body_size ||
          !agx_ioto_g17p_resource_program_compose(
             desc->program_va, body, copy_offset, body + copy_offset,
             body_size - copy_offset, &tail, staged.bytes,
             sizeof(staged.bytes), &written))
         return false;
   }

   if (written > UINT16_MAX)
      return false;

   const struct agx3_g17p_native_compute_program_desc launch_desc = {
      .program_va = desc->program_va,
      .shader_va = desc->shader_va,
      .launch_sources = desc->launch_sources,
   };
   if (!agx3_g17p_native_compute_program_bind(&launch_desc, &staged.launch))
      return false;

   staged.length = (uint16_t)written;
   *program = staged;
   return true;
}

bool
hk_agx3_g17p_native_compute_dispatch_append(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   struct agx3_compute_dispatch_sink *sink)
{
   return agx3_g17p_native_compute_dispatch_append(program, grid, sink);
}

bool
hk_agx3_g17s_minimal_compute_program_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc, void *output,
   size_t capacity, size_t *written, struct agx3_compute_program *program)
{
   if (chip != AGX_CHIP_G17S)
      return false;

   return agx3_g17s_minimal_compute_program_prepare(
      desc, output, capacity, written, program);
}

bool
hk_agx3_g17s_minimal_compute_program_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program)
{
   return agx3_g17s_minimal_compute_program_upload_prepare(
      chip, desc, upload, written, program);
}

bool
hk_agx3_g17s_minimal_compute_program_live_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx_ioto_g17s_minimal_compute_resources *resources,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program)
{
   return agx3_g17s_minimal_compute_program_live_upload_prepare(
      chip, desc, resources, upload, written, program);
}

bool
hk_agx3_g17s_minimal_compute_dispatch_append(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   struct agx3_compute_dispatch_sink *sink)
{
   return agx3_g17s_minimal_compute_dispatch_append(program, grid, sink);
}
