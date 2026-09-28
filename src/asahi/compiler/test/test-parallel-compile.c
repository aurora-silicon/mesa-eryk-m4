/* Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_compile.h"
#include "agx_debug.h"
#include "agx_nir.h"
#include "c11/threads.h"
#include "nir_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORKERS 8
#define SHADERS 16

static mtx_t mutex;
static cnd_t condition;
static unsigned ready;
static bool start;

struct worker {
   struct agx_shader_part parts[SHADERS];
};

static bool
compile(unsigned seed, struct agx_shader_part *part)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "parallel AGX compilation");
   b.shader->info.workgroup_size[0] = 32;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   nir_def *root = nir_load_preamble(&b, 1, 64, .base = 0);
   nir_def *index = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_def *value = nir_load_global_constant(
      &b, 1, 32, nir_iadd_imm(&b, root, seed * 4), .align_mul = 4,
      .access = ACCESS_CAN_SPECULATE);
   for (unsigned i = 0; i < 12; ++i)
      value = nir_ixor(&b, nir_imul_imm(&b, value, 0x1234567 + 2 * i),
                      nir_iadd_imm(&b, index, seed + i));
   nir_store_global(&b, value,
      nir_iadd(&b, nir_imm_int64(&b, 0x8000),
               nir_u2u64(&b, nir_imul_imm(&b, index, 4))), .align_mul = 4);
   agx_preprocess_nir(b.shader);
   struct agx_shader_key key = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .reserved_preamble = 8,
      .has_scratch = true,
      .promote_constants = true,
   };
   bool ok = agx_compile_shader_nir(b.shader, &key, part);
   ralloc_free(b.shader);
   return ok;
}

static int
worker_main(void *data)
{
   struct worker *worker = data;
   mtx_lock(&mutex);
   ready++;
   cnd_broadcast(&condition);
   while (!start)
      cnd_wait(&condition, &mutex);
   mtx_unlock(&mutex);

   /* Race the first publication, rather than initializing on the main thread. */
   if (agx_get_compiler_debug() != AGX_DBG_NOPROMOTE)
      return 1;
   for (unsigned i = 0; i < SHADERS; ++i) {
      if (!compile(i, &worker->parts[i]))
         return 1;
   }
   return 0;
}

int main(void)
{
   glsl_type_singleton_init_or_ref();
   if (mtx_init(&mutex, mtx_plain) != thrd_success ||
       cnd_init(&condition) != thrd_success)
      return 1;
   struct worker workers[WORKERS] = {0};
   thrd_t threads[WORKERS];
   for (unsigned i = 0; i < WORKERS; ++i) {
      if (thrd_create(&threads[i], worker_main, &workers[i]) != thrd_success)
         return 1;
   }
   mtx_lock(&mutex);
   while (ready != WORKERS)
      cnd_wait(&condition, &mutex);
   start = true;
   cnd_broadcast(&condition);
   mtx_unlock(&mutex);
   bool ok = true;
   for (unsigned i = 0; i < WORKERS; ++i) {
      int result;
      if (thrd_join(threads[i], &result) != thrd_success || result)
         ok = false;
   }
   for (unsigned i = 0; ok && i < SHADERS; ++i) {
      struct agx_shader_part reference = {0};
      ok = compile(i, &reference);
      for (unsigned j = 0; ok && j < WORKERS; ++j) {
         const struct agx_shader_part *part = &workers[j].parts[i];
         ok = part->binary && reference.binary &&
              !memcmp(&part->info, &reference.info, sizeof(part->info)) &&
              !memcmp(part->binary, reference.binary, reference.info.binary_size);
      }
      free(reference.binary);
   }
   for (unsigned i = 0; i < WORKERS; ++i)
      for (unsigned j = 0; j < SHADERS; ++j)
         free(workers[i].parts[j].binary);
   cnd_destroy(&condition);
   mtx_destroy(&mutex);
   glsl_type_singleton_decref();
   printf("%s: 128 concurrent native compilations match serial binaries and metadata\n",
          ok ? "PASS" : "FAIL");
   return !ok;
}
