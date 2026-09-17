/* SPDX-License-Identifier: MIT */
#include "hk_nir_preserve_sample_loops.h"

/* G16 filtering/reduction and border handling expand a sampled texture far
 * beyond the cost seen by early loop analysis. Unrolling a cheap-looking
 * source loop first duplicates that expansion hundreds of times. Preserve
 * these loops through preprocessing and later optimization; unrelated loops
 * retain the normal unrolling policy. This changes no sampling semantics.
 */
bool
hk_nir_preserve_sample_loops(nir_shader *nir)
{
   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_tex ||
                !nir_tex_instr_need_sampler(nir_instr_as_tex(instr)))
               continue;
            for (nir_cf_node *node = block->cf_node.parent;
                 node && node->type != nir_cf_node_function; node = node->parent) {
               if (node->type != nir_cf_node_loop)
                  continue;
               nir_loop *loop = nir_cf_node_as_loop(node);
               progress |= loop->control != nir_loop_control_dont_unroll;
               loop->control = nir_loop_control_dont_unroll;
            }
         }
      }
   }
   /* Only optimization hints changed; CFG, SSA and loop analysis stay valid. */
   nir_shader_preserve_all_metadata(nir);
   return progress;
}
