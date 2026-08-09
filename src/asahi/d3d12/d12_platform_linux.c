/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_private.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>
#include <xf86drm.h>

static int
d12_open_asahi_fd(void)
{
   drmDevicePtr devices[8];
   int count = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   int result = -1;

   for (int i = 0; i < count; i++) {
      if (!(devices[i]->available_nodes & (1 << DRM_NODE_RENDER)))
         continue;

      int fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
      if (fd < 0)
         continue;

      drmVersionPtr version = drmGetVersion(fd);
      bool is_asahi = version && !strcmp(version->name, "asahi");
      drmFreeVersion(version);

      if (is_asahi) {
         result = fd;
         break;
      }
      close(fd);
   }

   if (count > 0)
      drmFreeDevices(devices, count);

   return result;
}

bool
d12_platform_open_device(struct d12_device *device, IUnknown *adapter)
{
   (void)adapter;

   int fd = d12_open_asahi_fd();
   if (fd < 0)
      return false;

   device->dev.fd = fd;
   if (!agx_open_device(NULL, &device->dev)) {
      close(fd);
      return false;
   }

   device->sync = util_sync_provider_drm(fd);
   if (!device->sync) {
      agx_close_device(&device->dev);
      return false;
   }

   device->adapter_luid = (LUID){.LowPart = 0xA6A00001u, .HighPart = 0};
   return true;
}

void
d12_platform_close_device(struct d12_device *device)
{
   if (device->sync) {
      device->sync->finalize(device->sync);
      device->sync = NULL;
   }

   agx_close_device(&device->dev);
}

void
d12_platform_signal_event(HANDLE event)
{
   int fd = (int)(intptr_t)event;

   /* Native Linux tests model a Win32 auto-reset event with eventfd. Avoid
    * accumulating another token when the event is already signalled. */
   struct pollfd pfd = {.fd = fd, .events = POLLIN};
   if (poll(&pfd, 1, 0) > 0)
      return;

   const uint64_t one = 1;
   if (write(fd, &one, sizeof(one)) != sizeof(one) && errno != EAGAIN)
      mesa_loge("fence event write failed: %s", strerror(errno));
}
