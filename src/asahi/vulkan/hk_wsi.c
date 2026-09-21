/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_wsi.h"
#include "hk_instance.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_queue.h"
#include "wsi_common.h"
#include "util/os_time.h"
#include "util/u_debug.h"

#include <xf86drm.h>

DEBUG_GET_ONCE_BOOL_OPTION(hk_present_timing, "HK_PRESENT_TIMING", false)

VKAPI_ATTR VkResult VKAPI_CALL
hk_QueuePresentKHR(VkQueue handle, const VkPresentInfoKHR *info)
{
   struct hk_queue *queue = (struct hk_queue *)vk_queue_from_handle(handle);
   const bool timing = debug_get_option_hk_present_timing();
   const uint64_t start = timing ? os_time_get_nano() : 0;
   VkResult result = wsi_common_queue_present(
      queue->vk.base.device->physical->wsi_device, &queue->vk, info);
   if (timing) {
      const uint64_t end = os_time_get_nano();
      fprintf(stderr, "HK_PRESENT queue=%p frame=%" PRIu64 " end_ns=%" PRIu64
              " interval_ns=%" PRIu64 " present_ns=%" PRIu64 " result=%d\n",
              (void *)queue, ++queue->diagnostic_present_frame, end,
              queue->diagnostic_present_ns ? end - queue->diagnostic_present_ns : 0,
              end - start, result);
      queue->diagnostic_present_ns = end;
   }
   return result;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
hk_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(hk_physical_device, pdev, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(pdev->vk.instance, pName);
}

static bool
hk_wsi_can_present_on_device(VkPhysicalDevice physicalDevice, int fd)
{
   VK_FROM_HANDLE(hk_physical_device, pdev, physicalDevice);
   if (pdev->dev.is_virtio) {
      return false;
   }

   drmDevicePtr device;
   if (drmGetDevice2(fd, 0, &device) != 0) {
      return false;
   }

   /* Allow on-device presentation for all non-virtio devices with bus type
    * PLATFORM */
   bool match = device->bustype == DRM_BUS_PLATFORM;

   drmFreeDevice(&device);

   return match;
}

VkResult
hk_init_wsi(struct hk_physical_device *pdev)
{
   VkResult result;

   struct wsi_device_options wsi_options = {.sw_device = false};
   result = wsi_device_init(
      &pdev->wsi_device, hk_physical_device_to_handle(pdev), hk_wsi_proc_addr,
      &pdev->vk.instance->alloc, pdev->master_fd,
      &hk_physical_device_instance(pdev)->dri_options, &wsi_options);
   if (result != VK_SUCCESS)
      return result;

   pdev->wsi_device.supports_scanout = false;
   pdev->wsi_device.supports_modifiers = true;
   pdev->wsi_device.can_present_on_device = hk_wsi_can_present_on_device;

   pdev->vk.wsi_device = &pdev->wsi_device;

   return result;
}

void
hk_finish_wsi(struct hk_physical_device *pdev)
{
   pdev->vk.wsi_device = NULL;
   wsi_device_finish(&pdev->wsi_device, &pdev->vk.instance->alloc);
}
