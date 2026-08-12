/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

/*
 * The Windows winsys backend for agx_device_ops_t, talking D3DKMT to a WDDM
 * miniport instead of DeviceIoControl to a private ioctl node.
 *
 * WHY: a private ioctl node (\\.\AGXKMD) and dxgkrnl are two kernel
 * interfaces to one GPU. That means two allocators, two GPU VA regimes and
 * two handle namespaces, so a Vulkan image and a D3D texture can never be the
 * same object, no primary can be shared, and DXGI interop is structurally
 * impossible. Vendor Vulkan ICDs on ordinary Windows go through D3DKMT for
 * exactly this reason -- it is what makes VK_KHR_external_memory_win32 and
 * D3D<->Vulkan interop work at all. This backend puts Honeykrisp behind the
 * same kernel interface a D3D UMD uses.
 *
 * LAYERING: everything here lives in src/asahi/lib, the layer Honeykrisp and
 * the Gallium `asahi` driver share, and nothing in it is Vulkan-shaped. A
 * D3D-side consumer (a d3d10umd frontend over Gallium asahi) uses the same
 * two entry points below.
 *
 * OPEN CONTRACT: unlike agx_win_open_device(), which takes a pre-opened NT
 * HANDLE from the caller's CreateFileW, opening is done HERE, keyed by
 * adapter LUID. The reasons are structural, not stylistic:
 *
 *   - "opening" is now five D3DKMT calls plus a caps parse
 *     (EnumAdapters2 -> QueryAdapterInfo -> OpenAdapterFromLuid ->
 *     CreateDevice -> CreatePagingQueue -> ReserveGpuVirtualAddress), not one
 *     CreateFileW. Duplicating that in every consumer guarantees drift.
 *   - the LUID is the identifier a D3D consumer already has. DXGI hands
 *     IDXGIAdapter::GetDesc().AdapterLuid to the UMD, and the D3D10/11 UMD
 *     DDI is given its adapter by the runtime. Keying on LUID means the D3D
 *     path calls agx_d3dkmt_open_device() with an identifier it was given,
 *     while Honeykrisp -- which is handed nothing -- calls
 *     agx_d3dkmt_enumerate_adapters() first.
 *   - the LUID is also the identity Vulkan must report
 *     (VkPhysicalDeviceIDProperties::deviceLUID), which is what lets an
 *     application match a VkPhysicalDevice to a D3D device. A raw NT file
 *     handle carries no such identity.
 */

#include <stdbool.h>
#include <stdint.h>

#include "agx_d3dkmt.h"
#include "agx_d3dkmt_core.h"
#include "agx_d3dkmt_runtime.h"
#include "agx_device.h"

struct util_sync_provider;

/*
 * Fill `out` with the LUIDs of every WDDM adapter whose
 * KMTQAITYPE_UMDRIVERPRIVATE caps blob identifies it as an AGX GPU this
 * winsys understands. Writes at most `capacity` entries; *count always
 * receives the number found (which may exceed `capacity`). Returns false only
 * if D3DKMT itself is unavailable -- "no AGX adapter present" is a successful
 * enumeration of zero.
 */
bool agx_d3dkmt_enumerate_adapters(struct agx_kmt_luid *out, uint32_t capacity,
                                   uint32_t *count);

/* Resolve the one adapter whose complete private identity matches `identity`.
 * Every temporary handle returned by D3DKMTEnumAdapters2 is closed before
 * this returns. Ambiguous identical adapters are rejected instead of merging
 * their process GPU-VA namespaces. */
bool agx_d3dkmt_find_adapter_luid(
   const struct agxw_adapter_identity *identity,
   struct agx_kmt_luid *luid);

/*
 * Open `luid` and install the D3DKMT ops on `dev`. Mirrors
 * agx_virtio_open_device()'s contract: on success the caller runs
 * agx_open_device(memctx, dev) next for the backend-agnostic bring-up
 * (GET_PARAMS, VM_CREATE, zero/scratch/printf BOs, ...).
 */
bool agx_d3dkmt_open_device(struct agx_device *dev,
                            const struct agx_kmt_luid *luid);

/* Install the same AGX winsys on a D3D-runtime-owned device.  The callback
 * bridge creates child paging/context/allocation objects but this function
 * never opens or destroys an adapter/device behind the runtime's back. */
bool agx_d3dkmt_open_runtime_device(
   struct agx_device *dev,
   const struct agx_d3dkmt_runtime_device *runtime_device);

/*
 * Tear down what agx_d3dkmt_open_device() built. Call AFTER
 * agx_close_device(), which still owns the BO cache and VMA heaps.
 */
void agx_d3dkmt_close_device(struct agx_device *dev);

/* The adapter LUID `dev` was opened from, for VkPhysicalDeviceIDProperties
 * and for matching against a D3D adapter. */
struct agx_kmt_luid agx_d3dkmt_device_luid(const struct agx_device *dev);

/* Resource-backed external memory. `handle` is an owning NT HANDLE for
 * OPAQUE_WIN32 and the zero-extended, non-owning hGlobalShare identifier for
 * OPAQUE_WIN32_KMT. Import never closes either kind. Export returns a fresh
 * owning NT HANDLE for OPAQUE_WIN32 and the retained identifier for KMT. */
struct agx_bo *agx_d3dkmt_bo_create_external(
   struct agx_device *dev, size_t size, unsigned align,
   enum agx_bo_flags flags,
   enum agx_d3dkmt_external_memory_type external_type, const char *label);

/* Create the sole allocation backing a D3D runtime resource.  This path is
 * available only on agx_d3dkmt_open_runtime_device() devices: dxgkrnl owns
 * the hRTResource namespace, while the returned BO owns the corresponding
 * KMD allocation, GPU-VA binding and renderer references. */
struct agx_bo *agx_d3dkmt_bo_create_runtime_resource(
   struct agx_device *dev, size_t size, unsigned align,
   enum agx_bo_flags flags, void *runtime_resource, const char *label);

struct agx_bo *agx_d3dkmt_bo_create_runtime_resource_with_driver_data(
   struct agx_device *dev, size_t size, unsigned align,
   enum agx_bo_flags flags, void *runtime_resource,
   const void *resource_driver_data, uint32_t resource_driver_data_size,
   const char *label);

/* Adopt the allocation handles which dxgkrnl supplied to the D3D12
 * OpenHeapAndResource DDI.  The runtime owns residency policy; this only
 * installs the allocation in the renderer BO map and binds a process GPU VA.
 */
struct agx_bo *agx_d3dkmt_bo_open_runtime_resource(
   struct agx_device *dev, size_t size, unsigned align,
   enum agx_bo_flags flags, void *runtime_resource,
   uint32_t allocation, uint32_t resource, const char *label);

/* Rotate live DXGI runtime-resource identities without moving the BO
 * objects or their pointer-sized runtime lifetime tokens.  Kernel allocation
 * handles, KMD resource handles, GPU-VA objects, mappings and writer state
 * move left in array order.  Every BO must belong to the same runtime-owned
 * D3DKMT device and have an identical allocation shape. */
bool agx_d3dkmt_bo_rotate_runtime_identities(struct agx_device *dev,
                                             struct agx_bo *const *bos,
                                             uint32_t count);

/* Runtime-private data is carried by dxgkrnl with the shared resource and is
 * returned verbatim to another process opening it. It is frontend metadata,
 * never interpreted by the miniport. The plain entry points above/below are
 * equivalent to passing no runtime data. */
struct agx_bo *agx_d3dkmt_bo_create_external_with_runtime_data(
   struct agx_device *dev, size_t size, unsigned align,
   enum agx_bo_flags flags,
   enum agx_d3dkmt_external_memory_type external_type,
   const void *runtime_data, uint32_t runtime_data_size, const char *label);

struct agx_bo *agx_d3dkmt_bo_import_external(
   struct agx_device *dev, uintptr_t handle,
   enum agx_d3dkmt_external_memory_type external_type, const char *label);

struct agx_bo *agx_d3dkmt_bo_import_external_with_runtime_data(
   struct agx_device *dev, uintptr_t handle,
   enum agx_d3dkmt_external_memory_type external_type, void *runtime_data,
   uint32_t *runtime_data_size, const char *label);

int agx_d3dkmt_bo_export_external(
   struct agx_device *dev, struct agx_bo *bo,
   enum agx_d3dkmt_external_memory_type external_type, uintptr_t *handle);

/* D3D shared handles may carry a Win32 name, security descriptor and access
 * mask. `object_attributes` is an opaque OBJECT_ATTRIBUTES pointer so this
 * frontend-neutral header does not expose NT types. NULL selects the same
 * unnamed defaults as agx_d3dkmt_bo_export_external(). */
int agx_d3dkmt_bo_export_external_with_attributes(
   struct agx_device *dev, struct agx_bo *bo,
   enum agx_d3dkmt_external_memory_type external_type,
   const void *object_attributes, uint32_t access, uintptr_t *handle);
int agx_d3dkmt_bo_make_resident(struct agx_device *dev, struct agx_bo *bo);
int agx_d3dkmt_bo_evict(struct agx_device *dev, struct agx_bo *bo);

/*
 * The Windows util_sync_provider, backed by D3DKMT monitored fences.
 *
 * Built from `dev` rather than from a bare handle (unlike
 * util_sync_provider_ntagx) because it MUST share the syncobj table with the
 * winsys: drm_asahi_submit carries syncobj handles, so submit() has to be
 * able to resolve the same handles this provider mints. The provider does not
 * own `dev` and must be finalized before agx_d3dkmt_close_device().
 */
struct util_sync_provider *util_sync_provider_d3dkmt(struct agx_device *dev);

/* External D3D12 fences are the same monitored-fence objects used by the
 * util sync provider, registered in its dense handle table so queue waits,
 * CPU waits and AGX submissions all name one WDDM object. */
int agx_d3dkmt_syncobj_create_external(struct agx_device *dev,
                                       uint64_t initial_value,
                                       uint32_t *handle);
int agx_d3dkmt_syncobj_import_external(struct agx_device *dev,
                                       uintptr_t nt_handle,
                                       uint32_t *handle);
int agx_d3dkmt_syncobj_export_external(
   struct agx_device *dev, uint32_t handle, const void *object_attributes,
   uint32_t access, uintptr_t *nt_handle);
int agx_d3dkmt_syncobj_query(struct agx_device *dev, uint32_t handle,
                             uint64_t *value);
int agx_d3dkmt_open_nt_handle_from_name(struct agx_device *dev,
                                        const void *object_attributes,
                                        uint32_t access,
                                        uintptr_t *nt_handle);

/* Return the runtime-transport token naming a queue's WDDM context.  The D3D
 * UMD translates this token back to the opaque HANDLE required by Present;
 * renderer code never casts or truncates the handle itself. */
bool agx_d3dkmt_queue_context_token(struct agx_device *dev,
                                    uint32_t queue_id, uint32_t *token);

extern const agx_device_ops_t agx_device_d3dkmt_ops;
