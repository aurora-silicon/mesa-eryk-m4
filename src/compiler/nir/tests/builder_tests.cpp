/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir_test.h"

namespace {

class nir_builder_test : public nir_test {
private:
   const glsl_type *type_for_def(nir_def *def)
   {
      switch (def->bit_size) {
      case 8:  return glsl_u8vec_type(def->num_components);
      case 16: return glsl_u16vec_type(def->num_components);
      case 32: return glsl_uvec_type(def->num_components);
      case 64: return glsl_u64vec_type(def->num_components);
      default: UNREACHABLE("Invalid bit size");
      }
   }

protected:
   nir_builder_test()
      : nir_test::nir_test("nir_builder_test")
   {
   }

   void store_test_val(nir_def *val)
   {
      nir_variable *var = nir_variable_create(b->shader, nir_var_mem_ssbo,
                                              type_for_def(val), NULL);
      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_deref);
      store->num_components = val->num_components;
      store->src[0] = nir_src_for_ssa(&nir_build_deref_var(b, var)->def);
      store->src[1] = nir_src_for_ssa(val);
      nir_intrinsic_set_write_mask(store, ((1 << val->num_components) - 1));
      nir_builder_instr_insert(b, &store->instr);

      stores.push_back(store);
   }

   nir_def *test_val(unsigned idx)
   {
      return stores[idx]->src[1].ssa;
   }

   std::vector<nir_intrinsic_instr *> stores;
};

/* Allow grouping the tests while still sharing the helpers. */
class nir_extract_bits_test : public nir_builder_test {};

} // namespace

// TODO: Re-enable this once we get vec8 support in NIR
TEST_F(nir_extract_bits_test, DISABLED_unaligned8)
{
   nir_def *srcs[] = {
      nir_imm_int(b, 0x03020100),
      nir_imm_ivec2(b, 0x07060504, 0x0b0a0908),
   };

   store_test_val(nir_extract_bits(b, srcs, 2, 24, 1, 64));

   NIR_PASS(_, b->shader, nir_opt_constant_folding);

   nir_src val = nir_src_for_ssa(test_val(0));

   ASSERT_EQ(nir_src_as_uint(val), 0x0a09080706050403);
}

TEST_F(nir_extract_bits_test, unaligned16_disabled)
{
   nir_def *srcs[] = {
      nir_imm_int(b, 0x03020100),
      nir_imm_ivec2(b, 0x07060504, 0x0b0a0908),
   };

   store_test_val(nir_extract_bits(b, srcs, 2, 16, 1, 64));

   NIR_PASS(_, b->shader, nir_opt_constant_folding);

   nir_src val = nir_src_for_ssa(test_val(0));

   ASSERT_EQ(nir_src_as_uint(val), 0x0908070605040302);
}

TEST_F(nir_extract_bits_test, mixed_bit_sizes)
{
   nir_def *srcs[] = {
      nir_imm_int(b, 0x03020100),
      nir_imm_intN_t(b, 0x04, 8),
      nir_imm_intN_t(b, 0x08070605, 32),
      nir_vec2(b, nir_imm_intN_t(b, 0x0a09, 16),
                  nir_imm_intN_t(b, 0x0c0b, 16)),
   };

   store_test_val(nir_extract_bits(b, srcs, 4, 24, 2, 32));

   NIR_PASS(_, b->shader, nir_opt_constant_folding);

   nir_src val = nir_src_for_ssa(test_val(0));

   ASSERT_EQ(nir_src_comp_as_uint(val, 0), 0x06050403);
   ASSERT_EQ(nir_src_comp_as_uint(val, 1), 0x0a090807);
}

/* Repeated library calls inserted into an existing block must keep every
 * caller and callee definition in a distinct SSA index slot, including phis. */
TEST_F(nir_builder_test, inline_function_ssa_indices)
{
   nir_builder callee = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, b->shader->options, "inline callee");
   nir_function *func = callee.impl->function;
   func->num_params = 1;
   func->params = rzalloc_array(func, nir_parameter, 1);
   func->params[0].num_components = 1;
   func->params[0].bit_size = 32;
   nir_def *param = nir_load_param(&callee, 0);
   nir_if *branch = nir_push_if(&callee, nir_ilt_imm(&callee, param, 10));
   nir_def *yes = nir_iadd_imm(&callee, param, 1);
   nir_push_else(&callee, branch);
   nir_def *no = nir_iadd_imm(&callee, param, 2);
   nir_pop_if(&callee, branch);
   nir_bindgen_return(&callee, nir_if_phi(&callee, yes, no));

   nir_def *arg = nir_imm_int(b, 5);
   nir_def *existing = nir_iadd_imm(b, arg, 100);
   const unsigned existing_index = existing->index;
   for (unsigned i = 0; i < 3; ++i) {
      b->cursor = nir_before_instr(nir_def_instr(existing));
      arg = nir_inline_function_impl(b, callee.impl, &arg, NULL);
      ASSERT_NE(arg, nullptr);
   }
   b->cursor = nir_after_impl(b->impl);
   store_test_val(arg);
   nir_validate_shader(b->shader, "repeated inlining");
   EXPECT_EQ(existing->index, existing_index);
   std::vector<bool> used(b->impl->ssa_alloc, false);
   nir_foreach_block(block, b->impl) {
      nir_foreach_instr(instr, block) {
         nir_def *def = nir_instr_def(instr);
         if (def) {
            ASSERT_LT(def->index, used.size());
            EXPECT_FALSE(used[def->index]);
            used[def->index] = true;
         }
      }
   }
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, b->shader, nir_opt_constant_folding);
      NIR_PASS(progress, b->shader, nir_opt_if, nir_opt_if_optimize_phi_true_false);
      NIR_PASS(progress, b->shader, nir_opt_dead_cf);
      NIR_PASS(progress, b->shader, nir_opt_remove_phis);
   } while (progress);
   ASSERT_TRUE(nir_src_is_const(stores.back()->src[1]));
   EXPECT_EQ(nir_src_as_uint(stores.back()->src[1]), 8u);
   ralloc_free(callee.shader);
}
