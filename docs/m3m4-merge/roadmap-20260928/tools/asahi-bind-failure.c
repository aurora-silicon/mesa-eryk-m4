/* SPDX-License-Identifier: MIT
 * Test-only LD_PRELOAD interposer. Arm explicitly after device construction;
 * reject one read-only VM bind before it reaches the kernel. This exercises
 * userspace cleanup, not kernel allocation-failure handling. Never installed
 * into a normal session or system library directory.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "asahi_drm.h"
static _Atomic int armed;
static _Atomic int injected;
void asahi_test_arm_bind_failure(void) { atomic_store(&injected,0);atomic_store(&armed,1); }
int asahi_test_bind_failure_injected(void) { return atomic_load(&injected); }
static int (*real_ioctl)(int,unsigned long,void*);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void resolve_ioctl(void) {
    /* Vulkan may dlopen its driver RTLD_LOCAL. RTLD_NEXT from this global
     * interposer cannot see that dependency scope; use its explicit handle. */
    void *drm=dlopen("libdrm.so.2",RTLD_NOW|RTLD_LOCAL);
    if(drm)real_ioctl=dlsym(drm,"drmIoctl");
    if(!real_ioctl){fprintf(stderr,"BIND_FAILURE cannot resolve libdrm: %s\n",dlerror());abort();}
}
int drmIoctl(int fd,unsigned long request,void *arg) {
    pthread_once(&once,resolve_ioctl);
    if(request==DRM_IOCTL_ASAHI_VM_BIND && atomic_load(&armed)) {
        const struct drm_asahi_vm_bind *bind=arg;
        if(bind->num_binds==1 && bind->stride==sizeof(struct drm_asahi_gem_bind_op)) {
            const struct drm_asahi_gem_bind_op *op=(const void*)(uintptr_t)bind->userptr;
            if(op->flags==DRM_ASAHI_BIND_READ && atomic_exchange(&armed,0)) {
                atomic_store(&injected,1);
                fprintf(stderr,"BIND_FAILURE injected read-only bind handle=%u address=%#llx\n",op->handle,(unsigned long long)op->addr);
                errno=ENOMEM;return -1;
            }
        }
    }
    return real_ioctl(fd,request,arg);
}
