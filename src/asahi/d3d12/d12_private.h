/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * d3d12agx: experimental native Direct3D 12 user-mode driver for Apple AGX
 * GPUs, built directly on the Mesa/Asahi substrate (agx_device and friends,
 * the same layer Honeykrisp sits on). There is deliberately no Vulkan and no
 * SPIR-V anywhere in this driver.
 *
 * API ABI: microsoft/DirectX-Headers (MIT). D3D12 behavioral contract:
 * Microsoft public documentation. Hardware truth: src/asahi at the pinned
 * Mesa tag, and nothing else.
 *
 * Campaign front door: ntasp repo, docs/d3d12agx-campaign.md.
 */

#pragma once

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include <stdbool.h>
#include <stdint.h>

#include "agx_device.h"

#define MESA_LOG_TAG "d3d12agx"
#include "util/log.h"
#include "util/macros.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"

/*
 * COM plumbing. Every d12 object embeds its COM interface as the FIRST
 * member so the interface pointer and the object pointer are the same
 * address, exactly like vtable-first layouts elsewhere in Mesa.
 */

static inline bool
d12_guid_equal(REFIID a, REFIID b)
{
   return memcmp(a, b, sizeof(GUID)) == 0;
}

/* Log an unknown riid so we learn what callers actually want. */
void d12_log_unknown_riid(const char *where, REFIID riid);

struct d12_device {
   ID3D12Device iface; /* must be first */
   uint32_t refcount;

   /* The real GPU. Owned: closed on final release. */
   struct agx_device dev;

   D3D_FEATURE_LEVEL feature_level;
};

struct d12_fence {
   ID3D12Fence iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */

   /* DRM timeline syncobj: D3D12 fences ARE timeline semaphores. The
    * completed value is the syncobj's signaled point; CPU Signal is a
    * timeline signal; queue Signal/Wait are syncobj transfers, mirroring
    * hk_queue.c.
    */
   uint32_t syncobj;
};

struct d12_queue {
   ID3D12CommandQueue iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_COMMAND_QUEUE_DESC desc;

   /* Hardware queue on the asahi device */
   uint32_t agx_queue_id;

   /* Queue-progress timeline, same scheme as hk_queue: point N signals when
    * the Nth chunk of queue work completes. With no execution engine yet
    * (G1), the timeline only advances through Wait() transfers.
    */
   uint32_t syncobj;
   uint64_t timeline_value;
   simple_mtx_t lock;
};

static inline struct d12_device *
d12_device_from_iface(ID3D12Device *iface)
{
   return (struct d12_device *)iface;
}

static inline struct d12_fence *
d12_fence_from_iface(ID3D12Fence *iface)
{
   return (struct d12_fence *)iface;
}

static inline struct d12_queue *
d12_queue_from_iface(ID3D12CommandQueue *iface)
{
   return (struct d12_queue *)iface;
}

/* Object constructors (Create* device methods land here) */
HRESULT d12_fence_create(struct d12_device *device, UINT64 initial_value,
                         D3D12_FENCE_FLAGS flags, REFIID riid, void **out);
HRESULT d12_queue_create(struct d12_device *device,
                         const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid,
                         void **out);

void d12_device_add_ref(struct d12_device *device);
void d12_device_release(struct d12_device *device);
