#!/usr/bin/env python3
"""Extract real lazy variant admission paths and inject ownership failures."""
from pathlib import Path
import subprocess,tempfile
s=Path('mesa/m3m4-merged/src/asahi/vulkan/hk_cmd_draw.c').read_text()
def function(name):
 start=s.rfind('\nstatic ',0,s.index(name+'('))+1
 brace=s.index('{',s.index(name+'('));depth=1;end=brace+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
body='\n'.join(function(n) for n in ['hk_get_fast_linked_locked_vs','hk_get_fast_linked_locked_fs','hk_get_fast_linked','hk_update_fast_linked'])
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
typedef int mesa_shader_stage;
#define MESA_SHADER_VERTEX 0
#define MESA_SHADER_FRAGMENT 1
#define BITFIELD_BIT(n) (1u<<(n))
#define UNREACHABLE(s) abort()
#define mesa_loge(...) ((void)0)
struct agx_shader_part {int dummy;};
struct hk_linked_shader {struct {bool uses_base_param;} b; bool sw_indexing,prolog_uses_root,prolog_statistics_uniform;};
struct hash_entry {void *data;};
struct table {struct hash_entry entry;void *key;bool valid;};
struct hk_device {int dev;};
struct hk_shader {struct {int stage;} info;struct {struct table *ht;int lock;} linked;};
struct hk_fast_link_key_vs {struct {bool hw;int adjacency;} prolog;};
struct hk_fast_link_key_fs {struct {bool statistics;int cull_distance_size,api_sample_mask,cf_base;bool statistics_uniform;} prolog;int epilog,nr_samples_shaded;};
struct hk_cmd_buffer {VkResult vk;struct {struct {struct hk_linked_shader *linked[2];unsigned linked_dirty;} gfx;} state;};
static struct hk_device device;
static int failure,keys,links,locks;
static struct agx_shader_part part;
static bool agx_native_graphics(int*d){(void)d;return true;}
static void builder(void){}
#define agx_nir_vs_prolog_raw builder
#define agx_nir_vs_prolog builder
#define build_g17p_fs_prolog builder
#define build_fs_prolog builder
#define build_g17p_fs_epilog builder
#define agx_nir_fs_epilog builder
static struct agx_shader_part *hk_get_prolog_epilog(struct hk_device*d,void*k,size_t n,void(*b)(void),bool pre,bool stop,unsigned cf,VkResult*r){(void)d;(void)k;(void)n;(void)b;(void)pre;(void)stop;(void)cf;*r=failure==1?VK_ERROR_OUT_OF_HOST_MEMORY:failure==2?VK_ERROR_FEATURE_NOT_PRESENT:VK_SUCCESS;return *r==VK_SUCCESS?&part:NULL;}
static void *ralloc_memdup(struct table*t,void*k,size_t n){(void)t;if(failure==3)return NULL;void*p=malloc(n);memcpy(p,k,n);keys++;return p;}
static void ralloc_free(void*p){assert(keys>0);keys--;free(p);}
static struct hk_linked_shader *hk_fast_link_checked(struct hk_device*d,bool fs,struct hk_shader*s,struct agx_shader_part*p,struct agx_shader_part*e,unsigned n,VkResult*r){(void)d;(void)fs;(void)s;(void)p;(void)e;(void)n;*r=failure==4?VK_ERROR_OUT_OF_HOST_MEMORY:failure==5?VK_ERROR_OUT_OF_DEVICE_MEMORY:failure==6?VK_ERROR_UNKNOWN:VK_SUCCESS;if(*r!=VK_SUCCESS)return NULL;links++;return calloc(1,sizeof(struct hk_linked_shader));}
static void hk_destroy_linked_shader(struct hk_device*d,struct hk_linked_shader*s){(void)d;assert(links>0);links--;free(s);}
static struct hash_entry *_mesa_hash_table_search(struct table*t,void*k){(void)k;return t->valid?&t->entry:NULL;}
static struct hash_entry *_mesa_hash_table_insert(struct table*t,void*k,void*s){if(failure==7)return NULL;assert(!t->valid);t->valid=true;t->key=k;t->entry.data=s;return &t->entry;}
static void simple_mtx_lock(int*l){(void)l;assert(!locks);locks++;}
static void simple_mtx_unlock(int*l){(void)l;assert(locks==1);locks--;}
static struct hk_device *hk_cmd_buffer_device(struct hk_cmd_buffer*c){(void)c;return &device;}
static void vk_command_buffer_set_error(VkResult*r,VkResult e){assert(e!=VK_SUCCESS);if(*r==VK_SUCCESS)*r=e;}
/*BODY*/
int main(void){
 for(int stage=0;stage<2;stage++)for(failure=0;failure<8;failure++){
  struct table table={0};struct hk_shader shader={.info.stage=stage,.linked.ht=&table};struct hk_cmd_buffer cmd={0};
  struct hk_fast_link_key_vs vs={.prolog.hw=true};struct hk_fast_link_key_fs fs={.prolog.statistics=true,.prolog.api_sample_mask=255};
  void *key=stage==0?(void*)&vs:(void*)&fs;
  bool ok=hk_update_fast_linked(&cmd,&shader,key);
  assert(!locks);
  if(failure){VkResult expected=failure==2?VK_ERROR_FEATURE_NOT_PRESENT:failure==5?VK_ERROR_OUT_OF_DEVICE_MEMORY:failure==6?VK_ERROR_UNKNOWN:VK_ERROR_OUT_OF_HOST_MEMORY;assert(!ok&&cmd.vk==expected&&!keys&&!links&&!table.valid&&!cmd.state.gfx.linked[stage]&&!cmd.state.gfx.linked_dirty);}
  else {assert(ok&&cmd.vk==VK_SUCCESS&&keys==1&&links==1&&table.valid);cmd.state.gfx.linked_dirty=0;int saved=failure;failure=7;assert(hk_update_fast_linked(&cmd,&shader,key)&&cmd.vk==VK_SUCCESS&&!cmd.state.gfx.linked_dirty);failure=saved;ralloc_free(table.key);hk_destroy_linked_shader(&device,table.entry.data);}
 }
 puts("PASS lazy VS/FS prolog, clone, host/device/upload, cache insertion failures; ownership rollback and cached retry");
}
'''.replace('/*BODY*/',body)
with tempfile.TemporaryDirectory(prefix='lazy-link-errors-') as d:
 p=Path(d);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O2','-Imesa/m3m4-merged/include',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
