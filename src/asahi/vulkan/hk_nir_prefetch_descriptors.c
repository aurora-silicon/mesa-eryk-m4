/* SPDX-License-Identifier: MIT */
#include "hk_descriptor_prefetch.h"
#include "hk_native_descriptor.h"
#include "hk_sampler_flags.h"
#include "asahi/compiler/agx_compiler.h"
#include "nir_builder.h"
#include "asahi/lib/agx_abi.h"
#include "util/format/u_format.h"
#include "util/u_dynarray.h"

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
      while (nir_def_is_alu(address)) {
         nir_alu_instr *alu = nir_def_as_alu(address);
         if (alu->op != nir_op_iadd)
            return false;
         unsigned constant = nir_src_is_const(alu->src[0].src) ? 0 : 1;
         if (!nir_src_is_const(alu->src[constant].src))
            return false;
         uint64_t add = nir_src_as_uint(alu->src[constant].src);
         if (add > UINT32_MAX || byte > UINT32_MAX - add)
            return false;
         byte += add;
         base_src = alu->src[1 - constant].src;
         address = base_src.ssa;
      }
   } else {
      return false;
   }

   nir_intrinsic_instr *base = nir_src_as_intrinsic(base_src);
   if (!base || base->def.bit_size != 64 || base->def.num_components != 1)
      return false;
   unsigned half;
   if (base->intrinsic == nir_intrinsic_load_descriptor_set_agx)
      half = set_base + 4 * nir_intrinsic_desc_set(base);
   else if (base->intrinsic == nir_intrinsic_load_preamble)
      half = nir_intrinsic_base(base);
   else
      return false;
   if (half < set_base || (half - set_base) % 4 ||
       (half - set_base) / 4 >= set_count)
      return false;

   *size = intr->def.num_components * (intr->def.bit_size / 8);
   if ((byte | *size) & 1 || byte > UINT32_MAX - *size)
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
                                 set_count, &set, &offset, &size) ||
                ((offset | size) & 3))
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
                ((offset | size) & 3) || !spans[set].size_B)
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

/* Match the exact bounded-global load emitted by hk_shader.c. No bounds check
 * is discarded: the command encoder selects the same buffer or zero page.
 * Only immutable UBO descriptors approved by the pipeline layout qualify. */
static nir_scalar
ubo_src(nir_scalar s, unsigned index)
{
   return nir_scalar_chase_movs(nir_scalar_chase_alu_src(s, index));
}

static bool
ubo_load(nir_intrinsic_instr *intr, unsigned set_base, unsigned set_count,
         struct hk_ubo_prefetch *load)
{
   if (intr->intrinsic != nir_intrinsic_load_global ||
       intr->def.bit_size != 32 || intr->def.num_components != 1 ||
       (nir_intrinsic_access(intr) & (ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER)) !=
          (ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER) ||
       (nir_intrinsic_access(intr) & ACCESS_VOLATILE))
      return false;
   nir_scalar address = nir_scalar_resolved(intr->src[0].ssa, 0);
   uint64_t offset = 0;
   if (nir_scalar_is_alu(address) && nir_scalar_alu_op(address) == nir_op_iadd) {
      nir_scalar a = ubo_src(address, 0);
      nir_scalar b = ubo_src(address, 1);
      if (!nir_scalar_is_const(b)) {
         nir_scalar tmp = a; a = b; b = tmp;
      }
      if (!nir_scalar_is_const(b))
         return false;
      offset = nir_scalar_as_uint(b);
      address = a;
   }
   if (offset > UINT32_MAX - 4 || (offset & 3) ||
       !nir_scalar_is_alu(address) || nir_scalar_alu_op(address) != nir_op_bcsel)
      return false;
   nir_scalar valid = ubo_src(address, 0);
   nir_scalar pointer = ubo_src(address, 1);
   nir_scalar zero = ubo_src(address, 2);
   if (!nir_scalar_is_const(zero) || nir_scalar_as_uint(zero) != AGX_ZERO_PAGE_ADDRESS ||
       !nir_scalar_is_alu(valid) || nir_scalar_alu_op(valid) != nir_op_ult ||
       !nir_scalar_is_alu(pointer) ||
       nir_scalar_alu_op(pointer) != nir_op_pack_64_2x32_split)
      return false;
   nir_scalar last = ubo_src(valid, 0);
   nir_scalar bound = ubo_src(valid, 1);
   nir_scalar lo = ubo_src(pointer, 0);
   nir_scalar hi = ubo_src(pointer, 1);
   if (!nir_scalar_is_const(last) || nir_scalar_as_uint(last) != offset + 3 ||
       lo.def != hi.def || lo.def != bound.def ||
       lo.comp != 0 || hi.comp != 1 || bound.comp != 2)
      return false;
   nir_intrinsic_instr *descriptor = nir_scalar_as_intrinsic(lo);
   unsigned set;
   uint32_t descriptor_offset, size;
   if (!descriptor || !descriptor_load(descriptor, set_base, set_count,
                                      &set, &descriptor_offset, &size) ||
       descriptor->def.bit_size != 32 || size != 16)
      return false;
   *load = (struct hk_ubo_prefetch){
      .set = set, .descriptor_offset_B = descriptor_offset,
      .offset_B = offset, .size_B = 4,
   };
   return true;
}

bool
hk_nir_prefetch_ubos(nir_shader *nir, unsigned set_base, unsigned set_count,
                    unsigned *uniform_end, struct hk_ubo_prefetch *loads,
                    unsigned *load_count, hk_ubo_prefetch_allowed allowed,
                    void *data)
{
   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      bool changed = false;
      struct candidate { nir_intrinsic_instr *intr; unsigned entry; };
      struct util_dynarray candidates;
      util_dynarray_init(&candidates, NULL);
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            struct hk_ubo_prefetch load;
            if (!ubo_load(intr, set_base, set_count, &load) ||
                !allowed(data, load.set, load.descriptor_offset_B))
               continue;
            unsigned entry = 0;
            for (; entry < *load_count; ++entry) {
               if (loads[entry].set == load.set &&
                   loads[entry].descriptor_offset_B == load.descriptor_offset_B &&
                   loads[entry].offset_B == load.offset_B)
                  break;
            }
            if (entry == *load_count) {
               unsigned start = align(*uniform_end, 4);
               /* Two load ranges per entry, leaving room for roots, texture
                * tables, descriptors and constants in the 128-chunk loader. */
               if (entry == 48 || start + 4 > AGX_NUM_UNIFORMS)
                  continue;
               load.uniform_half = start;
               load.direct_half = start + 2;
               for (unsigned i = 0; i < entry; ++i) {
                  if (loads[i].set == load.set &&
                      loads[i].descriptor_offset_B == load.descriptor_offset_B) {
                     load.direct_half = loads[i].direct_half;
                     break;
                  }
               }
               loads[entry] = load;
               *uniform_end = start + 4;
               ++*load_count;
            }
            util_dynarray_append(&candidates,
                                 ((struct candidate){intr, entry}));
         }
      }
      /* All accepted addresses are immutable descriptors plus a constant,
       * independently bounds-checked offset. The Ioto path already loads these
       * values at draw entry. Group the sparse fallback at entry as well, so
       * one uniform decision and descriptor fetch serve the whole UBO instead
       * of cloning its alias/residency walk into each scalar's separate else.
       * Volatile loads and partially bound/update-after-bind sets never enter
       * this optimization. Sparse resolution still happens on the GPU. */
      nir_builder b = nir_builder_at(nir_before_impl(impl));
      nir_def *replacement[HK_UBO_PREFETCH_MAX] = {0};
      for (unsigned first = 0; first < *load_count; ++first) {
         if (replacement[first])
            continue;
         const struct hk_ubo_prefetch *group = &loads[first];
         nir_def *direct = nir_load_preamble(&b, 1, 32,
                                            .base = group->direct_half);
         nir_def *values[HK_UBO_PREFETCH_MAX] = {0};
         nir_def *fallback[HK_UBO_PREFETCH_MAX] = {0};
         nir_push_if(&b, nir_ine_imm(&b, direct, 0));
         for (unsigned i = first; i < *load_count; ++i) {
            if (loads[i].direct_half == group->direct_half)
               values[i] = nir_load_preamble(&b, 1, 32,
                                             .base = loads[i].uniform_half);
         }
         nir_push_else(&b, NULL);
         nir_def *table = nir_load_preamble(&b, 1, 64,
                                            .base = set_base + 4 * group->set);
         nir_def *desc = nir_load_global_constant(&b, 4, 32,
            nir_iadd_imm(&b, table, group->descriptor_offset_B), .align_mul = 4);
         nir_def *pointer = nir_pack_64_2x32_split(&b,
            nir_channel(&b, desc, 0), nir_channel(&b, desc, 1));
         for (unsigned i = first; i < *load_count; ++i) {
            if (!values[i])
               continue;
            nir_def *valid = nir_ult(&b, nir_imm_int(&b, loads[i].offset_B + 3),
                                       nir_channel(&b, desc, 2));
            nir_def *base = nir_bcsel(&b, valid, pointer,
                                      nir_imm_int64(&b, AGX_ZERO_PAGE_ADDRESS));
            fallback[i] = nir_load_global(&b, 1, 32,
               nir_iadd_imm(&b, base, loads[i].offset_B), .align_mul = 4,
               .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);
         }
         nir_pop_if(&b, NULL);
         for (unsigned i = first; i < *load_count; ++i) {
            if (values[i])
               replacement[i] = nir_if_phi(&b, values[i], fallback[i]);
         }
      }
      util_dynarray_foreach(&candidates, struct candidate, c) {
         nir_def_replace(&c->intr->def, replacement[c->entry]);
         changed = true;
      }
      util_dynarray_fini(&candidates);
      nir_progress(changed, impl, nir_metadata_none);
      progress |= changed;
   }
   return progress;
}

/* Descriptor predicates are checked by command recording for the alternate
 * executable. Retain all other sampler fields and native texture operations. */
bool
hk_nir_specialize_plain_samplers(nir_shader *nir, unsigned set_base,
   unsigned set_count, struct hk_plain_sampler *samplers, unsigned *count)
{
   const struct hk_sampled_image_descriptor_layout layout =
      hk_sampled_image_descriptor_layout(9);
   bool used[HK_PLAIN_SAMPLER_MAX] = {0};
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
            if (!descriptor_load(intr, set_base, set_count, &set, &offset, &size))
               continue;
            for (unsigned i = 0; i < *count; ++i) {
               if (set != samplers[i].set || offset < samplers[i].descriptor_offset_B)
                  continue;
               unsigned relative = offset - samplers[i].descriptor_offset_B;
               nir_builder b = nir_builder_at(nir_after_instr(instr));
               nir_def *replacement = NULL;
               if (intr->def.bit_size == 16 && intr->def.num_components == 1) {
                  if (relative == layout.min_lod || relative == layout.min_lod_uint)
                     replacement = nir_imm_intN_t(&b, 0, 16);
                  else if (relative == layout.clamp_0_sampler_index)
                     replacement = nir_imm_intN_t(&b, -1, 16);
               } else if (intr->def.bit_size == 32 && !(relative & 3) &&
                          relative <= layout.sampler + 4 &&
                          relative + size > layout.sampler + 4) {
                  unsigned component = (layout.sampler + 4 - relative) / 4;
                  nir_def *parts[NIR_MAX_VEC_COMPONENTS];
                  for (unsigned c = 0; c < intr->def.num_components; ++c) {
                     parts[c] = nir_channel(&b, &intr->def, c);
                     if (c == component)
                        parts[c] = nir_iand_imm(&b, parts[c],
                           ~(3u << HK_MINMAX_MODE_SHIFT));
                  }
                  replacement = nir_vec(&b, parts, intr->def.num_components);
               }
               if (replacement) {
                  nir_def_rewrite_uses_after(&intr->def, replacement);
                  changed = used[i] = true;
                  break;
               }
            }
         }
      }
      nir_progress(changed, impl, nir_metadata_control_flow);
      progress |= changed;
   }
   unsigned n = 0;
   for (unsigned i = 0; i < *count; ++i)
      if (used[i]) samplers[n++] = samplers[i];
   *count = n;
   return progress;
}
