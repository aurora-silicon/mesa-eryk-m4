/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_cmd_pool.h"
#include "asahi/lib/agx_bo.h"
#include "asahi/lib/agx_device.h"
#include "drm-uapi/asahi_drm.h"
#include "util/u_debug.h"

#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_physical_device.h"

#define G17P_VDM_USER_BASE UINT64_C(0x1000000000)

bool
hk_g17p_vdm_low_window_enabled(const struct agx_device *dev)
{
   static int enabled = -1;

   /* On by default: a VDM record's pointer is 40 bits and hardware reads bit
    * 39 as the UAT root select, adding 0x1000000000 when it is clear. G17P's
    * main heap sits at 0x8000000000, so every main-heap pointer a VDM record
    * carries has bit 39 set and is fetched from the upper root instead. That
    * is exactly the dVDM_SSD unmapped read at (1<<42)|0x7ffee0400 hardware
    * reported for the first PPP state record after the root chunk
    * (0x87ffee0400). Native keeps these payloads inside the architectural
    * 0x1000000000 window, which is what this path reproduces. The environment
    * variable now only exists to turn the correct behaviour back off for A/B
    * testing. */
   if (enabled < 0)
      enabled = debug_get_bool_option("ASAHI_G17P_VDM_LOW_WINDOW", true);

   return dev->chip == AGX_CHIP_G17P && enabled;
}

uint64_t
hk_g17p_vdm_low(const struct agx_device *dev, uint64_t addr)
{
   assert(hk_g17p_vdm_low_window_enabled(dev));
   assert(addr >= dev->main_heap_base);

   const uint64_t offset = addr - dev->main_heap_base;
   assert(offset < dev->main_heap_size);
   assert(offset < BITFIELD64_BIT(39));

   /* The native HAL200 encoder stores every bound render-state address as a
    * 32-bit offset from 0x1000000000.  Main-heap allocations sit near the top
    * of the larger compact window, so preserve their low 32 bits in a second
    * alias rather than serializing an unrepresentable 38-bit offset into the
    * bind-pair field.  Command-pool BOs descend linearly and retain their
    * unique 128 KiB slots within this 4 GiB context window.
    */
   return (uint32_t)offset;
}

uint64_t
hk_g17p_vdm_alias(const struct agx_device *dev, uint64_t addr)
{
   return G17P_VDM_USER_BASE + hk_g17p_vdm_low(dev, addr);
}

static VkResult
hk_cmd_bo_create(struct hk_cmd_pool *pool, bool usc, struct hk_cmd_bo **bo_out)
{
   struct hk_device *dev = hk_cmd_pool_device(pool);
   struct hk_cmd_bo *bo;

   bo = vk_zalloc(&pool->vk.alloc, sizeof(*bo), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (bo == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   bo->bo = agx_bo_create(&dev->dev, HK_CMD_BO_SIZE, 0, usc ? AGX_BO_LOW_VA : 0,
                          usc ? "USC command pool" : "Main command pool");
   if (bo->bo == NULL) {
      vk_free(&pool->vk.alloc, bo);
      return vk_error(pool, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   if (!usc && hk_g17p_vdm_low_window_enabled(&dev->dev)) {
      const uint64_t alias = hk_g17p_vdm_alias(&dev->dev, bo->bo->va->addr);
      const uint32_t flags = DRM_ASAHI_BIND_READ | DRM_ASAHI_BIND_WRITE;

      if (agx_bo_bind(&dev->dev, bo->bo, alias, bo->bo->size, 0, flags)) {
         fprintf(stderr,
                 "asahi: failed to bind G17P VDM alias %#llx\n",
                 (unsigned long long)alias);
         agx_bo_unreference(&dev->dev, bo->bo);
         vk_free(&pool->vk.alloc, bo);
         return vk_error(pool, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }

      bo->g17p_vdm_alias = alias;
      fprintf(stderr,
              "asahi: G17P VDM command BO full=%#llx alias=%#llx low=%#llx size=%#llx\n",
              (unsigned long long)bo->bo->va->addr,
              (unsigned long long)alias,
              (unsigned long long)(alias - G17P_VDM_USER_BASE),
              (unsigned long long)bo->bo->size);
   }

   bo->map = agx_bo_map(bo->bo);

   *bo_out = bo;
   return VK_SUCCESS;
}

static void
hk_cmd_bo_destroy(struct hk_cmd_pool *pool, struct hk_cmd_bo *bo)
{
   struct hk_device *dev = hk_cmd_pool_device(pool);
   if (bo->g17p_vdm_alias) {
      agx_bo_bind(&dev->dev, NULL, bo->g17p_vdm_alias, bo->bo->size, 0,
                  DRM_ASAHI_BIND_UNBIND);
   }
   agx_bo_unreference(&dev->dev, bo->bo);
   vk_free(&pool->vk.alloc, bo);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateCommandPool(VkDevice _device,
                     const VkCommandPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkCommandPool *pCmdPool)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   struct hk_cmd_pool *pool;

   pool = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_command_pool_init(&device->vk, &pool->vk, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pool);
      return result;
   }

   list_inithead(&pool->free_bos);
   list_inithead(&pool->free_usc_bos);

   *pCmdPool = hk_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

static void
hk_cmd_pool_destroy_bos(struct hk_cmd_pool *pool)
{
   list_for_each_entry_safe(struct hk_cmd_bo, bo, &pool->free_bos, link)
      hk_cmd_bo_destroy(pool, bo);

   list_inithead(&pool->free_bos);

   list_for_each_entry_safe(struct hk_cmd_bo, bo, &pool->free_usc_bos, link)
      hk_cmd_bo_destroy(pool, bo);

   list_inithead(&pool->free_usc_bos);
}

VkResult
hk_cmd_pool_alloc_bo(struct hk_cmd_pool *pool, bool usc,
                     struct hk_cmd_bo **bo_out)
{
   struct hk_cmd_bo *bo = NULL;
   if (usc) {
      if (!list_is_empty(&pool->free_usc_bos)) {
         bo = list_first_entry(&pool->free_usc_bos, struct hk_cmd_bo, link);
         pool->num_free_usc_bos--;
      }
   } else {
      if (!list_is_empty(&pool->free_bos)) {
         bo = list_first_entry(&pool->free_bos, struct hk_cmd_bo, link);
         pool->num_free_bos--;
      }
   }
   if (bo) {
      list_del(&bo->link);
      *bo_out = bo;
      return VK_SUCCESS;
   }

   return hk_cmd_bo_create(pool, usc, bo_out);
}

void
hk_cmd_pool_free_bo_list(struct hk_cmd_pool *pool, struct list_head *bos)
{
   list_for_each_entry_safe(struct hk_cmd_bo, bo, bos, link) {
      list_del(&bo->link);
      if (pool->num_free_bos > HK_CMD_POOL_BO_MAX) {
         hk_cmd_bo_destroy(pool, bo);
      } else {
         list_addtail(&bo->link, &pool->free_bos);
         pool->num_free_bos++;
      }
   }
}

void
hk_cmd_pool_free_usc_bo_list(struct hk_cmd_pool *pool, struct list_head *bos)
{
   list_for_each_entry_safe(struct hk_cmd_bo, bo, bos, link) {
      list_del(&bo->link);
      if (pool->num_free_usc_bos > HK_CMD_POOL_BO_MAX) {
         hk_cmd_bo_destroy(pool, bo);
      } else {
         list_addtail(&bo->link, &pool->free_usc_bos);
         pool->num_free_usc_bos++;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyCommandPool(VkDevice _device, VkCommandPool commandPool,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   vk_command_pool_finish(&pool->vk);
   hk_cmd_pool_destroy_bos(pool);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR void VKAPI_CALL
hk_TrimCommandPool(VkDevice device, VkCommandPool commandPool,
                   VkCommandPoolTrimFlags flags)
{
   VK_FROM_HANDLE(hk_cmd_pool, pool, commandPool);

   vk_command_pool_trim(&pool->vk, flags);
   hk_cmd_pool_destroy_bos(pool);
}
