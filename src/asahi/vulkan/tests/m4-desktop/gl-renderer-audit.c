/* SPDX-License-Identifier: MIT
 * Test instrumentation: report the active context's identity when an app
 * queries GL_VERSION. All queries are forwarded unchanged to libGL.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
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
