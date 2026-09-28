#!/usr/bin/env python3
"""Exercise actual shader deserialization with corrupt payloads and mocked upload.

Host-only: no kernel, Vulkan device or GPU access. The bounded byte reader and
allocation/upload mocks check ordering and ownership; hardware cache reuse is a
separate qualification.
"""
from pathlib import Path
import subprocess
import tempfile

source = Path('mesa/m3m4-merged/src/asahi/vulkan/hk_shader.c').read_text()
start = source.index('static VkResult\nhk_deserialize_shader(')
end = source.index('\nstatic VkResult\nhk_deserialize_api_shader(', start)
body = source[start:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define MESA_SHADER_COMPUTE 5
struct hk_device { int unused; };
struct hk_shader_info { int stage; };
struct agx_shader_info {
   int stage; uint32_t binary_size,main_offset,main_size,preamble_offset;
   bool has_preamble;
   struct {uint32_t offset;uint16_t size_16;} rodata;
};
struct agx_vdm_state_update_program_info {uint32_t unused;};
struct blob_reader {const uint8_t *current,*end;bool overrun;};
struct hk_shader {
   struct hk_shader_info info;
   struct {struct agx_shader_info info;void *binary;} b;
   struct agx_vdm_state_update_program_info agx3_vdm_state_update;
   uint32_t code_size,data_size;const void *code_ptr,*data_ptr;
};
static unsigned allocations,init_calls,uploads,fail_at;
static const void *blob_read_bytes(struct blob_reader *b,size_t n) {
   if(n>(size_t)(b->end-b->current)){b->overrun=true;return NULL;}
   const void *p=b->current;b->current+=n;return p;
}
static void blob_copy_bytes(struct blob_reader *b,void *p,size_t n) {
   const void *s=blob_read_bytes(b,n);if(s&&n)memcpy(p,s,n);
}
static uint32_t blob_read_uint32(struct blob_reader *b) {
   uint32_t n=0;blob_copy_bytes(b,&n,sizeof(n));return n;
}
static bool hk_agx3_vdm_program_cache_deserialize(struct blob_reader *b,struct agx_vdm_state_update_program_info *p) {
   blob_copy_bytes(b,p,sizeof(*p));return !b->overrun;
}
#define vk_error(dev,result) (result)
static VkResult hk_init_link_ht(struct hk_shader *s,int stage) {
   (void)s;assert(stage>=0&&stage<=MESA_SHADER_COMPUTE);init_calls++;return VK_SUCCESS;
}
static VkResult hk_upload_shader(struct hk_device *d,struct hk_shader *s) {
   (void)d;assert(s->b.binary==s->code_ptr);uploads++;return VK_SUCCESS;
}
static void *checked_malloc(size_t n) {
   assert(n&&n<128);allocations++;if(allocations==fail_at)return NULL;return malloc(n);
}
#define malloc checked_malloc
/*BODY*/
#undef malloc
static VkResult run(struct hk_shader_info i,struct agx_shader_info b,
                    uint32_t code,uint32_t data,size_t payload) {
   uint8_t bytes[512],*p=bytes;
#define PUT(v) do {memcpy(p,&(v),sizeof(v));p+=sizeof(v);} while(0)
   PUT(i);PUT(b);struct agx_vdm_state_update_program_info v={0};PUT(v);PUT(code);PUT(data);
   memset(p,0x5a,payload);p+=payload;
   struct blob_reader reader={.current=bytes,.end=p};struct hk_shader shader={0};
   allocations=init_calls=uploads=0;
   VkResult result=hk_deserialize_shader(NULL,&reader,&shader);
   if(result==VK_SUCCESS){
      assert(reader.current==reader.end);
      for(unsigned n=0;n<code;n++)assert(((const uint8_t*)shader.code_ptr)[n]==0x5a);
      for(unsigned n=0;n<data;n++)assert(((const uint8_t*)shader.data_ptr)[n]==0x5a);
   }
   free((void*)shader.code_ptr);free((void*)shader.data_ptr);
   return result;
}
#define BAD(i,b,c,d,n) do {assert(run(i,b,c,d,n)==VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);assert(!allocations&&!init_calls&&!uploads);} while(0)
int main(void) {
   struct hk_shader_info info={.stage=4};
   struct agx_shader_info valid={.stage=4,.binary_size=16,.main_size=16};
   for(unsigned n=0;n<24;n++)BAD(info,valid,16,8,n);
   BAD(info,valid,UINT32_MAX,UINT32_MAX,0);
   BAD(info,valid,16,UINT32_MAX,16);
   struct hk_shader_info bad_info=info;bad_info.stage=-1;BAD(bad_info,valid,16,0,16);
   for(unsigned mode=0;mode<7;mode++){
      struct agx_shader_info b=valid;
      switch(mode){
      case 0:b.stage=-1;break;
      case 1:b.binary_size=17;break;
      case 2:b.main_offset=UINT32_MAX;break;
      case 3:b.main_size=UINT32_MAX;break;
      case 4:b.has_preamble=true;b.preamble_offset=16;break;
      case 5:b.rodata.offset=UINT32_MAX;b.rodata.size_16=1;break;
      case 6:b.rodata.offset=14;b.rodata.size_16=2;break;
      }
      BAD(info,b,16,0,16);
   }
   assert(run(info,valid,16,0,16)==VK_SUCCESS&&allocations==1&&uploads==1);
   assert(run(info,valid,16,8,24)==VK_SUCCESS&&allocations==2&&uploads==1);
   struct agx_shader_info empty={0};assert(run(info,empty,0,0,0)==VK_SUCCESS&&!allocations);
   for(fail_at=1;fail_at<=2;fail_at++)
      assert(run(info,valid,16,8,24)==VK_ERROR_OUT_OF_HOST_MEMORY&&!uploads);
   puts("PASS: truncated/oversized payloads and code ranges rejected before allocation; empty data, copies and OOM ownership checked");
}
'''.replace('/*BODY*/', body)
with tempfile.TemporaryDirectory(prefix='shader-cache-payload-') as directory:
    path = Path(directory)
    (path/'test.c').write_text(harness)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-Imesa/m3m4-merged/include',
                    str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
