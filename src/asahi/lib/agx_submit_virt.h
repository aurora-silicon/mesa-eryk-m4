/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/*
 * Optional frontend-to-winsys metadata which does not belong in the public
 * drm_asahi_submit UAPI. Linux and virtio backends may ignore it. The D3DKMT
 * backend consumes a complete read set to publish CPU cache ownership to the
 * WDDM miniport without enumerating every live allocation.
 *
 * GPU addresses are absolute in both the main and USC VA heaps. A frontend
 * may set READ_SET_COMPLETE only when every GPU-read range is present. An
 * absent marker deliberately selects the conservative winsys fallback.
 */
#define AGX_SUBMIT_VIRT_READ_SET_COMPLETE UINT32_C(0x00000001)
#define AGX_SUBMIT_VIRT_VALID_FLAGS AGX_SUBMIT_VIRT_READ_SET_COMPLETE

struct agx_submit_read_range {
   uint64_t gpu_va;
   uint64_t length;
};
