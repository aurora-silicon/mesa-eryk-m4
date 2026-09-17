/* SPDX-License-Identifier: MIT
 * CPU-only copy benchmark on a private Asahi WC GEM allocation. No GPU binds.
 * Run under the mandatory hardware guard. Compares complete writes, not skips.
 */
#include "drm-uapi/asahi_drm.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static uint64_t now(void) {
  struct timespec t;
  assert(!clock_gettime(CLOCK_MONOTONIC, &t));
  return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void publish(void) { __asm__ volatile("dsb oshst" ::: "memory"); }
int main(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
  assert(fd >= 0);
  const size_t size = 2560 * 1664 * 4;
  char *src = malloc(size);
  assert(src);
  memset(src, 0x56, size);
  for (unsigned flags = 0; flags < 2; flags++) {
    struct drm_asahi_gem_create c = {.size = size, .flags = flags};
    assert(!ioctl(fd, DRM_IOCTL_ASAHI_GEM_CREATE, &c));
    struct drm_asahi_gem_mmap_offset o = {.handle = c.handle};
    assert(!ioctl(fd, DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET, &o));
    char *dst =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, o.offset);
    assert(dst != MAP_FAILED);
    memcpy(dst, src, size);
    publish();
    uint64_t start = now();
    for (int i = 0; i < 30; i++) {
      memcpy(dst, src, size);
      publish();
    }
    printf("flags=%u bytes=%zu copy_ms=%f\n", flags, size,
           (now() - start) / 30.0 / 1e6);
    assert(!memcmp(dst, src, size));
    munmap(dst, size);
    struct drm_gem_close close_bo = {.handle = c.handle};
    assert(!ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo));
  }
  free(src);
  close(fd);
  puts("PASS: GEM copy matches");
  return 0;
}
