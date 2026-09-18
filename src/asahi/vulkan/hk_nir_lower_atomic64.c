/* SPDX-License-Identifier: MIT */
#include "compiler/nir/nir_builder.h"
#include "hk_cmd_buffer.h"
#include "hk_shader.h"

static void
atomic_fence(nir_builder *b, nir_memory_semantics semantics, bool shared)
{
   nir_barrier(b, .execution_scope = SCOPE_NONE,
               .memory_scope = shared ? SCOPE_WORKGROUP : SCOPE_DEVICE,
               .memory_semantics = semantics,
               .memory_modes = shared ? nir_var_mem_shared : nir_var_mem_global);
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
lower_atomic(nir_builder *b, nir_intrinsic_instr *intr, void *data)
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

   nir_def *lock;
   if (shared) {
      /* Shared storage belongs to one workgroup. A device-wide hash would
       * make independent workgroups wait for each other and can deadlock
       * when the lock holder cannot make progress alongside the contenders.
       */
      assert(*(unsigned *)data != UINT_MAX);
      lock = nir_imm_int(b, *(unsigned *)data);
   } else {
      lock = nir_load_global_constant(
         b, 1, 64,
         nir_iadd_imm(b, nir_load_root_agx(b),
                      hk_root_descriptor_offset(atomic64_lock)),
         .align_mul = 8, .access = ACCESS_CAN_SPECULATE);

      /* Buffer aliases have already been canonicalized. All mappings
       * preserve the offset within a 16-KiB GPU page, so aliased virtual
       * addresses select the same device lock. */
      nir_def *offset = nir_ushr_imm(
         b, nir_iand_imm(b, nir_u2u32(b, address), 0x3ff8), 1);
      lock = nir_iadd(b, lock, nir_u2u64(b, offset));
   }

   /* A winning lane releases before leaving this loop, so lanes still
    * retrying never wait for an inactive winner in their own subgroup. */
   nir_push_loop(b);
   nir_def *acquired =
      shared ? nir_shared_atomic_swap(b, 32, lock, nir_imm_int(b, 0),
                                       nir_imm_int(b, 1),
                                       .atomic_op = nir_atomic_op_cmpxchg)
             : nir_global_atomic_swap(b, 32, lock, nir_imm_int(b, 0),
                                       nir_imm_int(b, 1),
                                       .atomic_op = nir_atomic_op_cmpxchg);
   nir_push_if(b, nir_ieq_imm(b, acquired, 0));
   atomic_fence(b, NIR_MEMORY_ACQUIRE, shared);

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
   atomic_fence(b, NIR_MEMORY_RELEASE, shared);
   if (shared)
      nir_shared_atomic(b, 32, lock, nir_imm_int(b, 0),
                        .atomic_op = nir_atomic_op_xchg);
   else
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

/* Allocate and initialize the private lock before application control flow.
 * Even invocations that return early must participate in this initial barrier.
 * The launch reserves one extra allocation granule, preserving the public
 * 32-KiB application budget when all of it is used by the shader.
 */
static unsigned
initialize_shared_lock(nir_shader *nir)
{
   bool needed = false;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            needed |= (intr->intrinsic == nir_intrinsic_shared_atomic ||
                       intr->intrinsic == nir_intrinsic_shared_atomic_swap) &&
                      intr->def.bit_size == 64;
         }
      }
   }

   if (!needed)
      return UINT_MAX;

   const unsigned offset = ALIGN_POT(nir->info.shared_size, 4);
   nir->info.shared_size = offset + 4;
   nir_builder b =
      nir_builder_at(nir_before_impl(nir_shader_get_entrypoint(nir)));
   nir_push_if(&b, nir_ieq_imm(&b, nir_load_local_invocation_index(&b), 0));
   nir_store_shared(&b, nir_imm_int(&b, 0), nir_imm_int(&b, offset),
                    .align_mul = 4);
   nir_pop_if(&b, NULL);
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP,
               .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL,
               .memory_modes = nir_var_mem_shared);
   return offset;
}

bool
hk_nir_lower_atomic64(nir_shader *nir)
{
   unsigned shared_lock = initialize_shared_lock(nir);
   bool progress = nir_shader_intrinsics_pass(
      nir, lower_atomic, nir_metadata_none, &shared_lock);
   if (progress)
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   return progress;
}
