/* SPDX-License-Identifier: MIT
 * Test instrumentation: report the active context's identity when an app
 * queries GL_VERSION. All queries are forwarded unchanged to libGL.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
const unsigned char *glGetString(unsigned name);
const unsigned char *glGetString(unsigned name)
{
    typedef const unsigned char *(*query_fn)(unsigned);
    query_fn query = (query_fn)dlsym(RTLD_NEXT, "glGetString");
    if (!query) abort();
    const unsigned char *value = query(name);
    if (name == 0x1f02) {
        const unsigned char *renderer = query(0x1f01);
        const unsigned char *vendor = query(0x1f00);
        fprintf(stderr, "M4 context audit: GL_RENDERER=%s; GL_VENDOR=%s\n",
                renderer ? (const char *)renderer : "(null)",
                vendor ? (const char *)vendor : "(null)");
    }
    return value;
}

/* Optional CPU wall-clock instrumentation only: no GL calls or GPU waits.
 * Each row describes a completed SDL swap and the interval since the previous
 * one. Keep startup rows; offline summaries explicitly select their interval.
 */
void SDL_GL_SwapWindow(void *window)
{
    static void (*swap)(void *);
    static int enabled = -1;
    static uint64_t previous, frame;
    struct timespec t;
    if (!swap) swap = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    if (!swap) abort();
    if (enabled < 0) enabled = getenv("M4_SWAP_TIMING") != NULL;
    if (!enabled) { swap(window); return; }
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t start = (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
    swap(window);
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t end = (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
    fprintf(stderr, "M4_SWAP_TIMING frame=%llu end_ns=%llu interval_ns=%llu swap_ns=%llu\n",
            (unsigned long long)frame++, (unsigned long long)end,
            (unsigned long long)(previous ? end - previous : 0),
            (unsigned long long)(end - start));
    previous = end;
}
