/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_private.h"
#include "vk_queue.h"
#include "c11/threads.h"
#include "util/u_dynarray.h"

struct hk_device;
struct agx_bo;

/* G15S submission batching (see hk_queue.c): CDM storage of one batch. */
#define HK_BATCH_SLOTS 8
struct hk_batch_slot {
   struct agx_bo *bo;
   uint8_t *map;
   uint64_t point; /* queue timeline value of its last use */
};

struct hk_queue {
   struct vk_queue vk;
   uint64_t diagnostic_present_frame;
   uint64_t diagnostic_present_ns;

   struct {
      /* Asahi kernel queue ID */
      uint32_t id;

      /* Timeline syncobj backing the queue */
      uint32_t syncobj;

      /* Ring-idx used with virtgpu, equal to priority + 1 */
      uint32_t virt_ring_idx;

      /* Current maximum timeline value for the queue's syncobj. If the
       * syncobj's value equals timeline_value, then all work is complete.
       */
      uint64_t timeline_value;
   } drm;

   /* G15S submission batching: compute-only submissions made while two of
    * this queue's DRM submissions are executing wait here and are submitted
    * together, as one compute command, when one of those completes. */
   struct {
      bool enabled, run;
      mtx_t lock;
      cnd_t wake;
      thrd_t thread;
      /* Pending submissions: their CDM records without terminates, their
       * signal operations, and whether all retain the wide final barrier. */
      struct util_dynarray cdm;
      struct util_dynarray signals;
      uint32_t count;
      bool wide_visibility;
      struct hk_batch_slot slot[HK_BATCH_SLOTS];
      unsigned next_slot;
   } batch;
};

static inline struct hk_device *
hk_queue_device(struct hk_queue *queue)
{
   return (struct hk_device *)queue->vk.base.device;
}

VkResult hk_queue_init(struct hk_device *dev,
                       const VkDeviceQueueCreateInfo *pCreateInfo,
                       uint32_t index_in_family);

void hk_queue_finish(struct hk_device *dev, struct hk_queue *queue);
