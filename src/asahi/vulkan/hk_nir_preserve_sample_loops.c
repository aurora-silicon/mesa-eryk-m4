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

/* Reconsider small loops only after native resource specialization removed
 * expensive sampler fallbacks. The early hint protects compilation from the
 * large generic expansion; keeping it forever can hide constant indices and
 * serialize a short sequence of otherwise independent texture operations. */
bool
hk_nir_release_short_sample_loops(nir_shader *nir)
{
   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      nir_metadata_require(impl, nir_metadata_loop_analysis, nir_var_all, 0);
      nir_foreach_block(block, impl) {
         nir_cf_node *node = block->cf_node.parent;
         if (!node || node->type != nir_cf_node_loop)
            continue;
         nir_loop *loop = nir_cf_node_as_loop(node);
         if (loop->control != nir_loop_control_dont_unroll ||
             !loop->info->exact_trip_count_known ||
             !loop->info->max_trip_count || loop->info->max_trip_count > 4 ||
             loop->info->instr_cost > 128)
            continue;
         bool nested = false;
         for (nir_cf_node *parent = node->parent;
              parent && parent->type != nir_cf_node_function; parent = parent->parent)
            nested |= parent->type == nir_cf_node_loop;
         nir_foreach_block_in_cf_node(child, node) {
            for (nir_cf_node *parent = child->cf_node.parent;
                 parent && parent != node; parent = parent->parent)
               nested |= parent->type == nir_cf_node_loop;
         }
         if (nested)
            continue;
         loop->control = nir_loop_control_none;
         progress = true;
      }
   }
   nir_shader_preserve_all_metadata(nir);
   return progress;
}
