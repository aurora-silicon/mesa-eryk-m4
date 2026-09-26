/* SPDX-License-Identifier: MIT */
#include "hk_nir_buffer_bounds.h"
#include "nir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ordinary UBO/SSBO descriptor bases are at least 16-byte aligned. Group
 * four readonly scalar loads only when their first address is aligned too.
 * Their offsets then cannot wrap within the 16-byte span. The fast path never
 * reads outside the descriptor; partial groups execute the original checks.
 * This pass runs before bounded descriptor loads become arbitrary pointers.
 */

/* Resolve an unsigned byte offset into an SSA base plus an immediate. */
static bool
split_offset(nir_def *def, nir_def **base, uint32_t *offset)
{
   *base = NULL;
   *offset = 0;
   for (unsigned depth = 0; depth < 8; ++depth) {
      nir_scalar s = nir_scalar_chase_movs(nir_get_scalar(def, 0));
      if (nir_scalar_is_const(s)) {
         *offset += nir_scalar_as_uint(s);
         return true;
      }
      if (nir_scalar_is_alu(s) && nir_scalar_alu_op(s) == nir_op_iadd) {
         nir_scalar a = nir_scalar_chase_alu_src(s, 0);
         nir_scalar c = nir_scalar_chase_alu_src(s, 1);
         if (nir_scalar_is_const(a)) {
            nir_scalar t = a;
            a = c;
            c = t;
         }
         if (nir_scalar_is_const(c) && a.comp == 0 && a.def->num_components == 1) {
            *offset += nir_scalar_as_uint(c);
            def = a.def;
            continue;
         }
      }
      if (s.comp || s.def->num_components != 1)
         return false;
      *base = s.def;
      return true;
   }
   return false;
}

static bool
eligible(nir_intrinsic_instr *load)
{
   if (load->instr.pass_flags || load->def.num_components != 1 ||
       load->def.bit_size != 32)
      return false;
   if (load->intrinsic != nir_intrinsic_load_global_constant_bounded &&
       load->intrinsic != nir_intrinsic_load_global_bounded)
      return false;
   enum gl_access_qualifier access = nir_intrinsic_access(load);
   return !(access & (ACCESS_VOLATILE | ACCESS_COHERENT)) &&
          (load->intrinsic == nir_intrinsic_load_global_constant_bounded ||
           (access & ACCESS_NON_WRITEABLE));
}

static bool
coalesce(nir_builder *b, nir_intrinsic_instr *first, void *data)
{
   if (!eligible(first) || nir_intrinsic_align(first) < 16)
      return false;
   nir_def *offset_base;
   uint32_t offset;
   if (!split_offset(first->src[1].ssa, &offset_base, &offset))
      return false;
   nir_intrinsic_instr *loads[4] = {first, NULL, NULL, NULL};
   unsigned found = 1, scanned = 0;
   for (nir_instr *instr = nir_instr_next(&first->instr); instr && scanned < 512;
        instr = nir_instr_next(instr), ++scanned) {
      if (instr->type == nir_instr_type_alu || instr->type == nir_instr_type_load_const)
         continue;
      if (instr->type != nir_instr_type_intrinsic)
         break;
      nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
      if (!eligible(load)) {
         if (nir_intrinsic_can_reorder(load))
            continue;
         break;
      }
      if (load->intrinsic != first->intrinsic ||
          load->src[0].ssa != first->src[0].ssa ||
          load->src[2].ssa != first->src[2].ssa ||
          nir_intrinsic_access(load) != nir_intrinsic_access(first))
         continue;
      nir_def *other_base;
      uint32_t other_offset;
      if (!split_offset(load->src[1].ssa, &other_base, &other_offset) ||
          other_base != offset_base)
         continue;
      uint32_t delta = other_offset - offset;
      if (delta > 12 || (delta & 3) || loads[delta / 4])
         continue;
      loads[delta / 4] = load;
      if (++found == 4)
         break;
   }
   if (found != 4)
      return false;

   b->cursor = nir_before_instr(&first->instr);
   nir_def *offsets[4];
   offsets[0] = first->src[1].ssa;
   for (unsigned c = 1; c < 4; ++c)
      offsets[c] = nir_iadd_imm(b, offsets[0], c * 4);
   nir_def *valid = hk_nir_buffer_offset_in_bounds(b, offsets[0], first->src[2].ssa, 16);
   nir_push_if(b, valid);
   nir_def *address = nir_iadd(b, first->src[0].ssa, nir_u2u64(b, offsets[0]));
   nir_def *vector = nir_load_global(b, 4, 32, address, .align_mul = 16,
      .access = (nir_intrinsic_access(first) | ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER) &
                ~ACCESS_CAN_SPECULATE);
   nir_push_else(b, NULL);
   nir_def *scalars[4];
   for (unsigned c = 0; c < 4; ++c) {
      nir_intrinsic_instr *copy = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &loads[c]->instr));
      copy->instr.pass_flags = 1;
      nir_builder_instr_insert(b, &copy->instr);
      nir_src_rewrite(&copy->src[1], offsets[c]);
      scalars[c] = &copy->def;
   }
   nir_def *fallback = nir_vec(b, scalars, 4);
   nir_pop_if(b, NULL);
   nir_def *result = nir_if_phi(b, vector, fallback);
   for (unsigned c = 0; c < 4; ++c) {
      loads[c]->instr.pass_flags = 1;
      nir_def_rewrite_uses(&loads[c]->def, nir_channel(b, result, c));
   }
   ++*(unsigned *)data;
   return true;
}

bool
hk_nir_coalesce_bounded(nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir)
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block)
            instr->pass_flags = 0;
   const char *audit = getenv("HK_COALESCE_BOUNDED_AUDIT");
   if (audit && !strcmp(audit, "nir"))
      nir_print_shader(nir, stderr);
   unsigned count = 0;
   bool progress = nir_shader_intrinsics_pass(nir, coalesce, nir_metadata_none, &count);
   if (audit)
      fprintf(stderr, "HK coalesced bounded groups=%u name=%s\n", count, nir->info.name ? nir->info.name : "unknown");
   if (progress)
      nir_opt_dce(nir);
   return progress;
}
