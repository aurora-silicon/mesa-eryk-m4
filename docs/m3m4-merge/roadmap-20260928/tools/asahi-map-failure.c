/* SPDX-License-Identifier: MIT
 * Test-only one-shot CPU mapping rejection for a DRM render fd. Arm immediately
 * before command recording. This does not inject kernel VM-allocation failure.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
static _Atomic int armed,injected;
static _Atomic unsigned matching;
static size_t match_size;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void *(*real_mmap)(void*,size_t,int,int,int,off_t);
static void resolve_mmap(void){real_mmap=dlsym(RTLD_NEXT,"mmap");if(!real_mmap)abort();}
void asahi_test_arm_map_failure(void){const char *s=getenv("ASAHI_TEST_MAP_SIZE");match_size=s?strtoull(s,NULL,0):0;const char *n=getenv("ASAHI_TEST_MAP_MATCH");unsigned ordinal=n?strtoul(n,NULL,0):1;if(!ordinal)abort();atomic_store(&matching,ordinal);atomic_store(&injected,0);atomic_store(&armed,1);}
int asahi_test_map_failure_injected(void){return atomic_load(&injected);}
void *mmap(void *addr,size_t len,int prot,int flags,int fd,off_t offset){
    pthread_once(&once,resolve_mmap);
    if(fd>=0 && atomic_load(&armed) && (!match_size || len==match_size)){
        struct stat st;
        if(!fstat(fd,&st) && S_ISCHR(st.st_mode) && major(st.st_rdev)==226 && atomic_fetch_sub(&matching,1)==1 && atomic_exchange(&armed,0)){
            atomic_store(&injected,1);fprintf(stderr,"MAP_FAILURE injected fd=%d bytes=%zu offset=%#llx\n",fd,len,(unsigned long long)offset);
            errno=ENOMEM;return MAP_FAILED;
        }
    }
    return real_mmap(addr,len,prot,flags,fd,offset);
}
void *mmap64(void *addr,size_t len,int prot,int flags,int fd,off64_t offset){return mmap(addr,len,prot,flags,fd,(off_t)offset);}
