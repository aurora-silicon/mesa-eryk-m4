#!/usr/bin/env python3
"""Exercise actual command-pool allocation functions with CPU-map failures.

Mocks expose ownership and publication: no hardware or Vulkan loading. Covers
both ordinary pools and the oversized allocations not reached by the fill probe.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path('mesa/m3m4-merged/src/asahi/vulkan')
s = (root/'hk_cmd_pool.c').read_text()
create = s[s.index('static VkResult\nhk_cmd_bo_create('):s.index('\nstatic void\nhk_cmd_bo_destroy(')]
s = (root/'hk_cmd_buffer.c').read_text()
allocate = s[s.index('struct agx_ptr\nhk_pool_alloc_internal('):s.index('\nuint64_t\nhk_pool_upload(')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
#define HK_CMD_BO_SIZE (128*1024)
#define AGX_BO_LOW_VA 1
#define AGX_BO_EXEC 2
#define AGX_CHIP_G16G 4
#define unlikely(x) (x)
#define align(x,a) (((x)+(a)-1)&~((a)-1))
#define vk_error(p,r) (r)
struct agx_device { int chip; };
struct hk_device { struct agx_device dev; };
struct agx_bo { struct {uint64_t addr;} *va; };
struct hk_cmd_bo { struct agx_bo *bo; void *map; };
struct hk_cmd_pool { struct { int alloc; } vk; };
struct hk_uploader { void *map; uint64_t base; uint32_t offset; };
struct hk_cmd_buffer { VkResult vk; struct {struct hk_uploader main,usc;} uploader; int large_bos; };
struct agx_ptr { uint64_t gpu; void *cpu; };
static struct hk_device device;
static struct hk_cmd_pool pool;
static int fail, wrappers, bos, published;
static struct agx_bo bo;
static char map[HK_CMD_BO_SIZE*2];
static struct hk_device *hk_cmd_pool_device(struct hk_cmd_pool *p){(void)p;return &device;}
static struct hk_device *hk_cmd_buffer_device(struct hk_cmd_buffer *p){(void)p;return &device;}
static void *vk_zalloc(int*a,size_t size,int alignment,int scope){(void)a;(void)alignment;(void)scope;if(fail==1)return NULL;++wrappers;return calloc(1,size);}
static void vk_free(int*a,void*p){(void)a;assert(wrappers==1);--wrappers;free(p);}
static struct agx_bo *agx_bo_create(struct agx_device*d,uint32_t size,int alignment,uint32_t flags,const char*label){(void)d;(void)size;(void)alignment;(void)flags;(void)label;if(fail==2)return NULL;assert(!bos);++bos;static typeof(*bo.va) va={.addr=0x100000};bo.va=&va;return &bo;}
static void *agx_bo_map(struct agx_bo*b){assert(b==&bo && bos==1);return fail==3?NULL:map;}
static void agx_bo_unreference(struct agx_device*d,struct agx_bo*b){(void)d;assert(b==&bo && bos==1);--bos;}
static void vk_command_buffer_set_error(VkResult*r,VkResult e){if(*r==VK_SUCCESS)*r=e;}
static void append_bo(int *array,struct agx_bo*b){assert(b==&bo && bos==1);++*array;++published;}
#define util_dynarray_append(array,b) append_bo(array,b)
/*CREATE*/
static struct hk_cmd_bo *owned;
static VkResult hk_cmd_buffer_alloc_bo(struct hk_cmd_buffer*c,bool usc,struct hk_cmd_bo**out){(void)c;VkResult r=hk_cmd_bo_create(&pool,usc,out);if(r==VK_SUCCESS){owned=*out;++published;}return r;}
/*ALLOCATE*/
int main(void){
 for(int chip=3;chip<=4;++chip)for(int usc=0;usc<=1;++usc)for(int big=0;big<=1;++big)for(fail=0;fail<=3;++fail){
  if(big && fail==1)continue;
  device.dev.chip=chip;struct hk_cmd_buffer cmd={0};published=0;owned=NULL;
  struct agx_ptr p=hk_pool_alloc_internal(&cmd,big?HK_CMD_BO_SIZE+16:16,16,usc);
  if(fail){assert(!p.cpu && !p.gpu && !bos && !wrappers && !published);assert(cmd.vk==(fail==2?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_ERROR_OUT_OF_HOST_MEMORY));assert(!cmd.large_bos && !cmd.uploader.main.map && !cmd.uploader.usc.map);}
  else {assert(p.cpu==map && p.gpu==0x100000 && published==1 && cmd.vk==VK_SUCCESS);agx_bo_unreference(&device.dev,&bo);if(owned)vk_free(&pool.vk.alloc,owned);}
  assert(!bos && !wrappers);
 }
 puts("PASS command BO wrapper/backing/map failures, both pools/chips, oversized ownership and publication");
}
'''.replace('/*CREATE*/', create).replace('/*ALLOCATE*/', allocate)
with tempfile.TemporaryDirectory(prefix='command-map-unwind-') as temp:
    p = Path(temp)
    (p/'test.c').write_text(harness)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-Imesa/m3m4-merged/include', str(p/'test.c'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
