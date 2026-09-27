#!/usr/bin/env python3
"""Exercise production agx_bo_import/free with injected native import failures.

Offline: platform allocation/DRM/VA operations are observable mocks. This does
not prove kernel dma-buf mapping semantics; guarded external-memory CTS covers
those separately. Run from the workspace root.
"""
from pathlib import Path
import subprocess,tempfile
source=Path('mesa/m3m4-merged/src/asahi/lib/agx_device.c').read_text()
free=source[source.index('void\nagx_bo_free('):source.index('\n#ifdef HAVE_LIBDRM\nstatic int\nagx_drm_bo_bind')]
imp=source[source.index('struct agx_bo *\nagx_bo_import('):source.index('\nvoid\nagx_bo_make_shared')]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define HAVE_LIBDRM 1
#define ASSERTED
#define AIL_PAGESIZE 16384
#define AGX_BO_SHARED 1
#define AGX_BO_SHAREABLE 2
#define AGX_DBG_TRACE 4
#define DRM_ASAHI_BIND_READ 1
#define DRM_ASAHI_BIND_WRITE 2
struct agx_device; struct agx_bo;
struct agx_va {uint64_t addr;};
struct agx_bo {struct agx_device *dev; size_t size,align; unsigned flags,handle,uapi_handle; int prime_fd,refcnt; const char *label; struct agx_va *va; void *_map;};
struct agx_device {int fd,bo_map_lock,is_virtio,debug;void *vdrm,*agxdecode;struct {void(*bo_free)(struct agx_device*,uint32_t);void(*bo_munmap)(struct agx_device*,struct agx_bo*);}ops;};
static struct agx_bo slot;
static int failure,handles,duplicates,vas,bound,closed;
static void simple_mtx_lock(int *p){assert(!*p);*p=1;}
static void simple_mtx_unlock(int *p){assert(*p);*p=0;}
static int drmPrimeFDToHandle(int fd,int prime,unsigned *handle){assert(fd==10 && prime==20);if(failure==1)return -1;*handle=7;handles=1;return 0;}
static struct agx_bo *agx_lookup_bo(struct agx_device*d,unsigned h){assert(d->bo_map_lock && h==7);return &slot;}
static void agx_include_bo_handle(struct agx_device*d,unsigned h){assert(d->bo_map_lock && h==7);}
static off_t test_lseek(int fd,off_t off,int whence){assert(fd==20 && off==0 && whence==SEEK_END);return failure==2?0:failure==3?-1:failure==4?16385:16384;}
#define lseek test_lseek
static int os_dupfd_cloexec(int fd){assert(fd==20);if(failure==5)return -1;++duplicates;return 21;}
static int test_close(int fd){assert(fd==21 && duplicates==1);--duplicates;return 0;}
#define close test_close
static void p_atomic_set(int*p,int v){*p=v;}
static int p_atomic_read(int*p){return *p;}
static void agx_bo_reference(struct agx_bo*b){++b->refcnt;}
static struct agx_va *agx_va_alloc(struct agx_device*d,size_t n,size_t a,int f,int z){(void)d;(void)n;(void)a;(void)f;(void)z;if(failure==6)return NULL;struct agx_va*v=malloc(sizeof(*v));assert(v);v->addr=0x10000;++vas;return v;}
static void agx_va_free(struct agx_device*d,struct agx_va*v,bool unbind){(void)d;assert(!unbind);if(v){assert(vas==1);--vas;free(v);}}
static unsigned vdrm_handle_to_res_id(void*v,unsigned h){(void)v;return h;}
static int agx_bo_bind(struct agx_device*d,struct agx_bo*b,uint64_t va,size_t n,size_t off,unsigned flags){(void)d;(void)b;(void)va;(void)n;(void)off;(void)flags;if(failure==7)return -1;bound=1;return 0;}
static void agxdecode_track_alloc(void*d,struct agx_bo*b){(void)d;(void)b;}
static void *agx_bo_map(struct agx_bo*b){(void)b;abort();}
static void mock_free(struct agx_device*d,uint32_t h){assert(d->bo_map_lock && h==7 && handles==1);assert(!memcmp(&slot,&(struct agx_bo){0},sizeof(slot)));handles=0;bound=0;++closed;}
/*FREE*/
/*IMPORT*/
int main(void){
 struct agx_device d={.fd=10,.ops={.bo_free=mock_free}};
 for(failure=1;failure<=7;++failure){
  closed=0;assert(!agx_bo_import(&d,20));
  assert(!handles && !duplicates && !vas && !bound && !d.bo_map_lock);
  assert(closed==(failure!=1));
  assert(!memcmp(&slot,&(struct agx_bo){0},sizeof(slot)));
 }
 failure=0;closed=0;
 struct agx_bo*b=agx_bo_import(&d,20);assert(b==&slot && b->refcnt==1);
 assert(handles==1 && duplicates==1 && vas==1 && bound==1);
 assert(agx_bo_import(&d,20)==b && b->refcnt==2);
 b->refcnt=0;assert(agx_bo_import(&d,20)==b && b->refcnt==1);
 assert(handles==1 && duplicates==1 && vas==1 && closed==0);
 simple_mtx_lock(&d.bo_map_lock);agx_bo_free(&d,b);simple_mtx_unlock(&d.bo_map_lock);
 assert(!handles && !duplicates && !vas && !bound && closed==1);
 puts("PASS seven import failure boundaries, existing import, zero-ref revival and final ownership cleanup");
}
'''.replace('/*FREE*/',free).replace('/*IMPORT*/',imp)
with tempfile.TemporaryDirectory(prefix='bo-import-') as temp:
    path=Path(temp);(path/'test.c').write_text(harness)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O2',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
