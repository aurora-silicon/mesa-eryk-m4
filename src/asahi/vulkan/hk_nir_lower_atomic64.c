/* SPDX-License-Identifier: MIT */
#include "compiler/nir/nir_builder.h"
#include "hk_cmd_buffer.h"
#include "hk_shader.h"

static void
device_fence(nir_builder *b, nir_memory_semantics semantics)
{
   nir_barrier(b, .execution_scope = SCOPE_NONE, .memory_scope = SCOPE_DEVICE,
               .memory_semantics = semantics,
               .memory_modes = nir_var_mem_global);
}

static nir_def *
updated_value(nir_builder *b, nir_intrinsic_instr *intr, nir_def *old)
{
   nir_atomic_op op = nir_intrinsic_atomic_op(intr);
   const bool agx = intr->intrinsic == nir_intrinsic_global_atomic_agx ||
                    intr->intrinsic == nir_intrinsic_global_atomic_swap_agx;
   unsigned source = agx ? 2 : 1;
   nir_def *value = intr->src[source].ssa;
   switch (op) {
   case nir_atomic_op_xchg:
      return value;
   case nir_atomic_op_cmpxchg:
      return nir_bcsel(b, nir_ieq(b, old, value), intr->src[source + 1].ssa,
                       old);
   default:
      return nir_build_alu2(b, nir_atomic_op_to_alu(op), old, value);
   }
}

static bool
lower_atomic(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if ((intr->intrinsic != nir_intrinsic_global_atomic &&
        intr->intrinsic != nir_intrinsic_global_atomic_swap &&
        intr->intrinsic != nir_intrinsic_global_atomic_agx &&
        intr->intrinsic != nir_intrinsic_global_atomic_swap_agx &&
        intr->intrinsic != nir_intrinsic_shared_atomic &&
        intr->intrinsic != nir_intrinsic_shared_atomic_swap) ||
       intr->def.bit_size != 64)
      return false;

   switch (nir_intrinsic_atomic_op(intr)) {
   case nir_atomic_op_iadd:
   case nir_atomic_op_imin:
   case nir_atomic_op_umin:
   case nir_atomic_op_imax:
   case nir_atomic_op_umax:
   case nir_atomic_op_iand:
   case nir_atomic_op_ior:
   case nir_atomic_op_ixor:
   case nir_atomic_op_xchg:
   case nir_atomic_op_cmpxchg:
      break;
   default:
      return false;
   }

   b->cursor = nir_before_instr(&intr->instr);
   nir_variable *result = nir_local_variable_create(
      b->impl, glsl_uint64_t_type(), "atomic64_result");
   nir_store_var(b, result, nir_imm_int64(b, 0), 1);

   /* A helper cannot acquire an atomic lock. Exclude it explicitly rather
    * than relying on an undefined returned value from a suppressed atomic. */
   const bool fragment = b->shader->info.stage == MESA_SHADER_FRAGMENT;
   if (fragment)
      nir_push_if(b, nir_inot(b, nir_load_helper_invocation(b, 1)));

   nir_def *lock = nir_load_global_constant(
      b, 1, 64,
      nir_iadd_imm(b, nir_load_root_agx(b),
                   hk_root_descriptor_offset(atomic64_lock)),
      .align_mul = 8, .access = ACCESS_CAN_SPECULATE);
   const bool shared = intr->intrinsic == nir_intrinsic_shared_atomic ||
                       intr->intrinsic == nir_intrinsic_shared_atomic_swap;
   nir_def *address = intr->src[0].ssa;
   if (shared)
      address = nir_iadd_imm(b, address, nir_intrinsic_base(intr));
   if (intr->intrinsic == nir_intrinsic_global_atomic_agx ||
       intr->intrinsic == nir_intrinsic_global_atomic_swap_agx) {
      nir_def *index = nir_intrinsic_sign_extend(intr)
                          ? nir_i2i64(b, intr->src[1].ssa)
                          : nir_u2u64(b, intr->src[1].ssa);
      address = nir_iadd(b, address, nir_ishl_imm(b, index, 3));
   }
   nir_def *high_address = nir_iadd_imm(b, address, 4);

   /* All mappings preserve the offset within a 16-KiB GPU page. Select a
    * lock by the aligned word's page offset so aliased virtual addresses
    * still agree, while unrelated texels need not serialize globally.
    * Shared addresses may collide across workgroups, which is harmless. */
   nir_def *lock_offset = nir_ushr_imm(
      b, nir_iand_imm(b, nir_u2u32(b, address), 0x3ff8), 1);
   lock = nir_iadd(b, lock, nir_u2u64(b, lock_offset));

   /* Buffer aliases have already been canonicalized. Keep each 64-bit
    * operation under its device-wide lock, using native 32-bit atomic
    * accesses for the halves and device fences around the update.
    * A winning lane releases before leaving this loop, so lanes still
    * retrying never wait for a lock held by an inactive lane of their warp. */
   nir_push_loop(b);
   nir_def *acquired =
      nir_global_atomic_swap(b, 32, lock, nir_imm_int(b, 0), nir_imm_int(b, 1),
                             .atomic_op = nir_atomic_op_cmpxchg);
   nir_push_if(b, nir_ieq_imm(b, acquired, 0));
   device_fence(b, NIR_MEMORY_ACQUIRE);

   nir_def *low = shared ? nir_shared_atomic(b, 32, address, nir_imm_int(b, 0),
                                             .atomic_op = nir_atomic_op_ior)
                         : nir_global_atomic(b, 32, address, nir_imm_int(b, 0),
                                             .atomic_op = nir_atomic_op_ior);
   nir_def *high =
      shared ? nir_shared_atomic(b, 32, high_address, nir_imm_int(b, 0),
                                 .atomic_op = nir_atomic_op_ior)
             : nir_global_atomic(b, 32, high_address, nir_imm_int(b, 0),
                                 .atomic_op = nir_atomic_op_ior);
   nir_def *old = nir_pack_64_2x32_split(b, low, high);
   nir_def *value = updated_value(b, intr, old);
   if (shared) {
      nir_shared_atomic(b, 32, address, nir_unpack_64_2x32_split_x(b, value),
                        .atomic_op = nir_atomic_op_xchg);
      nir_shared_atomic(b, 32, high_address,
                        nir_unpack_64_2x32_split_y(b, value),
                        .atomic_op = nir_atomic_op_xchg);
   } else {
      nir_global_atomic(b, 32, address, nir_unpack_64_2x32_split_x(b, value),
                        .atomic_op = nir_atomic_op_xchg);
      nir_global_atomic(b, 32, high_address,
                        nir_unpack_64_2x32_split_y(b, value),
                        .atomic_op = nir_atomic_op_xchg);
   }
   device_fence(b, NIR_MEMORY_RELEASE);
   nir_global_atomic(b, 32, lock, nir_imm_int(b, 0),
                     .atomic_op = nir_atomic_op_xchg);
   nir_store_var(b, result, old, 1);
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, NULL);
   nir_pop_loop(b, NULL);

   if (fragment)
      nir_pop_if(b, NULL);

   nir_def_replace(&intr->def, nir_load_var(b, result));
   return true;
}

bool
hk_nir_lower_atomic64(nir_shader *nir)
{
   bool progress =
      nir_shader_intrinsics_pass(nir, lower_atomic, nir_metadata_none, NULL);
   if (progress)
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   return progress;
}
