/* SPDX-License-Identifier: MIT
 * Complete NIR->native compile test for packed normalized memory operations.
 */
#undef NDEBUG
#include "agx_compile.h"
#include "compiler/nir/nir_builder.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
   unsigned bytes[2], instructions[2];
   for (unsigned packed=0;packed<2;++packed) {
      nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
         &agx_nir_options,"RGB10 formatted memory roundtrip");
      b.shader->info.workgroup_size[0]=1;
      b.shader->info.workgroup_size[1]=1;
      b.shader->info.workgroup_size[2]=1;
      enum pipe_format format=packed?PIPE_FORMAT_R10G10B10A2_UNORM:PIPE_FORMAT_R32_UINT;
      nir_def *value=nir_load_agx(&b,packed?4:1,32,
         nir_imm_int64(&b,0x100000),nir_imm_int(&b,0),.format=format);
      nir_store_agx(&b,value,nir_imm_int64(&b,0x200000),nir_imm_int(&b,0),.format=format);
      agx_preprocess_nir(b.shader);
      struct agx_shader_key key={.dev.encoding=AGX_ISA_ENCODING_AGX3B,.no_preamble=true};
      struct agx_shader_part part={0};
      assert(agx_compile_shader_nir(b.shader,&key,&part));
      assert(part.binary && !part.info.scratch_size);
      bytes[packed]=part.info.main_size;instructions[packed]=part.info.stats.instrs;
      printf("%s instructions=%u main_bytes=%u\n",packed?"formatted":"raw",instructions[packed],bytes[packed]);
      free(part.binary);ralloc_free(b.shader);
   }
   assert(instructions[1]==instructions[0]+2);
   assert(bytes[1]<=bytes[0]+32);
   puts("PASS full NIR->native RGB10 memory pipeline adds one pack and one unpack to raw32-bit load/store");
}
