/* SPDX-License-Identifier: MIT */
#include "compiler/nir/nir_builder.h"
#include "hk_cmd_buffer.h"
#include "hk_shader.h"

static nir_def *
root_word(nir_builder *b, unsigned offset)
{
   return nir_load_global_constant(
      b, 1, 64, nir_iadd_imm(b, nir_load_root_agx(b), offset), .align_mul = 8,
      .access = ACCESS_CAN_SPECULATE);
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
   nir_def *word_address =
      nir_iadd(b, map->pointer,
               nir_u2u64(b, nir_iand_imm(b, nir_ushr_imm(b, page, 3), ~3u)));
   nir_def *word =
      nir_load_global_constant(b, 1, 32, word_address, .align_mul = 4);
   nir_def *bit =
      nir_iand_imm(b, nir_ushr(b, word, nir_iand_imm(b, page, 31)), 1);
   return nir_ior(b, nir_inot(b, inside), nir_ine_imm(b, bit, 0));
}

static nir_def *
canonical_address(nir_builder *b, const struct sparse_map *map,
                  nir_def *address)
{
   nir_def *primary = nir_iand(b, address, nir_inot(b, map->size));
   nir_def *offset = nir_isub(b, primary, map->base);
   nir_def *inside = nir_ult(b, offset, map->size);
   nir_def *safe_offset = nir_bcsel(b, inside, offset, nir_imm_int64(b, 0));
   nir_def *directory =
      root_word(b, hk_root_descriptor_offset(buffer_alias_table));
   nir_def *leaf = nir_load_global_constant(
      b, 1, 64,
      nir_iadd(b, directory,
               nir_ishl_imm(b, nir_ushr_imm(b, safe_offset, 26), 3)),
      .align_mul = 8);
   nir_def *fallback = nir_bcsel(b, inside, primary, address);
   nir_push_if(b, nir_iand(b, inside, nir_ine_imm(b, leaf, 0)));
   nir_def *mapped = nir_load_global_constant(
      b, 1, 64,
      nir_iadd(b, leaf,
               nir_iand_imm(b, nir_ushr_imm(b, safe_offset, 11), 32760)),
      .align_mul = 8);
   nir_def *canonical =
      nir_bcsel(b, nir_ine_imm(b, mapped, 0),
                nir_iadd(b, mapped, nir_iand_imm(b, address, 16383)), primary);
   nir_push_else(b, NULL);
   nir_pop_if(b, NULL);
   return nir_if_phi(b, canonical, fallback);
}

/* Only the first two levels of the alias table are driver-owned memory.
 * Entries in a leaf are application addresses, and dereferencing those still
 * requires residency checks. Do not treat arbitrary pointers loaded from
 * descriptors or push constants as metadata. */
static unsigned
alias_table_level(nir_def *address)
{
   nir_instr *instr = nir_def_instr(address);
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      return alu->op == nir_op_iadd
                ? MAX2(alias_table_level(alu->src[0].src.ssa),
                       alias_table_level(alu->src[1].src.ssa))
                : 0;
   }
   if (instr->type != nir_instr_type_intrinsic)
      return 0;
   nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
   if (load->intrinsic != nir_intrinsic_load_global_constant ||
       load->def.bit_size != 64 || load->def.num_components != 1)
      return 0;

   nir_def *src = load->src[0].ssa;
   nir_instr *src_instr = nir_def_instr(src);
   if (src_instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(src_instr);
      if (alu->op == nir_op_iadd) {
         for (unsigned i = 0; i < 2; ++i) {
            nir_instr *base = nir_def_instr(alu->src[i].src.ssa);
            if (base->type == nir_instr_type_intrinsic &&
                nir_instr_as_intrinsic(base)->intrinsic == nir_intrinsic_load_root_agx &&
                nir_src_is_const(alu->src[1 - i].src) &&
                nir_src_as_uint(alu->src[1 - i].src) ==
                   hk_root_descriptor_offset(buffer_alias_table))
               return 1;
         }
      }
   }
   return alias_table_level(src) == 1 ? 2 : 0;
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
             op == nir_intrinsic_load_descriptor_set_agx ||
             op == nir_intrinsic_load_from_texture_handle_agx;
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
       (is_descriptor_address(address) || alias_table_level(address)))
      return false;
   assert(address->bit_size == 64);
   b->cursor =
      atomic ? nir_before_instr(&intr->instr) : nir_after_instr(&intr->instr);
   const struct sparse_map map = load_map(b);
   const unsigned bytes = intr->def.bit_size / 8;

   if (atomic) {
      nir_def *valid = resident(b, &map, address);
      nir_push_if(b, valid);
      nir_intrinsic_instr *copy =
         nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));
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
   return nir_shader_intrinsics_pass(nir, lower_access, nir_metadata_none,
                                     NULL);
}

static bool
lower_memory_alias(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   bool atomic = intr->intrinsic == nir_intrinsic_global_atomic ||
                 intr->intrinsic == nir_intrinsic_global_atomic_swap;
   bool store = intr->intrinsic == nir_intrinsic_store_global;
   bool load = intr->intrinsic == nir_intrinsic_load_global ||
               intr->intrinsic == nir_intrinsic_load_global_constant;
   if (!atomic && !load && !store)
      return false;
   nir_def *address = intr->src[store ? 1 : 0].ssa;
   if (is_descriptor_address(address))
      return false;
   b->cursor = nir_before_instr(&intr->instr);
   const struct sparse_map map = load_map(b);
   if (atomic) {
      nir_src_rewrite(&intr->src[0], canonical_address(b, &map, address));
      return true;
   }

   unsigned bits = store ? intr->src[0].ssa->bit_size : intr->def.bit_size;
   unsigned components =
      store ? intr->src[0].ssa->num_components : intr->def.num_components;
   unsigned bytes = bits / 8;
   /* A naturally aligned access contained in one mapping page has a single
    * translation. Preserve its vector shape and write mask: splitting it
    * into scalar components repeats the alias lookup and prevents ordinary
    * vector memory operations from reaching the backend. An access whose
    * alignment cannot prove page containment still needs the piecewise path.
    */
   if (components * bytes <= MIN2(nir_intrinsic_align(intr), 16384)) {
      nir_src_rewrite(&intr->src[store ? 1 : 0],
                      canonical_address(b, &map, address));
      return true;
   }
   /* A vector or misaligned scalar may span independently mapped pages.
    * Resolve each naturally aligned piece rather than mapping only its
    * first byte and accidentally continuing in the backing allocation. */
   unsigned piece = MIN2(bytes, nir_intrinsic_align(intr));
   nir_def *values[NIR_MAX_VEC_COMPONENTS];
   for (unsigned c = 0; c < components; ++c) {
      if (store && !(nir_intrinsic_write_mask(intr) & BITFIELD_BIT(c)))
         continue;
      nir_def *value = store ? nir_channel(b, intr->src[0].ssa, c)
                             : nir_imm_intN_t(b, 0, bits);
      for (unsigned offset = 0; offset < bytes; offset += piece) {
         nir_def *addr = canonical_address(
            b, &map, nir_iadd_imm(b, address, c * bytes + offset));
         if (store) {
            nir_def *part =
               nir_u2uN(b, nir_ushr_imm(b, value, offset * 8), piece * 8);
            nir_store_global(b, part, addr, .write_mask = 1, .align_mul = piece,
                             .access = nir_intrinsic_access(intr));
         } else {
            nir_def *part =
               intr->intrinsic == nir_intrinsic_load_global_constant
                  ? nir_load_global_constant(
                       b, 1, piece * 8, addr, .align_mul = piece,
                       .access = nir_intrinsic_access(intr))
                  : nir_load_global(b, 1, piece * 8, addr, .align_mul = piece,
                                    .access = nir_intrinsic_access(intr));
            value = nir_ior(
               b, value, nir_ishl_imm(b, nir_u2uN(b, part, bits), offset * 8));
         }
      }
      if (!store)
         values[c] = value;
   }
   if (store)
      nir_instr_remove(&intr->instr);
   else
      nir_def_replace(&intr->def, nir_vec(b, values, components));
   return true;
}

bool
hk_nir_lower_memory_aliases(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_memory_alias, nir_metadata_none,
                                     NULL);
}
