/* SPDX-License-Identifier: MIT */
#include "hk_cmd_buffer.h"
#include "hk_shader.h"
#include "compiler/nir/nir_builder.h"

static nir_def *
root_word(nir_builder *b, unsigned offset)
{
   return nir_load_global_constant(
      b, 1, 64, nir_iadd_imm(b, nir_load_root_agx(b), offset),
      .align_mul = 8, .access = ACCESS_CAN_SPECULATE);
}

struct sparse_map {
   nir_def *pointer, *base, *size;
};

static struct sparse_map
load_map(nir_builder *b)
{
   return (struct sparse_map){
      .pointer = root_word(b, hk_root_descriptor_offset(sparse_residency_map)),
      .base = root_word(b, hk_root_descriptor_offset(sparse_va_base)),
      .size = root_word(b, hk_root_descriptor_offset(sparse_va_size)),
   };
}

static nir_def *
resident(nir_builder *b, const struct sparse_map *map, nir_def *address)
{
   /* Normalize both primary and read-only alias addresses. Driver-private
    * allocations outside this VA window are always resident. Select a safe
    * map index before loading, including for null robust-access addresses. */
   nir_def *primary = nir_iand(b, address, nir_inot(b, map->size));
   nir_def *offset = nir_isub(b, primary, map->base);
   nir_def *inside = nir_ult(b, offset, map->size);
   nir_def *page = nir_u2u32(b, nir_ushr_imm(b, offset, 14));
   page = nir_bcsel(b, inside, page, nir_imm_int(b, 0));
   nir_def *word_address = nir_iadd(
      b, map->pointer, nir_u2u64(b, nir_iand_imm(b, nir_ushr_imm(b, page, 3), ~3u)));
   nir_def *word = nir_load_global_constant(b, 1, 32, word_address,
                                            .align_mul = 4);
   nir_def *bit = nir_iand_imm(b, nir_ushr(b, word, nir_iand_imm(b, page, 31)), 1);
   return nir_ior(b, nir_inot(b, inside), nir_ine_imm(b, bit, 0));
}

/* Root/set addresses are driver metadata, not application pointers. Do not
 * add residency work to each descriptor lookup. Stop at a memory load: a BDA
 * read from push constants or an SSBO must still be checked at its pointee. */
static bool
is_descriptor_address(nir_def *address)
{
   nir_instr *instr = nir_def_instr(address);
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_op op = nir_instr_as_intrinsic(instr)->intrinsic;
      return op == nir_intrinsic_load_root_agx ||
             op == nir_intrinsic_load_descriptor_set_agx;
   }
   if (instr->type != nir_instr_type_alu)
      return false;
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   return alu->op == nir_op_iadd &&
          (is_descriptor_address(alu->src[0].src.ssa) ||
           is_descriptor_address(alu->src[1].src.ssa));
}

static bool
lower_access(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   const bool atomic = intr->intrinsic == nir_intrinsic_global_atomic ||
                       intr->intrinsic == nir_intrinsic_global_atomic_swap;
   if (!atomic && intr->intrinsic != nir_intrinsic_load_global &&
       intr->intrinsic != nir_intrinsic_load_global_constant)
      return false;

   nir_def *address = intr->src[0].ssa;
   if (intr->intrinsic == nir_intrinsic_load_global_constant &&
       is_descriptor_address(address))
      return false;
   assert(address->bit_size == 64);
   b->cursor = atomic ? nir_before_instr(&intr->instr) : nir_after_instr(&intr->instr);
   const struct sparse_map map = load_map(b);
   const unsigned bytes = intr->def.bit_size / 8;

   if (atomic) {
      nir_def *valid = resident(b, &map, address);
      nir_push_if(b, valid);
      nir_intrinsic_instr *copy = nir_instr_as_intrinsic(
         nir_instr_clone(b->shader, &intr->instr));
      nir_builder_instr_insert(b, &copy->instr);
      nir_push_else(b, NULL);
      nir_def *zero = nir_imm_intN_t(b, 0, intr->def.bit_size);
      nir_pop_if(b, NULL);
      nir_def_replace(&intr->def, nir_if_phi(b, &copy->def, zero));
      return true;
   }

   nir_def *components[NIR_MAX_VEC_COMPONENTS];
   for (unsigned c = 0; c < intr->def.num_components; ++c) {
      nir_def *addr = nir_iadd_imm(b, address, c * bytes);
      nir_def *value = nir_channel(b, &intr->def, c);
      if (nir_intrinsic_align(intr) >= bytes) {
         components[c] = nir_bcsel(b, resident(b, &map, addr), value,
                                    nir_imm_intN_t(b, 0, intr->def.bit_size));
      } else {
         /* A misaligned scalar can straddle a page boundary. Preserve each
          * resident byte rather than discarding the whole scalar/vector. */
         nir_def *mask = nir_imm_intN_t(b, 0, intr->def.bit_size);
         for (unsigned i = 0; i < bytes; ++i) {
            nir_def *part = nir_bcsel(
               b, resident(b, &map, nir_iadd_imm(b, addr, i)),
               nir_imm_intN_t(b, UINT64_C(0xff) << (i * 8), intr->def.bit_size),
               nir_imm_intN_t(b, 0, intr->def.bit_size));
            mask = nir_ior(b, mask, part);
         }
         components[c] = nir_iand(b, value, mask);
      }
   }
   nir_def_rewrite_uses_after(&intr->def,
                              nir_vec(b, components, intr->def.num_components));
   return true;
}

bool
hk_nir_lower_sparse_access(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_access, nir_metadata_none, NULL);
}
