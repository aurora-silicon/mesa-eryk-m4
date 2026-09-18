/* SPDX-License-Identifier: MIT */
#include "hk_cmd_buffer.h"
#include "hk_shader.h"
#include "compiler/nir/nir_builder.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", __LINE__, #x); abort(); } } while (0)
static const nir_shader_compiler_options options = {0};
static bool alias_leaf_present;
static const uint64_t base = 0x20000000, size = 0x10000000, map = 0x80000000;

static void
fold(nir_shader *s)
{
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

int main(void)
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
   check_texture_descriptor();
   for (unsigned depth = 0; depth <= 4; ++depth) {
      check_alias_metadata(depth, false);
      check_alias_metadata(depth, true);
   }
   printf("PASS: %u sparse byte/vector masks and bound/unbound atomic execution\n", count);
   return 0;
}
