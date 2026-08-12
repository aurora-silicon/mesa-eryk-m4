# Native AGX D3D12 WDDM UMD frontend

This document is the source inventory, ABI pin and implementation map for the
WDDM user-mode driver frontend.  It describes measured source and header state
in the `codex/native-agx-d3d12-umd` worktree, not a proposed second renderer.

The governing rule is simple: Direct3D runtime objects and WDDM callbacks are
frontend concerns.  DXIL translation, NIR/AGX compilation, pipeline state,
resource layouts, descriptor contents, command encoding and AGX submission are
one renderer core shared with the existing public COM frontend.

## 1. Complete source inventory and ownership

The inventoried COM/core baseline was 48,249 lines before this frontend. The
current shared tree is 62,835 lines across C/header sources, excluding tests
and documentation. Its renderer is split by object kind even though the
original public entry surface is COM.

| source | current responsibility | WDDM reuse/seam |
|---|---|---|
| `d12_device.c` | COM device, feature queries, view creation, device-wide AGX allocations | `d12_device_backend_init/cleanup` now initialize the AGX compiler/render payload independently of platform discovery; both frontends can use it. |
| `d12_cmd.c` | command allocator/list state, draw/dispatch/copy/barrier lowering, USC/PPP/VDM/CDM encoding | Reuse the recorder and encoding directly; DDI command-list methods translate DDI descriptors/handles into these calls. |
| `d12_pipeline.c` | graphics/compute/mesh PSO parsing, shader compilation, linking and caches | Reuse directly. DDI runtime-owned private PSO memory stores a core PSO pointer until allocation is made intrusive. |
| `d12_resource.c` | buffer/texture validation, `ail` layout, BO and sparse resource state | Reuse validation/layout. Replace COM/D3DKMT allocation ownership at the seam with `pKTCallbacks` allocation/GPUVA operations. |
| `d12_descriptor.c` | descriptor heap backing and shader-visible upload | Reuse descriptor representation and upload. DDI handle calculation supplies runtime-private heap storage. |
| `d12_queue.c` | command-list aggregation, AGX submit and queue fence ordering | Reuse batching/submission. WDDM queue wrapper creates runtime/KMD contexts and submits through DDI callbacks rather than opening a parallel KMT device. |
| `d12_fence.c` | timeline fence state, waits, events and sharing | Reuse timeline policy; bind it to WDDM monitored-fence callbacks. |
| `d12_query.c` | query heaps | Reuse directly behind DDI private handles. |
| `d12_root_sig.c` | RTS0 parsing/serialization and binding assignment | Pure reusable core except COM blob/deserializer wrappers. |
| `d12_format.c` | DXGI-to-pipe/AGX format support | Reuse directly for `pfnCheckFormatSupport`, resources, views and presentation. |
| `dxil/d12_dxil_*` | DXBC container and DXIL bitstream/module parsing, DXIL-to-NIR | Reuse without a DDI fork. |
| Asahi compiler/lib/layout/genxml/poly | NIR optimization, AGX codegen/linking, layouts, packet definitions and geometry/tessellation support | Reuse without changes specific to WDDM. |
| `d12_platform_linux.c` | DRM discovery/sync/sharing | COM-platform implementation only. |
| `d12_platform_windows.c` | COM-side LUID discovery and direct D3DKMT device/sync/sharing | Keep for the public COM DLL. It is not the WDDM DDI transport because the D3D runtime already owns the device and supplies callbacks. |
| `d12_stubs.h`, `d3d12agx.def` | COM failure stubs and public D3D12 exports | COM glue only. |
| `umd/` | `OpenAdapter12`, DDI negotiation/tables, KMD callback transport, strict build and ABI probes | New thin WDDM frontend. |

The first concrete shared seam is `d12_core_caps.[ch]`.  It contains no COM,
WDK or Mesa types and is consumed by both `ID3D12Device::CheckFeatureSupport`
and the UMD `GetCaps` translator.  Capability claims can no longer drift
between the two frontends.

The second seam is `d12_device_backend_init/cleanup`.  The public COM factory
still discovers an adapter through `d12_platform_open_device`, then hands the
already-opened `agx_device` to this shared constructor.  The DDI frontend now
initializes that same renderer through the opaque
`d12_device_create_d3dkmt_runtime` constructor.  Its winsys operations call
the runtime-owned `D3DDDI_DEVICECALLBACKS` and never create a second D3DKMT
device. Internal AGX kernels, scratch pools, NULL descriptors, sampler state
and the geometry/tessellation heap are constructed once by shared code; COM
discovery is no longer entangled with renderer initialization.

`libd3d12agx_core` is the one native renderer archive. Both `d3d12agx.dll`
(public COM) and `d3d12agx_umd.dll` (WDDM DDI) link it whole. The strict DDI
archive contains only ABI translation/lifetime code and includes a narrow
opaque runtime constructor rather than Mesa's renderer-private header graph.

The remaining refactor seam is object lifetime.  COM objects currently begin
with a vtable and allocate themselves with `calloc`.  A DDI object instead
lives in runtime-provided memory whose size was returned by a `CalcPrivate*`
entrypoint.  The staged adapter stores a pointer to a renderer-core object in
that private memory.  Once a type is stable, its core payload can be embedded
directly.  No compiler, layout or renderer subsystem is copied.

## 2. Pinned Microsoft and Apple ABIs

The authoritative Microsoft contract for this tree is the locally pinned WDK
and SDK `10.0.26100.0`, package build `26100.6584`.  In particular:

- WDK `um/d3d12umddi.h` defines the D3D12 UMD entry types and tables.
- WDK `um/d3dumddi.h` defines adapter/device callbacks into dxgkrnl/KMD.
- SDK `um/dxgiddi.h` defines the DXGI presentation DDI and callback tables.
- SDK `shared/d3dkmthk.h`, `d3dkmdt.h` and `d3dukmdt.h` define public KMT and
  allocation/context/GPUVA structures.
- `drivers/AppleAgxWddm/core/agxw_contract.h` is included as source, never
  transcribed.  Its 48-byte `agxw_adapter_identity` is the exact
  `UMDRIVERPRIVATE` discovery payload.

The checked-in build uses only those headers.  The Microsoft header index and
official DDI pages are:

- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddiarg_openadapter>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_openadapter>
- <https://learn.microsoft.com/windows-hardware/drivers/display/version-discovery-support>
- <https://learn.microsoft.com/windows-hardware/drivers/display/wddm-2-1-features>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_fillddittable>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtenumadapters2>

### Version matrix

| layer | pinned/implemented value | meaning |
|---|---:|---|
| AppleAgxWddm DXGK KMD interface | `0x5023` | `DXGKDDI_INTERFACE_VERSION_WDDM2_0`, asserted by the KMD. |
| KMD capability result | `DXGKDDI_WDDMv2` | The adapter currently reports WDDM 2.0, GPU MMU, GPUVA, one node and coherent UMA. |
| UMD callback-layout define | `0x5002` | `D3D_UMD_INTERFACE_VERSION_WDDM2_0`; exposes the WDDM 2.0 runtime callback members used by D3D12. |
| First complete D3D12 DDI target | interface `0x000c0002`, build `8` | `D3D12DDI_SUPPORTED_0003`, encoded as `0x000c000200080000`. |
| Release 1 pipeline-library target | interface `0x000c000a`, build `0` | `D3D12DDI_SUPPORTED_0010`, encoded as `0x000c000a00000000`; the 91-slot core table and all six library DDIs are implemented. |
| Release 1 Shader Model target | interface `0x000c000a`, build `1` | `D3D12DDI_SUPPORTED_0011`, encoded as `0x000c000a00010000`; reports released SM 6.0 and SM 5.1. |
| Latest revision in pinned header | interface R8 `0x000c0050`, build `110` | `D3D12DDI_SUPPORTED_0110`, encoded as `0x000c0050006e0000`. It is a header ceiling, not an implemented claim. |

The D3D12 core DDI revision and WDDM/DXGK interface are separate negotiations.
Raising AppleAgxWddm above WDDM 2.0 is not a prerequisite for the baseline
native D3D12 path. Release 1 is now a separate, complete UMD negotiation path:
`GetSupportedVersions` returns `0011`, `0010`, then `0003`, and table size
selects the exact 91- or 89-slot layout. Microsoft classifies DXIL, SM 6.0 and
PSO libraries as WDDM 2.1 features, however, and requires a driver claiming a
WDDM 2.1 capability to satisfy that model's mandatory KMD requirements. The
current AppleAgxWddm KMD still asserts `0x5023`/WDDM 2.0. Therefore Release 1
is build- and ABI-ready, but runtime selection is a coordinated KMD-model gate;
it is not evidence that changing only the INF or a version number is safe.
Later DDI values such as `0110` remain unlisted until their exact tables and
caps are implemented.

## 3. Entry and adapter ABI

The native ARM64 DLL exports exactly `OpenAdapter12`.  Its argument is
`D3D12DDIARG_OPENADAPTER`:

1. `hRTAdapter` is the runtime handle and is retained.
2. `pAdapterCallbacks->pfnQueryAdapterInfoCb` is called before device creation.
3. The callback asks AppleAgxWddm for exactly 48 bytes of
   `agxw_adapter_identity`; magic, payload version, struct size, contract
   version and AGX params size are validated.
4. `hAdapter.pDrvPrivate` receives the UMD adapter object.
5. `pAdapterFuncs` receives all eight required adapter functions:
   private-device size, create device, close, supported versions, caps,
   optional-table enumeration, DDI-table fill and destroy device.

The build has compile-time assertions for pointer/handle widths, every field
offset in `D3D12DDIARG_OPENADAPTER`, the 64-byte adapter table, the 48-byte
`CREATEDEVICE_0003` structure and the KMD identity.  The PE audit requires
machine `ARM64` and an export set equal to `{OpenAdapter12}`.

Device creation receives three callback families:

- `D3DDDI_DEVICECALLBACKS` for allocation, GPUVA, KMD synchronization,
  paging/residency and submit operations;
- `D3D12DDI_CORELAYER_DEVICECALLBACKS_*` for runtime error reporting,
  command-list table selection and D3D12-specific context/paging calls;
- DXGI callbacks through `DXGI_DDI_BASE_ARGS` when the runtime requests the
  `D3D12DDI_TABLE_TYPE_DXGI` table.

`CreateDevice` accepts the exact `0003`, `0010` or `0011` interface/build,
copies the runtime callback tables into runtime-owned private device storage
and, in the renderer-linked DLL, constructs the native backend through those
callbacks.
It first resolves the exact private adapter identity to a KMT LUID. That LUID
is the stable key used by every Mesa UMD module's process-wide GPU-VA broker;
the opaque runtime adapter pointer is never used as an address-space identity.
All temporary handles returned by `D3DKMTEnumAdapters2` are closed as required
by Microsoft's contract, and no second graphics device is opened.
`FillDDITable` fills all 89 device, 51 command-list and 7 command-queue slots
from a generator that parses the pinned WDK header. Implemented entries replace
the generated traps. Remaining void functions report `E_NOTIMPL` through
`pfnSetErrorCb`; paired destructors are silent because their trapped creators
allocated nothing. No baseline table has a callable NULL slot (the two WDK
queue fields explicitly named `pfnUnused*` remain NULL by contract).
The runtime handle supplied while filling the command-list table is retained
per adapter and rebound to every successfully created command list through
`pfnSetCommandListDDITableCb`, as required by the core-layer dispatch ABI.
There are concrete implementations behind all 89 baseline device slots.
`pfnRetrieveShaderComment` lazily disassembles exact native AGX executable
ranges and returns bounded, escaped, well-formed XML with program roles,
code sizes and compiler statistics; ordinary PSO creation pays no disassembly
cost. The Release 1 device table contains 91 slots because its six pipeline-
library functions replace the four baseline serialized-object functions; all
91 are populated with correctly typed entrypoints. The command-list
`CopyTiles` and queue tile-mapping entries remain explicit traps while
`TiledResourcesTier` is honestly reported as zero.

## 4. Private object and resource ABI

For every DDI-created object, Windows follows a size-then-create contract:

1. the runtime calls `CalcPrivate*Size`;
2. it allocates storage of that exact size;
3. create receives an opaque `D3D12DDI_H*` whose `pDrvPrivate` points to it;
4. destroy tears down content but does not free runtime storage.

No DDI object may use COM reference counting as its authoritative lifetime.
Shared renderer payloads may remain internally reference counted where command
lists or queues hold them, but the DDI wrapper has a single runtime owner.

Pipeline state also implements the baseline serialized-object lifetime. A
created compute or graphics PSO retains a deterministic, versioned,
pointer-free snapshot of its root signature, shaders and fixed-function state.
`SerializeObject` returns a driver-owned copy, `DestroyObjectSerialization`
releases that copy, `CalcPrivateDeserializedObjectSize` validates the complete
blob before the runtime allocates storage, and `CreateDeserializedObject`
reconstructs the same native AGX root signature and PSO through the shared
renderer seam. The format has strict object/count/size caps, a fixed-width
header and a checksum over both header and payload; corrupt, truncated and
non-canonical input fails before any backend allocation. It currently stores
frontend state and shader containers, so deserialization re-enters the existing
AGX compiler/cache path. Embedding a portable compiled-code cache record is a
future latency optimization, not a separate renderer or ABI requirement.

Release 1 implements the distinct indexed pipeline-library ABI. The runtime's
`PipelineIndex` is stored in a sorted lookup table containing the same
canonical pointer-free PSO snapshots. Library blobs have deterministic
ordering, fixed-width headers, whole-library and per-entry hashes,
duplicate-index rejection, strict count/size caps and transactional import. A
live library retains the native AGX PSO, so a library hit reuses compiled state
without shader recompilation. A reloaded persistent blob recreates the PSO
through the shared compiler/cache seam on first use and then retains it for
subsequent hits. No renderer subsystem or machine pointer is serialized.

Shader creation accepts both the self-sized SM4/SM5 tokenized program format
and complete DXBC containers carrying DXIL. Both paths are structurally
validated before allocation or compiler entry; container chunk bounds and
shader kind are checked, and a real SM 6.0 compute fixture is exercised by the
host suite. DXIL containers pass unchanged into the existing DXIL-to-NIR/AGX
compiler, while token streams receive the minimal signature chunks required by
the shared DXBC compiler.

Heap/resource creation additionally has two private ABIs:

- runtime-private UMD storage returned by
  `CalcPrivateHeapAndResourceSizes_*`; and
- KMD allocation private data.  The latter is exactly `agxw_alloc_private`
  from `agxw_contract.h`, passed to `pfnCreateAllocationCb`; it is not a new
  IOCTL or escape channel.

GPU virtual addresses are reserved/mapped/freed through the WDDM 2.0 members
of `D3DDDI_DEVICECALLBACKS`.  AGX native 16 KiB pages and the WDDM 64 KiB
reservation granularity remain separate, as already encoded and host-tested
in `agx_d3dkmt_core`.  Resource layout selection remains `ail`; WDDM supplies
allocation ownership and residency, not a new texture layout implementation.
The pre-device texture-layout query reports zero private layouts/swizzles and
does not advertise row-major textures. Buffers retain their required row-major
layout. Row-major textures remain gated until the frontend descriptor seam
preserves the runtime's explicit row and depth pitches instead of silently
recomputing them.

Heap/resource creation is attached to the real renderer object graph. A DDI
heap is one runtime-owned `agx_bo` and one shared `d12_heap`; placed buffers and
textures use the existing `d12_resource` and `ail` layout implementation. The
frontend supports descriptor heaps and SRV/UAV/RTV/DSV/CBV/sampler creation,
mapping, GPU virtual addresses, exact allocation-handle queries, subresource
layout queries and the D3D12 MakeResident/Evict callback model.

Application heaps are deliberately not made resident by the winsys behind the
runtime. The runtime's DDI MakeResident/Evict calls own their residency count;
only renderer-internal BOs retain winsys-managed paging. Runtime allocations
use a resource association supplied by dxgkrnl, so the callback transport
passes only the legal allocation hint instead of issuing a second
CreateResource/CreateShared request.

Created resources carry a versioned 176-byte resource-private blob. Its first
16 bytes are the unchanged `agxw_resource_private` KMD ABI; its frontend tail
preserves heap, canonical resource, clear-value and row-major-layout metadata.
`CalcPrivateOpenedHeapAndResourceSizes` and `OpenHeapAndResource` validate the
KMD allocation blob and reconstruct a renderer heap/resource around handles
opened by dxgkrnl. Opening arbitrary metadata emitted by a different D3D11 or
D3D12 UMD is not yet supported; native D3D12 cross-process open is supported,
while cross-UMD DXGI sharing remains a recorded compatibility gate.

## 5. Queues, fences, residency and submission

The baseline queue table is `D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001`:
execute command lists, tile mappings, fence signal and fence wait.  Each queue
maps to an AppleAgxWddm virtual context using the existing
`agxw_context_private` contract.  Queue submission reuses the renderer's flat
AGX command stream and existing `agxw_dma_header`/`agxw_submit_private` bytes,
but invokes the runtime's submit callback for the device/context it supplied.
It must not call `D3DKMTCreateDevice` and create a second device behind the
runtime.

The runtime-owned AGX device constructor creates the paging queue, reserves
the compiler-visible 4 GiB USC range at the renderer-selected address, waits
the paging fence when necessary and creates internal VA/submission contexts
through the kernel callback table. DDI queues wrap the shared renderer queue,
and `ExecuteCommandLists` aggregates and submits the renderer's native AGX
command segments through the callback-backed winsys. `d12_umd_kmt_submit_dma`
marshals the existing `agxw_dma_header`/`agxw_submit_private` contract into
`pfnSubmitCommandCb`; no side-channel escape or private IOCTL was added.

The callback transport implements WDDM monitored-fence synchronization and
the existing AGX timeline policy. DDI fence objects retain the runtime's
placement allocation. Queue Signal emits an ordered AGX compute write to that
placement, and queue Wait observes its coherent value before later work.
MakeResident/Evict and Offer/Reclaim forward runtime-owned allocation handles
through the core callback ABI. The translator accepts the complete baseline
set of pageable D3D12 object kinds. Heaps/resources contribute their actual
VidMm allocation; queues, PSOs, pipeline libraries, descriptor/query heaps,
fences and command signatures are ownership-validated but contribute no
fabricated handle because their state is CPU-only or backed by independently
resident renderer-internal allocations. Mixed requests retain an allocation-
to-original-object map, so Reclaim propagates discarded-content results to
the correct object while leaving nonallocation objects nondiscarded. Empty
requests are valid no-ops. An asynchronous paging fence is returned whenever
the runtime callback returns `E_PENDING`. Sparse tile mappings remain gated
and the driver advertises tiled-resources tier zero until they are complete.
The existing direct-D3DKMT winsys remains valid for the public COM DLL only.

### Performance and WDDM memory-model invariants

- No D3D12-to-D3D11, DXVK or command-stream translation layer exists.
- The runtime owns the adapter/device. The renderer calls its callback table
  directly; there is no hidden `D3DKMTCreateDevice` in the DDI path.
- Allocations, residency, paging fences and GPU-VA updates remain explicit
  WDDM objects. UMA does not mean “always resident” or justify bypassing VidMm.
- Context HANDLEs are stored at native pointer width in a bounded bridge table;
  renderer-side 32-bit IDs are tokens and never truncated HANDLE values.
- Hot submission callbacks do fixed-field translation only. Heap allocation is
  absent from submit. Residency batches of up to 16 runtime objects use fixed
  stack storage; larger `MakeResident`, `Evict`, `OfferResources`, and
  `ReclaimResources` batches use one bounded spill allocation. The remaining
  variable-length GPU-VA update operation array is outside draw/dispatch
  submit.
- Version/capability exposure is correctness-gated. Performance claims require
  ETW/GPUView traces, residency-pressure tests and application benchmarks on
  the Windows target.

## 6. DXGI presentation

Presentation has two connected DDI surfaces:

- the core table reports present-private-data size and the command-list table
  implements `pfnPresent`, producing `D3D12DDI_PRESENT_0003`; and
- `FillDDITable(D3D12DDI_TABLE_TYPE_DXGI, ...)` fills the versioned
  21-slot WDDM 2.0 `DXGI1_4_DDI_BASE_FUNCTIONS` surface. Its
  `DXGI_DDI_BASE_ARGS` callback-table pointer is retained (not individual
  callback addresses), including `pfnPresentCb`; the two SDK-reserved slots
  remain NULL.

Swap-chain back buffers are ordinary WDDM allocations associated with a KMD
resource. The command-list Present implementation returns the allocation and
queue context in `D3D12DDI_PRESENT_0003`; the DXGI base table forwards
flip presentation through the runtime callback using a live direct-queue
context. `Present1` validates the complete stereo/multi-surface array, keeps
the WDDM 2.0 LDA multiplicity at one, translates only the final source and
makes exactly one callback as required by the Microsoft DDI; dirty rectangles
are accepted as optional optimization hints and never alter correctness.
Reserved flag bits, conflicting blit/flip modes and out-of-range flip intervals
fail before the callback. The current KMD Present contract carries one
allocation per source and no subresource selector, so swap-chain presentation
is intentionally restricted to subresource zero until that shared contract is
extended. The base table also translates DXGI priority/residency queries to
batched WDDM callbacks. Residency aggregation returns the exact legacy Direct3D success
status (`S_OK`, `S_RESIDENT_IN_SHARED_MEMORY`, or `S_NOT_RESIDENT`) required
by the DXGI DDI while also filling every per-resource status. DXGI
Offer/Reclaim validates each driver resource, translates it
to its VidMm allocation and calls the matching WDDM 1.2 allocation callback;
Reclaim forwards the optional per-resource discarded-content results without
inventing an asynchronous fence that the legacy DXGI ABI cannot return.
`TrimResidencySet` releases only zero-reference renderer-internal BO cache
entries: periodic requests use cache generations to drop allocations not
referenced since the previous request, restart advances that baseline, and
budget requests evict oldest entries only until the requested byte target is
met. Application heaps remain on VidMm's explicit device residency list.
`RotateResourceIdentities` performs the DXGI-mandated left rotation of kernel
allocation, KMD-resource and GPU-VA identities while keeping UMD object
addresses and runtime resource handles fixed. The D3DKMT dense-handle pool
preserves each runtime lifetime token, live CPU mappings fail transactionally,
and the renderer repatches every tracked D3D12 descriptor plus its coherent
AGX texture/PBE side tables. This avoids both stale persistent views and the
premature-deallocation bug caused by swapping whole backend objects.
Composition presents use the renderer command-list copy path. The
D3D12 command-list `pfnBlt` handles identity texture-region copies and
full-subresource MSAA resolves directly in the native recorder. DXGI `Blt`
and `Blt1` reuse that translator through a transient native command list on
the live direct queue, covering exact same-format copies and full-subresource
resolves without a D3D11 or CPU detour. A shared frontend-neutral classifier
routes scaling, 90/180/270-degree rotation, partial resolve and display-format
conversion to a single native AGX compute dispatch. The kernel reads and
writes AIL linear/tiled layouts directly, applies sample resolve before
bilinear sampling, preserves sRGB transfer semantics, and covers the common
8/10/16-bit and packed 5:6:5/5:5:5:1 swap-chain formats. Compound operations
such as resolve plus conversion plus scaling remain one pass; missing required
DXGI `Stretch`/`Convert` flags and reserved bits fail before recording.
Each direct queue owns one serialized internal presentation recorder: its
allocator and list are created lazily, reset after each successful submit and
discarded on any record/close/execute failure. Steady-state composition blits
therefore add no allocator/list object churn while retaining queue-local WDDM
ordering and safe destruction under the device queue lock.
Multiplane overlays and render-adapter
mode setting are likewise reported unsupported. DXGI/dxgkrnl retains scanout
scheduling ownership. Device creation returns the successful
`DXGI_STATUS_NO_REDIRECTION` status while foreign-UMD shared-redirection
metadata is gated, so DXGI selects this explicit Present path instead of
assuming an unsupported DWM shared-resource path. No
D3D12-through-D3D11 or DXVK interop is part of this path. Triton remains in the
DX9/DX10/DX11 slots during bring-up and the native DLL occupies only the
D3D12 `KMTUMDVERSION` slot.

## 7. Compileable delivery gates

| gate | success criterion | state |
|---|---|---|
| A. adapter | ARM64 DLL loads, `OpenAdapter12` validates Apple identity, returns exact adapter table/version/caps | implemented, including conservative texture-layout discovery, and statically/host tested |
| B. device/core tables | `CreateDevice` succeeds only with complete versioned device, command-list and queue tables; runtime error callback used on void-call failure | `0003` 89-slot and Release 1 91-slot device tables plus shared 51/7 command tables implemented and statically/loader-probe tested; command-list runtime table handles are retained/rebound; DXGI is gate G |
| C. heaps/resources | allocation-size checks, committed/placed resources, mapping and descriptor heaps use runtime/KMD callbacks and `agxw_alloc_private` | implemented for native buffers/textures, descriptor/view creation, GPUVA, map, private open ABI and allocation/subresource queries; foreign-UMD resource metadata pending |
| D. PSO/command lists | DDI descriptors translate to the existing root-signature, DXIL, PSO and command recorder core; draw/dispatch/copy tests execute | graphics/compute PSO, deterministic serialization/deserialization, shaders/root signatures including GS-with-stream-output and passthrough SO, draw/dispatch, bundles, state, barriers, copies, clears, discard hints, resolve, identity blits, queries/predication, stream-output targets and proven ExecuteIndirect forms translated to shared renderer; hardware DDI validation pending |
| E. queues/fences | virtual contexts, submit, monitored fences, GPU/CPU signal/wait | renderer queue binding, execute aggregation and placement-fence GPU signal/ordered wait implemented; hardware DDI validation pending |
| F. residency/paging | paging queue, make-resident/evict/offer/reclaim and device-removed paths | paging queue/GPUVA, mixed pageable-object core residency, DXGI batched Offer/Reclaim and generation/budget-bounded renderer cache trim implemented and ABI-probed, including discard remapping, optional discard output and CPU-only core objects; device removal and pressure validation pending |
| G. presentation | DXGI table, back-buffer present, windowed and fullscreen swap-chain probes | exact WDDM 2.0 21-slot DXGI table, core Present, single- and multi-surface Present1 translation, flip callback, priority, residency, synchronous DXGI Offer/Reclaim, kernel-identity rotation with descriptor repatching, direct-queue exact copy/resolve, and one-pass native AGX stretch/rotation/format-conversion blits implemented; MPO and mode-setting fail closed; real swap-chain hardware validation pending |
| H. revision/feature growth | advertise later DDI revisions and FL/SM/options only after version-specific tables and conformance tests pass | Release 1 builds `0010`/`0011` implemented: 91-slot table, pipeline libraries, DXIL import and SM 6.0 caps are statically/host tested; coordinated WDDM 2.1 KMD model and hardware conformance remain pending |
| I. packaging | register native ARM64 D3D12 slot alongside D3D11, then make it default after hardware probe | conflict-free stage tool implements the fourth D3D12 slot; signed rev-236 r6 package is self-contained and passes Aurora's INF, ARM64 PE/import/export, clean WinPE load, manifest and catalog-digest verifier; target install pending exclusive hardware access |

### Recorded gates before a production claim

- No hardware command has been sent from this branch while the shared Windows
  target is under concurrent bring-up. `d12_umd_runtime_probe.exe` is the first
  authorized runtime/GPU validation and must be run only by the hardware owner.
- `0003` remains the current WDDM 2.0-compatible hardware baseline. Release 1
  `0010`/`0011` has complete UMD tables and caps, but its DXIL/SM6/PSO-library
  selection must be validated with a coordinated WDDM 2.1 KMD target. A higher
  WDDM number by itself does not implement the mandatory KMD contracts.
- Tiled/sparse resources, unsupported ExecuteIndirect mutable-state forms and
  foreign-UMD shared-
  resource metadata are not advertised as complete. Baseline `0003` PSO
  object serialization, Release 1 indexed pipeline libraries and native AGX
  shader-disassembly XML are implemented. Persistent library blobs currently
  warm the existing compiler/cache on first load instead of embedding portable
  compiled AGX code; eliminating that first-load latency remains an
  optimization gate.
- Real DXGI windowed/fullscreen presentation, residency pressure, TDR/device
  removal, ETW/GPUView traces and application benchmarks remain mandatory
  before performance or production-readiness claims.

## 8. Reproduction

From the Mesa worktree root:

```sh
src/asahi/d3d12/umd/run-host-tests.sh
src/asahi/d3d12/umd/build-windows-arm64.sh
src/asahi/d3d12/umd/build-mesa-windows-arm64.sh
```

The standalone UMD and Aurora-owned source use `-Wall -Wextra -Werror` plus
conversion, shadow, prototype, undef and pedantic diagnostics. The full Mesa
renderer artifact is a release/NDEBUG build while retaining those fatal flags
on the UMD adapter targets; unrelated upstream Mesa utilities remain at the
project warning policy. The WDK staging directory is a system include path
because Microsoft's published C headers intentionally use MSVC extensions.
`check_pe.py` audits PE machine type, exact exports and every imported DLL,
rejecting non-system dependencies such as MinGW `libwinpthread-1.dll`.

`tools/run-d12-ddiprobe-qemu.sh` boots a networkless ARM64 WinPE image. It
first clean-loads and unloads the full renderer DLL, then loads the
callback-only DLL and executes the ABI probe. This catches constructor and
dependency failures in an environment without build-runner DLLs before any
DDI call. It performs no driver installation, adapter open or GPU access;
among other table checks it proves the exact identity-rotation direction,
fixed runtime handles, mapped-resource rejection, presentation-blit flag
validation and empty-list semantics on Windows ARM64. Load failures include
the numeric `GetLastError` value and `FormatMessageW` text.

The standalone build also emits Windows ARM64 loader, KMD-callback, queue,
runtime-transport and installed-D3D12-runtime probes. The transport probe covers allocation, residency,
paging, GPU-VA map/update, full-width context-token mapping, submit, monitored
fence and escape fields. The full build produces both frontend DLLs from the
same renderer archive and audits `d3d12agx_umd.dll` for the exact
`OpenAdapter12` export set and a Windows-system-only import closure. The r5
package is withdrawn because its UMD imported `libwinpthread-1.dll`; r6 links
the runtime path self-contained, is catalog-signed after the final DLL is
embedded-signed, and is the first package eligible for SSH-only target loader
probing.

`packaging/stage_package.py` adds the full-renderer DLL to a throwaway
AppleAgxWddm package stage, patches only D3D12 registration index 3, updates
the CDF and removes the stale catalog. Regenerate and sign the catalog before
installing. After installation run `d12_umd_hwprobe.exe` first, followed by
`d12_umd_runtime_probe.exe`; the latter enters through the Microsoft runtime
and verifies upload -> default -> readback GPU transit with a bounded fence
timeout.
