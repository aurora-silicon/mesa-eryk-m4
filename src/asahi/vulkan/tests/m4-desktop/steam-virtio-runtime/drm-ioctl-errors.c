/* Diagnostic LD_PRELOAD for a single guarded muvm host process.
 * Log failed libdrm ioctls without changing their return value or errno.
 * Build for the host architecture with -shared -fPIC -ldl.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "drm-uapi/asahi_drm.h"

int drmIoctl(int fd, unsigned long request, void *arg)
{
    int (*next)(int, unsigned long, void *) = dlsym(RTLD_NEXT, "drmIoctl");
    int ret = next(fd, request, arg);
    int error = errno;
    if (ret < 0) {
        fprintf(stderr, "DRM-ERROR pid=%d fd=%d request=%#lx ret=%d errno=%d\n",
                getpid(), fd, request, ret, error);
        if (request == DRM_IOCTL_ASAHI_VM_BIND) {
            struct drm_asahi_vm_bind *bind = arg;
            fprintf(stderr, "  VM_BIND vm=%u count=%u stride=%u\n",
                    bind->vm_id, bind->num_binds, bind->stride);
            for (unsigned i = 0; i < bind->num_binds; i++) {
                struct drm_asahi_gem_bind_op *op = (void *)
                    ((char *)(uintptr_t)bind->userptr + i * bind->stride);
                fprintf(stderr, "  flags=%#x handle=%u offset=%#llx range=%#llx addr=%#llx\n",
                        op->flags, op->handle,
                        (unsigned long long)op->offset,
                        (unsigned long long)op->range,
                        (unsigned long long)op->addr);
            }
        }
    }
    errno = error;
    return ret;
}
