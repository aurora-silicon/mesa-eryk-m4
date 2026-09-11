/* SPDX-License-Identifier: MIT */
#include "hk_descriptor_prefetch.h"
#include "asahi/compiler/agx_compiler.h"
#include "nir_builder.h"
#include "util/format/u_format.h"

static bool
descriptor_load(nir_intrinsic_instr *intr, unsigned set_base,
                unsigned set_count, unsigned *set, uint32_t *offset,
                uint32_t *size)
{
   if (intr->def.bit_size != 16 && intr->def.bit_size != 32)
      return false;

   nir_src base_src;
   uint64_t byte;
   if (intr->intrinsic == nir_intrinsic_load_constant_agx) {
      if (!nir_src_is_const(intr->src[1]) || intr->src[1].ssa->bit_size != 32)
         return false;
      uint32_t index = nir_src_as_uint(intr->src[1]);
      if (nir_intrinsic_sign_extend(intr) && (int32_t)index < 0)
         return false;
      unsigned element = util_format_get_blocksize(nir_intrinsic_format(intr));
      if (!element)
         return false;
      unsigned shift = util_logbase2(element) + nir_intrinsic_base(intr);
      if (shift >= 32)
         return false;
      byte = (uint64_t)index << shift;
      base_src = intr->src[0];
   } else if (intr->intrinsic == nir_intrinsic_load_global_constant) {
      /* At driver lowering time address lowering has not run yet. Match the
       * descriptor-set base plus a constant byte offset in its generic form. */
      nir_def *address = intr->src[0].ssa;
      byte = 0;
      base_src = intr->src[0];
      if (nir_def_is_alu(address)) {
         nir_alu_instr *alu = nir_def_as_alu(address);
         if (alu->op != nir_op_iadd)
            return false;
         unsigned constant = nir_src_is_const(alu->src[0].src) ? 0 : 1;
         if (!nir_src_is_const(alu->src[constant].src))
            return false;
         byte = nir_src_as_uint(alu->src[constant].src);
         base_src = alu->src[1 - constant].src;
      }
   } else {
      return false;
   }

   nir_intrinsic_instr *base = nir_src_as_intrinsic(base_src);
   if (!base || base->intrinsic != nir_intrinsic_load_preamble ||
       base->def.bit_size != 64 || base->def.num_components != 1)
      return false;
   unsigned half = nir_intrinsic_base(base);
   if (half < set_base || (half - set_base) % 4 ||
       (half - set_base) / 4 >= set_count)
      return false;

   *size = intr->def.num_components * (intr->def.bit_size / 8);
   if ((byte | *size) & 3 || byte > UINT32_MAX - *size)
      return false;
   *set = (half - set_base) / 4;
   *offset = byte;
   return true;
}

bool
hk_nir_prefetch_descriptors(nir_shader *nir, unsigned set_base,
                            unsigned set_count, unsigned *uniform_end,
                            struct hk_descriptor_prefetch *spans)
{
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            unsigned set;
            uint32_t offset, size;
            if (!descriptor_load(nir_instr_as_intrinsic(instr), set_base,
                                 set_count, &set, &offset, &size))
               continue;
            struct hk_descriptor_prefetch *span = &spans[set];
            uint32_t first = span->size_B ? MIN2(span->offset_B, offset) : offset;
            uint32_t last = MAX2(span->offset_B + span->size_B, offset + size);
            span->offset_B = first;
            span->size_B = last - first;
         }
      }
   }

   for (unsigned set = 0; set < set_count; ++set) {
      struct hk_descriptor_prefetch *span = &spans[set];
      unsigned start = align(*uniform_end, 4);
      /* A performance choice, never a shader admission limit: sparse/large
       * spans retain their original loads when the physical uniform file
       * cannot hold the data. */
      if (!span->size_B || start > AGX_NUM_UNIFORMS ||
          span->size_B / 2 > AGX_NUM_UNIFORMS - start) {
         span->size_B = 0;
         continue;
      }
      span->uniform_half = start;
      *uniform_end = start + span->size_B / 2;
   }

   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      bool changed = false;
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            unsigned set;
            uint32_t offset, size;
            if (!descriptor_load(intr, set_base, set_count, &set, &offset, &size) ||
                !spans[set].size_B)
               continue;
            nir_builder b = nir_builder_at(nir_before_instr(instr));
            unsigned half = spans[set].uniform_half +
                            (offset - spans[set].offset_B) / 2;
            nir_def *value = nir_load_preamble(&b, intr->def.num_components,
                                               intr->def.bit_size, .base = half);
            nir_def_replace(&intr->def, value);
            changed = true;
         }
      }
      nir_progress(changed, impl, nir_metadata_control_flow);
      progress |= changed;
   }
   return progress;
}
