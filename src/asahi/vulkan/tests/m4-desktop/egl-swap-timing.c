/* SPDX-License-Identifier: MIT
 * Forward EGL swaps and GBM locks unchanged; retain timing, ordering and results.
 * No error-state consumption, synchronization, readback or render-state changes.
 * Build: cc -shared -fPIC -O2 -Wall -Wextra egl-swap-timing.c -o egl-swap-timing.so -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef unsigned int EGLBoolean;
typedef void (*proc_t)(void);
static uint64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void *symbol(const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (!p) abort();
    return p;
}
EGLBoolean eglSwapBuffers(void *display, void *surface)
{
    EGLBoolean (*fn)(void *, void *) = symbol("eglSwapBuffers");
    uint64_t start = now();
    EGLBoolean ok = fn(display, surface);
    fprintf(stderr, "M4_EGL_SWAP pid=%d surface=%p start_ns=%llu end_ns=%llu ok=%u\n",
            getpid(), surface, (unsigned long long)start,
            (unsigned long long)now(), ok);
    return ok;
}
#define DAMAGE_SWAP(suffix) \
EGLBoolean eglSwapBuffersWithDamage##suffix(void *d, void *s, const int *r, int n) \
{ \
    proc_t (*lookup)(const char *) = symbol("eglGetProcAddress"); \
    EGLBoolean (*fn)(void *,void *,const int *,int) = (void *)lookup("eglSwapBuffersWithDamage" #suffix); \
    if (!fn) abort(); \
    uint64_t start = now(); \
    EGLBoolean ok = fn(d,s,r,n); \
    fprintf(stderr,"M4_EGL_DAMAGE_" #suffix " pid=%d surface=%p rects=%d start_ns=%llu end_ns=%llu ok=%u\n",getpid(),s,n,(unsigned long long)start,(unsigned long long)now(),ok); \
    return ok; \
}
DAMAGE_SWAP(KHR)
DAMAGE_SWAP(EXT)
void *gbm_surface_lock_front_buffer(void *surface)
{
    void *(*fn)(void *) = symbol("gbm_surface_lock_front_buffer");
    void *bo = fn(surface);
    fprintf(stderr,"M4_GBM_LOCK pid=%d surface=%p bo=%p ns=%llu\n",getpid(),surface,bo,(unsigned long long)now());
    return bo;
}
void gbm_surface_release_buffer(void *surface, void *bo)
{
    void (*fn)(void *,void *) = symbol("gbm_surface_release_buffer");
    fn(surface,bo);
    fprintf(stderr,"M4_GBM_RELEASE pid=%d surface=%p bo=%p ns=%llu\n",getpid(),surface,bo,(unsigned long long)now());
}
proc_t eglGetProcAddress(const char *name)
{
    proc_t (*lookup)(const char *) = symbol("eglGetProcAddress");
    if (!strcmp(name,"eglSwapBuffers")) return (proc_t)eglSwapBuffers;
    if (!strcmp(name,"eglSwapBuffersWithDamageKHR")) return lookup(name) ? (proc_t)eglSwapBuffersWithDamageKHR : NULL;
    if (!strcmp(name,"eglSwapBuffersWithDamageEXT")) return lookup(name) ? (proc_t)eglSwapBuffersWithDamageEXT : NULL;
    return lookup(name);
}
