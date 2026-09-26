/* SPDX-License-Identifier: MIT */
#include "hk_cmd_buffer.h"
#include "hk_shader.h"
#include "hk_nir_buffer_bounds.h"
#include "compiler/nir/nir_builder.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", __LINE__, #x); abort(); } } while (0)
static const nir_shader_compiler_options options = {0};
static bool alias_leaf_present;
static bool specialize_geometry;
static const uint64_t base = 0x20000000, size = 0x10000000, map = 0x80000000;

static void
fold(nir_shader *s)
{
   if (specialize_geometry)
      hk_nir_specialize_memory_window(s, base, size);
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_copy_prop);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      nir_builder b = nir_builder_create(nir_shader_get_entrypoint(s));
      nir_foreach_block(block, b.impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic) continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            uint64_t value;
            if (intr->intrinsic == nir_intrinsic_load_root_agx) {
               value = 0x1000;
            } else if (intr->intrinsic == nir_intrinsic_load_global_constant &&
                       nir_src_is_const(intr->src[0])) {
               uint64_t addr = nir_src_as_uint(intr->src[0]);
               if (addr == 0x1000 + hk_root_descriptor_offset(sparse_residency_map)) value = map;
               else if (addr == 0x1000 + hk_root_descriptor_offset(buffer_alias_table)) value = 0x8000;
               else if (addr >= 0x8000 && addr < 0x9000) value = alias_leaf_present ? 0x10000 : 0;
               else if (addr >= 0x10000 && addr < 0x18000) value = 0x60000000 + (addr - 0x10000) * 2048;
               else if (addr == 0x1000 + hk_root_descriptor_offset(sparse_va_base)) value = base;
               else if (addr == 0x1000 + hk_root_descriptor_offset(sparse_va_size)) value = size;
               else { CHECK(addr >= map && addr < map + size / (16384 * 8)); value = 0x55555555; }
            } else continue;
            b.cursor = nir_before_instr(instr);
            nir_def_replace(&intr->def, nir_imm_intN_t(&b, value, intr->def.bit_size));
            progress = true;
         }
      }
      NIR_PASS(progress, s, nir_opt_dce);
   } while (progress);
}

static bool
byte_resident(uint64_t address)
{
   address &= ~size;
   return address < base || address - base >= size ||
          (((address - base) / 16384) % 2 == 0);
}

static void
check_load(uint64_t address, unsigned bits, unsigned components, unsigned alignment)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options, "sparse bytes");
   nir_def *input = nir_load_global(&b, components, bits, nir_imm_int64(&b, address), .align_mul = alignment);
   nir_store_global(&b, input, nir_imm_int64(&b, 0x4000), .align_mul = alignment);
   CHECK(hk_nir_lower_sparse_access(b.shader));
   b.cursor = nir_before_instr(nir_def_instr(input));
   nir_def *values[NIR_MAX_VEC_COMPONENTS];
   for (unsigned c = 0; c < components; ++c)
      values[c] = nir_imm_intN_t(&b, UINT64_C(0x8877665544332211) + c, bits);
   nir_def_replace(input, nir_vec(&b, values, components));
   fold(b.shader);
   unsigned stores = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic) continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_store_global) continue;
         CHECK(nir_src_is_const(intr->src[0])); ++stores;
         const nir_const_value *v = nir_src_as_const_value(intr->src[0]);
         for (unsigned c = 0; c < components; ++c) {
            uint64_t expected = 0, original = UINT64_C(0x8877665544332211) + c;
            for (unsigned i = 0; i < bits / 8; ++i)
               if (byte_resident(address + c * (bits / 8) + i))
                  expected |= original & (UINT64_C(0xff) << (i * 8));
            CHECK(nir_const_value_as_uint(v[c], bits) == expected);
         }
      }
   }
   CHECK(stores == 1);
   ralloc_free(b.shader);
}

static void
check_atomic(bool bound)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options, "sparse atomic");
   nir_def *input = nir_global_atomic(&b, 32,
      nir_imm_int64(&b, base + (bound ? 0 : 16384)), nir_imm_int(&b, 7),
      .atomic_op = nir_atomic_op_iadd);
   nir_store_global(&b, input, nir_imm_int64(&b, 0x4000), .align_mul = 4);
   CHECK(hk_nir_lower_sparse_access(b.shader));
   fold(b.shader);
   unsigned atomics = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic &&
             nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_global_atomic)
            ++atomics;
      }
   }
   CHECK(atomics == (unsigned)bound);
   ralloc_free(b.shader);
}

static void
check_buffer_alias(uint64_t address, bool leaf_present)
{
   alias_leaf_present = leaf_present;
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options, "atomic alias");
   nir_def *value = nir_global_atomic(&b, 32, nir_imm_int64(&b, address),
                                     nir_imm_int(&b, 1), .atomic_op = nir_atomic_op_iadd);
   nir_store_global(&b, value, nir_imm_int64(&b, 0x4000), .align_mul = 4);
   CHECK(hk_nir_lower_memory_aliases(b.shader));
   nir_validate_shader(b.shader, "atomic alias lowering");
   fold(b.shader);
   nir_intrinsic_instr *atomic = nir_instr_as_intrinsic(nir_def_instr(value));
   uint64_t primary = address & ~size;
   uint64_t expected = leaf_present && primary >= base && primary < base + size ?
      0x60000000 + ((primary - base) & ((1u << 26) - 1)) :
      primary >= base && primary < base + size ? primary : address;
   CHECK(nir_src_is_const(atomic->src[0]));
   CHECK(nir_src_as_uint(atomic->src[0]) == expected);
   ralloc_free(b.shader);
   alias_leaf_present = false;
}

static void
check_alias_cross_page(bool store, unsigned bits, unsigned components)
{
   alias_leaf_present = true;
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options, "alias page crossing");
   uint64_t addr = base + 16383;
   if (store)
      nir_store_global(&b, nir_imm_zero(&b, components, bits), nir_imm_int64(&b, addr), .align_mul = 1);
   else {
      nir_def *value = nir_load_global(&b, components, bits, nir_imm_int64(&b, addr), .align_mul = 1);
      nir_store_global(&b, value, nir_imm_int64(&b, 0x4000), .align_mul = bits / 8);
   }
   CHECK(hk_nir_lower_memory_aliases(b.shader));
   nir_validate_shader(b.shader, "alias page-crossing lowering");
   fold(b.shader);
   unsigned pieces = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic) continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != (store ? nir_intrinsic_store_global : nir_intrinsic_load_global)) continue;
         unsigned src = store ? 1 : 0;
         CHECK(nir_src_is_const(intr->src[src]));
         CHECK(nir_src_as_uint(intr->src[src]) == 0x60000000 + 16383 + pieces);
         CHECK((store ? intr->src[0].ssa->bit_size : intr->def.bit_size) == 8);
         ++pieces;
      }
   }
   CHECK(pieces == components * bits / 8);
   ralloc_free(b.shader);
   alias_leaf_present = false;
}

static void
check_descriptor_pointer(bool dereference)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options,
                                                  "descriptor versus BDA");
   nir_def *pointer = nir_load_global_constant(&b, 1, 64,
      nir_iadd_imm(&b, nir_load_root_agx(&b), 128), .align_mul = 8);
   nir_def *value = dereference ? nir_load_global_constant(&b, 1, 32, pointer,
                                                           .align_mul = 4) : pointer;
   nir_store_global(&b, value, nir_imm_int64(&b, 0x4000), .align_mul = 4);
   CHECK(hk_nir_lower_sparse_access(b.shader) == dereference);
   nir_validate_shader(b.shader, "strict sparse descriptor/BDA distinction");
   ralloc_free(b.shader);
}

static void
check_alias_vector(bool store, uint64_t address, bool leaf_present,
                   unsigned bits, unsigned components, unsigned alignment,
                   unsigned write_mask)
{
   alias_leaf_present = leaf_present;
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options,
                                                  "page-contained alias vector");
   if (store) {
      nir_store_global(&b, nir_imm_zero(&b, components, bits),
                       nir_imm_int64(&b, address), .align_mul = alignment,
                       .write_mask = write_mask);
   } else {
      nir_def *value = nir_load_global(&b, components, bits,
                                       nir_imm_int64(&b, address),
                                       .align_mul = alignment);
      nir_store_shared(&b, value, nir_imm_int(&b, 0), .align_mul = alignment);
   }
   CHECK(hk_nir_lower_memory_aliases(b.shader));
   nir_validate_shader(b.shader, "page-contained alias vector");
   fold(b.shader);
   const uint64_t primary = address & ~size;
   const bool inside = primary >= base && primary < base + size;
   const uint64_t expected = inside && leaf_present ?
      0x60000000 + ((primary - base) & ((1u << 26) - 1)) :
      inside ? primary : address;
   unsigned accesses = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic) continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != (store ? nir_intrinsic_store_global :
                                         nir_intrinsic_load_global)) continue;
         CHECK(nir_src_as_uint(intr->src[store ? 1 : 0]) == expected);
         CHECK((store ? intr->src[0].ssa->num_components :
                        intr->def.num_components) == components);
         CHECK((store ? intr->src[0].ssa->bit_size : intr->def.bit_size) == bits);
         if (store) CHECK(nir_intrinsic_write_mask(intr) == write_mask);
         ++accesses;
      }
   }
   CHECK(accesses == 1);
   ralloc_free(b.shader);
   alias_leaf_present = false;
}

static void
check_texture_descriptor(void)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options, "texture metadata");
   nir_def *handle = nir_bindless_image_agx(&b, nir_imm_int(&b, 64), .desc_set = 0);
   nir_def *address = nir_load_from_texture_handle_agx(&b, handle);
   nir_def *descriptor = nir_load_global_constant(&b, 4, 32, nir_iadd_imm(&b, address, 16), .align_mul = 16);
   nir_store_shared(&b, descriptor, nir_imm_int(&b, 0), .align_mul = 16);
   CHECK(!hk_nir_lower_memory_aliases(b.shader));
   CHECK(!hk_nir_lower_sparse_access(b.shader));
   nir_validate_shader(b.shader, "texture metadata skips buffer alias lookup");
   ralloc_free(b.shader);
}

static void
check_alias_metadata(unsigned dereferences, bool alias_root)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options,
                                                  "alias metadata provenance");
   nir_def *pointer = nir_load_global_constant(&b, 1, 64,
      nir_iadd_imm(&b, nir_load_root_agx(&b), alias_root ?
         hk_root_descriptor_offset(buffer_alias_table) : 128), .align_mul = 8);
   for (unsigned i = 0; i < dereferences; ++i)
      pointer = nir_load_global_constant(&b, 1, 64,
         nir_iadd_imm(&b, pointer, 8), .align_mul = 8);
   nir_store_shared(&b, pointer, nir_imm_int(&b, 0), .align_mul = 8);
   CHECK(hk_nir_lower_sparse_access(b.shader) ==
         (alias_root ? dereferences > 2 : dereferences > 0));
   nir_validate_shader(b.shader, "alias metadata versus application address");
   ralloc_free(b.shader);
}

static int run_suite(void)
{
   unsigned count = 0;
   const uint64_t addresses[] = {base, base + 16380, base + 16383, base + 32766,
      base + 65535, base + 32 * 16384, base + size + 16383, 0x4000};
   for (unsigned bits = 8; bits <= 64; bits *= 2) {
      for (unsigned c = 1; c <= 4; ++c) {
         for (unsigned a = 0; a < ARRAY_SIZE(addresses); ++a) {
            check_load(addresses[a], bits, c, 1); ++count;
            if (addresses[a] % (bits / 8) == 0) {
               check_load(addresses[a], bits, c, bits / 8); ++count;
            }
         }
      }
   }
   check_atomic(false); check_atomic(true);
   const uint64_t atomic_addresses[] = {base, base + 16388, base + size + 16388,
      base + (1u << 26) + 32772, 0, 0x4000};
   for (unsigned i = 0; i < ARRAY_SIZE(atomic_addresses); ++i) {
      check_buffer_alias(atomic_addresses[i], false);
      check_buffer_alias(atomic_addresses[i], true);
   }
   for (unsigned bits = 8; bits <= 64; bits *= 2)
      for (unsigned components = 1; components <= 4; ++components) {
         check_alias_cross_page(false, bits, components);
         check_alias_cross_page(true, bits, components);
      }
   check_descriptor_pointer(false); check_descriptor_pointer(true);
   const uint64_t vector_addresses[] = {
      base, base + 16352, base + size + 16352, 0x4000,
   };
   for (unsigned bits = 8; bits <= 64; bits *= 2) {
      for (unsigned components = 1; components <= 4; ++components) {
         for (unsigned i = 0; i < ARRAY_SIZE(vector_addresses); ++i) {
            for (unsigned present = 0; present < 2; ++present) {
               check_alias_vector(false, vector_addresses[i], present,
                                  bits, components, 32, 0);
               check_alias_vector(true, vector_addresses[i], present,
                                  bits, components, 32,
                                  BITFIELD_MASK(components));
               check_alias_vector(true, vector_addresses[i], present,
                                  bits, components, 32,
                                  BITFIELD_MASK(components) & 5);
            }
         }
      }
   }
   check_texture_descriptor();
   for (unsigned depth = 0; depth <= 4; ++depth) {
      check_alias_metadata(depth, false);
      check_alias_metadata(depth, true);
   }
   printf("PASS: %u sparse byte/vector masks and bound/unbound atomic execution\n", count);
   return 0;
}

static void
check_direct_access(bool direct, unsigned operation)
{
   alias_leaf_present = true;
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  &options, "direct constant pointer");
   const uint64_t address = base + 16388;
   nir_def *pointer = nir_imm_int64(&b, address |
      (direct ? HK_BUFFER_ADDRESS_DIRECT_BIT : 0));
   nir_intrinsic_instr *load;
   if (operation == 2) {
      load = nir_store_global(&b, nir_imm_int(&b, 1), pointer, .align_mul = 4);
   } else {
      nir_def *value = operation == 0
         ? nir_load_global_constant(&b, 1, 32, pointer, .align_mul = 4)
         : operation == 1 ? nir_load_global(&b, 1, 32, pointer, .align_mul = 4)
         : nir_global_atomic(&b, 32, pointer, nir_imm_int(&b, 1),
                             .atomic_op = nir_atomic_op_iadd);
      load = nir_instr_as_intrinsic(nir_def_instr(value));
   }
   CHECK(hk_nir_lower_memory_aliases(b.shader));
   /* Observe the generated address, leaving the application load itself out
    * of this independent metadata evaluator. */
   b.cursor = nir_after_impl(b.impl);
   nir_intrinsic_instr *store = nir_store_global(
      &b, load->src[operation == 2 ? 1 : 0].ssa,
      nir_imm_int64(&b, 0x4000), .align_mul = 8);
   nir_instr_remove(&load->instr);
   fold(b.shader);
   nir_validate_shader(b.shader, "direct constant address");
   CHECK(nir_src_is_const(store->src[0]));
   CHECK(nir_src_as_uint(store->src[0]) ==
         (direct ? address : 0x60000000 + 16388));
   ralloc_free(b.shader);
   alias_leaf_present = false;
}

static void
check_direct_proof(unsigned form)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  &options, "direct address proof");
   nir_def *x = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_def *address = nir_ior_imm(&b, nir_u2u64(&b, x),
                                 HK_BUFFER_ADDRESS_DIRECT_BIT);
   if (form == 1) {
      nir_def *high = nir_ior_imm(&b, x, BITFIELD_BIT(31));
      address = nir_pack_64_2x32(&b, nir_vec2(&b, x, high));
   } else if (form == 2) {
      address = nir_bcsel(&b, nir_ine_imm(&b, x, 0), address,
                          nir_imm_int64(&b, AGX_ZERO_PAGE_ADDRESS));
   } else if (form == 3) {
      address = nir_iadd(&b, address, nir_u2u64(&b, x));
   } else if (form == 4) {
      /* A BDA fetched from direct backing is not itself known canonical. */
      address = nir_load_global(&b, 1, 64, address, .align_mul = 8);
   } else if (form == 5) {
      address = nir_iadd(&b, address, nir_pack_64_2x32(&b, nir_vec2(&b, x, x)));
   }
   nir_global_atomic(&b, 32, address, nir_imm_int(&b, 1),
                     .atomic_op = nir_atomic_op_iadd);
   CHECK(hk_nir_lower_memory_aliases(b.shader));
   nir_opt_dce(b.shader);
   nir_validate_shader(b.shader, "direct address provenance");
   unsigned lookups = 0;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic &&
             nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_global_constant)
            ++lookups;
      }
   }
   CHECK((lookups == 0) == (form < 4));
   ralloc_free(b.shader);
}

/* Moving the private tag to the base must preserve carries across the low
 * 32-bit address word and the robust zero-page arm. Keep a dynamic offset
 * until after lowering so the test exercises the address-expression path. */
static void
check_direct_base_offset(void)
{
   for (unsigned zero = 0; zero < 2; ++zero) {
      for (unsigned edge = 0; edge < 4; ++edge) {
         const uint32_t offsets[] = {0, 4, 0xfffc, UINT32_MAX - 3};
         nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
            &options, "direct base plus offset");
         nir_def *input = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
         uint64_t base_address = UINT64_C(0x12fffffffc);
         nir_def *base_def = nir_imm_int64(&b,
            base_address | HK_BUFFER_ADDRESS_DIRECT_BIT);
         nir_def *valid = nir_ine_imm(&b, input, 42);
         nir_def *selected = nir_bcsel(&b, valid, base_def,
            nir_imm_int64(&b, AGX_ZERO_PAGE_ADDRESS));
         nir_def *offset = nir_bcsel(&b, valid, input, nir_imm_int(&b, 0));
         nir_def *address = nir_iadd(&b, selected, nir_u2u64(&b, offset));
         nir_def *value = nir_load_global(&b, 1, 32, address, .align_mul = 4);
         nir_intrinsic_instr *load = nir_instr_as_intrinsic(nir_def_instr(value));
         CHECK(hk_nir_lower_memory_aliases(b.shader));
         b.cursor = nir_before_impl(b.impl);
         nir_def_rewrite_uses(input, nir_imm_int(&b, zero ? 42 : offsets[edge]));
         b.cursor = nir_after_impl(b.impl);
         nir_intrinsic_instr *out = nir_store_global(&b, load->src[0].ssa,
            nir_imm_int64(&b, 0x4000), .align_mul = 8);
         nir_instr_remove(&load->instr);
         fold(b.shader);
         nir_validate_shader(b.shader, "direct base offset tag removal");
         CHECK(nir_src_is_const(out->src[0]));
         CHECK(nir_src_as_uint(out->src[0]) ==
               (zero ? AGX_ZERO_PAGE_ADDRESS : base_address + offsets[edge]));
         ralloc_free(b.shader);
      }
   }
}

static void
check_buffer_bounds(void)
{
   const unsigned sizes[] = {1, 2, 3, 4, 8, 12, 16, 32, 64, 128};
   unsigned count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(sizes); ++i) {
      unsigned size = sizes[i];
      uint32_t limits[] = {0, 1, size - 1, size, size + 1, 256,
                          UINT32_MAX - size, UINT32_MAX - 1, UINT32_MAX};
      for (unsigned j = 0; j < ARRAY_SIZE(limits); ++j) {
         uint32_t bound = limits[j];
         uint32_t offsets[] = {0, 1, bound, bound - 1, bound + 1,
                               bound - size, bound - size + 1,
                               UINT32_MAX - size, UINT32_MAX};
         for (unsigned k = 0; k < ARRAY_SIZE(offsets); ++k) {
            nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
               &options, "buffer bound against wide CPU oracle");
            nir_def *valid = hk_nir_buffer_offset_in_bounds(&b,
               nir_imm_int(&b, offsets[k]), nir_imm_int(&b, bound), size);
            nir_intrinsic_instr *out = nir_store_global(&b, nir_b2i32(&b, valid),
               nir_imm_int64(&b, 0x4000), .align_mul = 4);
            nir_opt_constant_folding(b.shader);
            CHECK(nir_src_is_const(out->src[0]));
            CHECK(nir_src_as_uint(out->src[0]) ==
                  ((uint64_t)offsets[k] + size <= bound));
            ++count;
            ralloc_free(b.shader);
         }
      }
   }
   printf("PASS: %u buffer bounds against a 64-bit CPU oracle\n", count);
}

static void
check_coalesced_bounded(void)
{
   const uint32_t bounds[] = {0, 1, 3, 4, 7, 8, 12, 15, 16, 17, 31, 32, UINT32_MAX};
   const uint32_t offsets[] = {0, 16, 32, 0xfffffff0};
   unsigned cases = 0;
   for (unsigned bi = 0; bi < ARRAY_SIZE(bounds); ++bi) {
      for (unsigned oi = 0; oi < ARRAY_SIZE(offsets); ++oi) {
         nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
            &options, "coalesced loads with exact scalar fallback");
         nir_def *id = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
         nir_def *base = nir_imm_int64(&b, 0x8000);
         nir_def *bound = nir_imm_int(&b, bounds[bi]);
         nir_def *offset = nir_imul_imm(&b, id, 16);
         nir_def *values[4];
         for (unsigned c = 0; c < 4; ++c)
            values[c] = nir_load_global_bounded(&b, 1, 32, base,
               nir_iadd_imm(&b, offset, c * 4), bound,
               .align_mul = 16, .align_offset = c * 4,
               .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);
         nir_intrinsic_instr *out = nir_store_global(&b, nir_vec(&b, values, 4),
            nir_imm_int64(&b, 0x4000), .align_mul = 16);
         CHECK(hk_nir_coalesce_bounded(b.shader));
         nir_validate_shader(b.shader, "coalesced bounded loads");
         b.cursor = nir_before_impl(b.impl);
         nir_def_rewrite_uses(id, nir_imm_int(&b, offsets[oi] / 16));
         bool progress;
         do {
            progress = false;
            NIR_PASS(progress, b.shader, nir_opt_constant_folding);
            NIR_PASS(progress, b.shader, nir_opt_dead_cf);
            NIR_PASS(progress, b.shader, nir_opt_dce);
         } while (progress);
         nir_foreach_block(block, b.impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;
               nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
               if (in->intrinsic != nir_intrinsic_load_global &&
                   in->intrinsic != nir_intrinsic_load_global_bounded)
                  continue;
               bool vector = in->intrinsic == nir_intrinsic_load_global;
               CHECK(nir_src_is_const(in->src[0]));
               uint32_t off = vector ? nir_src_as_uint(in->src[0]) - 0x8000
                                    : nir_src_as_uint(in->src[1]);
               unsigned count = in->def.num_components;
               if (vector) {
                  CHECK(count == 4);
                  CHECK((uint64_t)off + 16 <= bounds[bi]);
               }
               b.cursor = nir_before_instr(instr);
               nir_def *data[4];
               for (unsigned c = 0; c < count; ++c) {
                  uint64_t at = (uint64_t)off + 4 * c;
                  uint32_t word = (uint64_t)off + 4 * count <= bounds[bi] ? (uint32_t)at ^ 0xa533cc77 : 0;
                  data[c] = nir_imm_int(&b, word);
               }
               nir_def_replace(&in->def, nir_vec(&b, data, count));
            }
         }
         do {
            progress = false;
            NIR_PASS(progress, b.shader, nir_opt_constant_folding);
            NIR_PASS(progress, b.shader, nir_opt_copy_prop);
            NIR_PASS(progress, b.shader, nir_opt_dce);
         } while (progress);
         CHECK(nir_src_is_const(out->src[0]));
         for (unsigned c = 0; c < 4; ++c) {
            uint64_t at = (uint64_t)offsets[oi] + c * 4;
            uint32_t expected = at + 4 <= bounds[bi] ? (uint32_t)at ^ 0xa533cc77 : 0;
            CHECK(nir_src_comp_as_uint(out->src[0], c) == expected);
         }
         ++cases;
         ralloc_free(b.shader);
      }
   }
   printf("PASS: %u coalesced-load boundaries against scalar memory oracle\n", cases);
}

static void
check_coalesced_rejections(void)
{
   for (unsigned mode = 0; mode < 4; ++mode) {
      nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
         &options, "coalesced load rejection boundaries");
      nir_def *base = nir_imm_int64(&b, 0x8000);
      nir_def *bound = nir_imm_int(&b, 128);
      nir_def *offset = nir_imul_imm(&b,
         nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0), 16);
      nir_def *values[4];
      for (unsigned c = 0; c < 4; ++c) {
         if (mode == 3 && c == 2)
            nir_store_global(&b, nir_imm_int(&b, 7), base, .align_mul = 4);
         values[c] = nir_load_global_bounded(&b, 1, 32, base,
            nir_iadd_imm(&b, offset, c * 4), bound,
            .align_mul = mode == 0 ? 4 : 16,
            .align_offset = mode == 0 ? 0 : c * 4,
            .access = (mode == 1 ? ACCESS_VOLATILE : 0) |
                      (mode == 2 ? 0 : ACCESS_NON_WRITEABLE));
      }
      nir_store_global(&b, nir_vec(&b, values, 4),
         nir_imm_int64(&b, 0x4000), .align_mul = 16);
      CHECK(!hk_nir_coalesce_bounded(b.shader));
      nir_validate_shader(b.shader, "rejected unsafe grouping");
      ralloc_free(b.shader);
   }
   puts("PASS: coalescing rejects unaligned, volatile, mutable and intervening stores");
}

int main(void)
{
   check_coalesced_bounded();
   check_coalesced_rejections();
   check_buffer_bounds();
   check_direct_base_offset();
   for (unsigned form = 0; form < 6; ++form)
      check_direct_proof(form);
   for (unsigned i = 0; i < 2; ++i) {
      specialize_geometry = i;
      run_suite();
      for (unsigned operation = 0; operation < 4; ++operation) {
         check_direct_access(false, operation);
         check_direct_access(true, operation);
      }
   }
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  &options, "window specialization");
   const unsigned offsets[] = {
      hk_root_descriptor_offset(sparse_va_base),
      hk_root_descriptor_offset(sparse_va_size),
      hk_root_descriptor_offset(sparse_residency_map),
      hk_root_descriptor_offset(buffer_alias_table),
   };
   nir_intrinsic_instr *stores[4];
   for (unsigned i = 0; i < 4; ++i) {
      nir_def *word = nir_load_global_constant(&b, 1, 64,
         nir_iadd_imm(&b, nir_load_root_agx(&b), offsets[i]), .align_mul = 8);
      stores[i] = nir_store_global(&b, word, nir_imm_int64(&b, 0x4000 + i * 8),
                                    .align_mul = 8);
   }
   CHECK(hk_nir_specialize_memory_window(b.shader, 0x8000000000ull, 0x4000000000ull));
   CHECK(nir_src_is_const(stores[0]->src[0]));
   CHECK(nir_src_as_uint(stores[0]->src[0]) == 0x8000000000ull);
   CHECK(nir_src_as_uint(stores[1]->src[0]) == 0x4000000000ull);
   CHECK(!nir_src_is_const(stores[2]->src[0]));
   CHECK(!nir_src_is_const(stores[3]->src[0]));
   CHECK(!hk_nir_specialize_memory_window(b.shader, 0x8000000000ull, 0x4000000000ull));
   nir_validate_shader(b.shader, "immutable geometry only");
   ralloc_free(b.shader);
   puts("PASS: immutable memory window specialization preserves dynamic tables");
   return 0;
}
