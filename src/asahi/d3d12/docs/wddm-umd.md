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
renderer remains split by object kind even though its original public entry
surface is COM; line counts are deliberately not used as a synchronization
mechanism now that the two frontends evolve together.

| source | current responsibility | WDDM reuse/seam |
|---|---|---|
| `d12_attachment_view_core.c` | frontend-neutral attachment mip/layer resolution and bounds | Used by shared view creation and a portable host unit test, keeping DDI and COM attachment semantics identical. |
| `d12_device.c` | COM device, feature queries, view creation, device-wide AGX allocations | `d12_device_backend_init/cleanup` now initialize the AGX compiler/render payload independently of platform discovery; both frontends can use it. |
| `d12_cmd.c` | command allocator/list state, draw/dispatch/copy/barrier lowering, USC/PPP/VDM/CDM encoding | Reuse the recorder and encoding directly; DDI command-list methods translate DDI descriptors/handles into these calls. |
| `d12_pipeline.c` | graphics/compute/mesh PSO parsing, shader compilation, linking and caches | Reuse directly. DDI runtime-owned private PSO memory stores a core PSO pointer until allocation is made intrusive. |
| `d12_resource.c` | buffer/texture validation, `ail` layout, BO and sparse resource state | Reuse validation/layout. Replace COM/D3DKMT allocation ownership at the seam with `pKTCallbacks` allocation/GPUVA operations. |
| `d12_descriptor.c` | descriptor heap backing and shader-visible upload | Reuse descriptor representation and upload. DDI handle calculation supplies runtime-private heap storage. |
| `d12_queue.c` | command-list aggregation, AGX submit and queue fence ordering | Reuse batching/submission. WDDM queue wrapper creates runtime/KMD contexts and submits through DDI callbacks rather than opening a parallel KMT device. |
| `d12_fence.c` | timeline fence state, waits, events and sharing | Reuse timeline policy; bind it to WDDM monitored-fence callbacks. |
| `d12_query.c`, `d12_query_core.c` | query heaps, type matching and queue-family policy | Reuse native heap/timestamp storage behind DDI private handles; the portable policy core keeps COM and DDI query ABIs aligned. |
| `d12_resolve_core.c` | typed resolve format, numeric class, mode and resource-family policy | Shared by format-cap queries and the COM/DDI command recorder; portable tests pin all 52 accepted DXGI spellings and reject typed/typeless mismatches. |
| `d12_present_blt_math.c`, `d12_present_format_core.c` | frontend-neutral blit geometry and display-format/color-space policy | Shared by DXGI validation, format caps and the native AGX recorder; portable tests pin rotation/scaling and every advertised display format. |
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
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_command_list_funcs_3d_0032>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_command_list_funcs_3d_0040>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddiarg_create_command_pool_0040>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_resourcebarrier_0022>
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtenumadapters2>
- <https://microsoft.github.io/DirectX-Specs/d3d/ResourceHeaps.html>
- <https://github.com/microsoft/graphics-driver-samples/blob/master/compute-only-sample/cosumd12/CosUmd12Adapter.cpp>

### Version matrix

| layer | pinned/implemented value | meaning |
|---|---:|---|
| AppleAgxWddm DXGK KMD interface | default `0x5023`; isolated branch `0x6003` | The shipping/dev KMD remains WDDM 2.0. `codex/wddm21-dxil` in the separate `AuroraSilicon-wddm21` worktree pins the WDK 26100.6584 WDDM 2.1 interface and is not merged into dev. |
| KMD capability result | default `DXGKDDI_WDDMv2`; isolated `DXGKDDI_WDDMv2_1` (`0x2100`) | Both retain GPU MMU, GPUVA, one node, non-cache-coherent UMA and a 39-bit AGX GPUVA space. The 2.1 branch also registers allocation-property validation and packages the native D3D12 UMD. |
| UMD callback-layout define | `0x5002`, `0x6003`, `0x8001`, `0x9001`, or `0x10000` | Meson `-Dasahi-d3d12-wddm-version=2.0|2.1|2.3|2.4|3.1` and standalone `D12_UMD_WDDM_TARGET=20|21|23|24|31` select exact callback/DXGI layouts. 2.1 is the DXIL/SM 6 minimum; 3.1 is the production-target UMD contract and remains host-only until the matching KMD/INF uplift is merged. |
| First complete D3D12 DDI target | interface `0x000c0002`, build `8` | `D3D12DDI_SUPPORTED_0003`, encoded as `0x000c000200080000`. |
| Release 1 pipeline-library target | interface `0x000c000a`, build `0` | `D3D12DDI_SUPPORTED_0010`, encoded as `0x000c000a00000000`; the 91-slot core table and all six library DDIs are implemented. |
| Release 1 Shader Model target | interface `0x000c000a`, build `1` | `D3D12DDI_SUPPORTED_0011`, encoded as `0x000c000a00010000`; reports released SM 6.0 and SM 5.1. |
| Release 1 root-signature 1.1 target | interface `0x000c000a`, build `3` | `D3D12DDI_SUPPORTED_0013`, encoded as `0x000c000a00030000`; the exact 92-slot table implements root-signature 1.1 and debug allocation/GPUVA association. |
| Release 3 immediate-write target | interface `0x000c001e`, build `2` | `D3D12DDI_SUPPORTED_0032`, encoded as `0x000c001e00020000`; the exact 92-slot device and 57-slot 3D command-list tables add the Release 2/3 allocation, queue, PSO/shader, barrier, resolve, present, copy-timestamp and immediate-write contracts. |
| Release 4 native-16 target | interface `0x000c0028`, build `2` | `D3D12DDI_SUPPORTED_0042`, encoded as `0x000c002800020000`; the exact 96-slot device and 58-slot command-list tables replace allocators with command pools/recorders, use PSO `0033`/command-list `0040` signatures and expose the shared core's released SM 6.2/native 16-bit shader capability. |
| Release 8 WDDM 3.1 target | interface R8 `0x000c0050`, build `99` | `D3D12DDI_SUPPORTED_0099`, encoded as `0x000c005000630000`; the exact 122-slot device and 73-slot command-list tables implement the mandatory `0095`/`0096`/`0099` state, `0088` resource, `0050` queue and `0051` present signatures. Cap-gated ray tracing, VRS, mesh, enhanced-barrier and meta-command paths are exact typed fail-closed stubs. |
| Latest revision in pinned header | interface R8 `0x000c0050`, build `110` | `D3D12DDI_SUPPORTED_0110`, encoded as `0x000c0050006e0000`. It is a header ceiling, not an implemented claim. |

The D3D12 core DDI revision and WDDM/DXGK interface are separate negotiations.
WDDM 2.0 advertises only baseline `0003`; it cannot accidentally negotiate
the DXIL-era tables. The isolated WDDM 2.1 build advertises `0013`, `0011`,
`0010`, then `0003`, and table size selects the exact 92-, 91- or 89-slot
layout. Build `0012` is deliberately not advertised: Microsoft gave `0012`
and `0013` the same 92-pointer table width but changed the two root-signature
callback types in `0013`, while `FillDDITable` receives only table type and
size. Advertising only the highest shape makes that callback ABI
unambiguous, matching Microsoft's sample-driver practice of selecting one
exact revision per shape family. The host-only WDDM 2.3 build advertises only
`0032`, then baseline `0003`: `0032` inherits root-signature 1.1 and the shared
DXIL compiler while selecting the exact Release 3 table signatures. Same-size
intermediate revisions are deliberately omitted for the same typed-table
reason. The host-only WDDM 2.4.2 build advertises only `0042`, then baseline
`0003`. It uses `pfnQueryAdapterInfoCb2`, exact Release 4 pool/recorder
ownership, DDI `0041` memory/options caps and shader caps `0042`. View
instancing, barycentrics, 64-bit operations and serialization tiers remain
false/tier zero, and a nonzero view-instancing PSO is rejected before backend
allocation. Its shader-model list adds released SM 6.2 and SM 6.1 ahead of
the inherited SM 6.0/5.1 entries; lower WDDM targets remain capped at SM 6.0
because the corresponding released DDI spellings do not exist before `0042`.
This split
is required because Microsoft classifies DXIL, SM 6.0 and PSO libraries as
WDDM 2.1 features. The 2.1 KMD branch reports `0x2100`, registers a real
`DxgkDdiValidateUpdateAllocationProperty` policy, uses DriverStore paths and
ships an atomic native-DX12 registration. Offer2/Reclaim3 and DXGI 1.5 are
selected only in the matching UMD build. Later DDI values such as `0110`
remain unlisted until their exact tables and caps are implemented.

The WDDM 3.1 build advertises `0099`, then the baseline compatibility shape.
It exposes the 122/73 tables, DDI 0096 extended-feature
table 27, modern options/shader caps through `0098`/`0089`/`0084`, per-face
stencil masks and references, float depth bias, dynamic strip cut, sampler
descriptor 2, queue 0050 and Present 0051. Enhanced barriers, hardware
scheduling, ray tracing, VRS, mesh shaders, sampler feedback and background
processing remain unadvertised and fail closed.

The isolated AppleAgxWddm package remains at WDDM 2.1. The `0x8001`, `0x9001`
and `0x10000` builds are not installable with that KMD and are not copied into
its INF: they are strict host compilation targets for advancing the UMD
contract without pretending that the corresponding KMD callback and
registration obligations have been uplifted.

WDDM 2.1 also requires a package-wide `AA.BB.CCCCC.DDDDD` file version.
The isolated package pins `21.17.240.0`: 21 is WDDM 2.1 and 17 is the highest
reported D3D feature level, 11.0. The INF `DriverVer`, KMD SYS and both
registered UMD DLLs carry that exact fixed and string `VERSIONINFO`. Each
build validates its own PE, and the atomic package builder and signed-package
verifier reject any mismatch before catalog or install work.

## 3. Entry and adapter ABI

The native ARM64 DLL exports exactly `OpenAdapter12`.  Its argument is
`D3D12DDIARG_OPENADAPTER`:

1. `hRTAdapter` is the runtime handle and is retained.
2. `pAdapterCallbacks->pfnQueryAdapterInfoCb` is called before device creation;
   the WDDM 2.4 target requires and uses `pfnQueryAdapterInfoCb2` with
   `D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE`.
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

`CreateDevice` accepts only the exact interface/build set advertised by the
selected target (`0003`; Release 1 `0010`/`0011`/`0013`; Release 3 `0032`;
Release 4 `0042`; or Release 8 `0099`),
copies the runtime callback tables into runtime-owned private device storage
and, in the renderer-linked DLL, constructs the native backend through those
callbacks.
It requires both runtime error channels: device/object failures are reported
through `pfnSetErrorCb`, while command-recording failures are reported through
`pfnSetCommandListErrorCb` with the exact `hRTCommandList` supplied by the
runtime. A malformed command-list callback binding falls back defensively to
the device channel, but an ordinary list validation or renderer failure never
poisons or misattributes the whole device. The adapter preserves
`E_OUTOFMEMORY` and `D3DDDIERR_DEVICEREMOVED`; other recording failures use
the D3D12 command-list callback's allowed `D3DDDIERR_APPLICATIONERROR` code.
It first resolves the exact private adapter identity to a KMT LUID. That LUID
is the stable key used by every Mesa UMD module's process-wide GPU-VA broker;
the opaque runtime adapter pointer is never used as an address-space identity.
All temporary handles returned by `D3DKMTEnumAdapters2` are closed as required
by Microsoft's contract, and no second graphics device is opened.
`FillDDITable` fills the selected exact 89/91/92/96/122-slot device table,
51/57/58/73-slot command-list table, 7-slot command-queue table and, for
Release 8, the four-slot `D3D12DDI_EXTENDED_FEATURES_FUNCS_0096` table
from a generator that parses the pinned WDK header. Implemented entries replace
the generated traps. Remaining void command-list functions report
`D3DDDIERR_APPLICATIONERROR` through `pfnSetCommandListErrorCb`; paired
destructors are silent because their trapped creators allocated nothing. No
baseline table has a callable NULL slot (the two WDK
queue fields explicitly named `pfnUnused*` remain NULL by contract).
Every Windows build also parses those exact pinned table definitions and the
typed installer functions, and fails if the WDK table widths change or any
usable slot is left on a generated trap. This independently guards the handoff
between table generation and the concrete frontend implementations.
The runtime handle supplied while filling the command-list table is retained
per adapter and rebound to every successfully created command list through
`pfnSetCommandListDDITableCb`, as required by the core-layer dispatch ABI.
There are concrete implementations behind all 89 baseline device slots.
`pfnRetrieveShaderComment` lazily disassembles exact native AGX executable
ranges and returns bounded, escaped, well-formed XML with program roles,
code sizes and compiler statistics; ordinary PSO creation pays no disassembly
cost. The Release 1 build-0 device table contains 91 slots because its six pipeline-
library functions replace the four baseline serialized-object functions; all
91 are populated with correctly typed entrypoints. Build `0013` adds the
92nd debug-allocation callback and replaces the two root-signature callbacks
with their 1.1 signatures; every usable slot is populated. Heaps and backed
resources report their exact runtime-owned KMT allocation and bounded GPUVA
interval, reserved resources report their virtual interval without inventing
a KMT allocation, and validated CPU-only pageable objects report neither.
The command-list
`CopyTiles` and queue `UpdateTileMappings`/`CopyTileMappings` entries translate
reserved-resource mappings to the shared renderer. Tier-1 mip packing,
null/unmap ranges, reuse-single-tile and skip ranges are bounds-checked, and
`TiledResourcesTier` is reported as one.
The size-then-create ABI also calls the renderer's shared Tier-1 reserved-
resource validator before returning private storage. Reserved buffers and
eligible 2D undefined-swizzle textures are accepted; Texture1D, Texture3D,
multisampled textures and 2D arrays whose AGX layout has a shared packed-mip
tail are rejected consistently by both `CalcPrivateHeapAndResourceSizes` and
creation. This prevents the DDI from exposing a Tier-3 volume-texture shape or
promising runtime storage for an object the shared renderer cannot construct.

## 4. Private object and resource ABI

For every DDI-created object, Windows follows a size-then-create contract:

1. the runtime calls `CalcPrivate*Size`;
2. it allocates storage of that exact size;
3. create receives an opaque `D3D12DDI_H*` whose `pDrvPrivate` points to it;
4. destroy tears down content but does not free runtime storage.

No DDI object may use COM reference counting as its authoritative lifetime.
Shared renderer payloads may remain internally reference counted where command
lists or queues hold them, but the DDI wrapper has a single runtime owner.
Every opaque renderer factory is paired with its destroy operation before it
is called. A failure carrying a partially-created object is rolled back,
success carrying no object is rejected, and the wrapper signature is written
only after construction and all object-specific validation succeed. This rule
applies uniformly to queues/fences, allocators/lists, descriptor/query heaps,
root signatures, PSOs, command signatures and resource objects, including the
queue-local DXGI blit recorder cache.

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

Descriptor tables are not subject to the former sixteen-range implementation
ceiling. The DDI validates and deep-copies as many as 1024 aggregate ranges per
root signature, and the shared renderer parses those records into one flat,
layout-owned allocation rather than embedding a fixed array in every root
parameter. Parameter records carry stable range offsets, so embedded-root-
signature comparison, pipeline-library keys and cached compute PSOs remain
pointer-free and deterministic. The command recorder resolves the same flat
records without a per-bind allocation. Both the portable cache test and the
ARM64 callback probe pin acceptance of a seventeen-range table.
Static-sampler validation and serialization use the renderer's 256-record
binding capacity as well, rather than the former 64-record frontend ceiling;
the host cache test and callback probe cover a 65-sampler signature with
stage-disjoint visibility.

The `0013` root-signature path deep-copies descriptor-range and root-descriptor
volatility promises into the same frontend-neutral representation used by
pipeline serialization and the shared renderer. The private pipeline format
is versioned, rejects conflicting data-volatility flags, and preserves those
flags across library/PSO round trips. Baseline 1.0 signatures are translated
to `DESCRIPTORS_VOLATILE | DATA_VOLATILE` ranges and `DATA_VOLATILE` root
descriptors before the shared core emits a versioned 1.1 RTS0 blob, preserving
the 1.0 execution semantics instead of silently applying 1.1 static defaults.

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
shader kind are checked, and a structurally valid SM 6.2 compute fixture is exercised by the
host suite. DXIL containers pass unchanged into the existing DXIL-to-NIR/AGX
compiler, while token streams receive the minimal signature chunks required by
the shared DXBC compiler.

Build `0013` inherits the expanded build-`0012` shader-cap and signature
contracts. `D3D12DDICAPS_TYPE_SHADER` therefore accepts both the baseline
20-byte payload and the 36-byte Release-1 payload. The latter reports the same
conservative policy as the COM frontend: no wave operations, with zero lane
counts, until the shared compiler and hardware path have a validated wave
contract. Release-1 shader signatures preserve the `_0012` geometry-output
`Stream` byte, reject streams outside 0--3, match stream-output declarations by
both stream and register, and write the stream into the generated SG1 record.
Baseline `0003` signatures deterministically translate their padding to stream
zero. This keeps the same shared DXBC wrapper for both frontends without
silently aliasing identical register numbers from different GS streams.

Heap/resource creation additionally has two private ABIs:

- runtime-private UMD storage returned by
  `CalcPrivateHeapAndResourceSizes_*`; and
- KMD allocation private data.  The latter is exactly `agxw_alloc_private`
  from `agxw_contract.h`, passed to `pfnCreateAllocationCb`; it is not a new
  IOCTL or escape channel.

GPU virtual addresses are reserved/mapped/freed through the WDDM 2.x members
of `D3DDDI_DEVICECALLBACKS`.  AGX native 16 KiB pages and the WDDM 64 KiB
reservation granularity remain separate, as already encoded and host-tested
in `agx_d3dkmt_core`.  Resource layout selection remains `ail`; WDDM supplies
allocation ownership and residency, not a new texture layout implementation.
Mesa VMA addresses are absolute in both the main and USC heaps. The submit
v3/v4 coherency read set validates low-VA BOs against the declared USC window and
forwards that absolute address unchanged; only packed AGX USC pointers subtract
`shader_base`. Host tests specifically prove that a second base addition can
remain contract-valid while naming unrelated main-heap storage.
The pre-device texture-layout query reports zero private layouts/swizzles and
answers both the legacy cap and the nondeprecated Release-2
`D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT` spelling. The latter returns the exact
`D3D12DDI_TEXTURE_LAYOUT_CAPS_0026` payload: row-major textures are supported,
while 64 KiB standard swizzle, indexable/private layouts and private swizzle
patterns remain false or zero. Release-3/4 negotiation also returns explicit
zero/false payloads for CPU page-table false positives, UMD global-real-time
queue priority, protected resource sessions and compute-only-adapter status.
This keeps optional paths disabled through their defined cap contracts instead
of depending on an `E_NOTIMPL` fallback; the graphics pipeline remains visible.
The texture-layout query advertises the precise row-major layout subset
accepted by the shared AGX
renderer: cross-adapter 2D textures with one mip, one array layer and one
sample, no depth/stencil or unordered-access use, uncompressed elements no
larger than 16 bytes, a 512-byte base alignment, and a 256-byte row-pitch
alignment. Buffers retain their required row-major layout. The DDI seam
preserves the runtime's exact row and slice pitches in the frontend descriptor,
the versioned open-resource blob and the shared renderer layout. It rejects
pitch overflow, a non-tight slice pitch and any shape or flag outside that
advertised subset instead of silently recomputing a different layout.
`CheckResourceAllocationInfo` is the only path that accepts the runtime's
`TL_UNDEFINED` request. It resolves ordinary textures to the built-in
`TL_64KB_TILE_UNDEFINED_SWIZZLE` token, sizes the actual AGX GPU-tiled layout
and returns that concrete token; later creation rejects an unresolved token or
the unadvertised standard swizzle. Allocation reflection and construction now
call the same AIL layout calculation for linear, GPU-tiled, twiddled,
depth/stencil and explicit-pitch resources, so the heap size cannot drift from
the resource subsequently placed in it. Committed heap/resource validation
also compares the reflected byte size, alignment and heap category before any
KMD allocation. Existing-resource reflection publishes its allocation handle
only after the renderer query succeeds and proves the resource range is
contained by that allocation.

Heap/resource creation is attached to the real renderer object graph. A DDI
heap is one runtime-owned `agx_bo` and one shared `d12_heap`; placed buffers and
textures use the existing `d12_resource` and `ail` layout implementation. The
renderer seam is transactional: a failed or malformed heap/open/resource
result destroys every backend object already returned, clears the runtime's
private storage, and never links a half-created heap into the device. Newly
created and opened heaps both read their allocation and KMD-resource handles
from the locked winsys identity record, so reflection, DXGI identity rotation
and destruction operate on the same dxgkrnl objects. Native open metadata
keeps the application's logical D3D12 heap size distinct from the VidMm
allocation size: the latter must equal the former rounded once to the 16 KiB
Apple page, which supports Direct3D's valid non-aligned heap sizes without
permitting padding or truncation beyond the real backing allocation. The
frontend supports descriptor heaps and SRV/UAV/RTV/DSV/CBV/sampler creation,
mapping, GPU virtual addresses, exact allocation-handle queries, subresource
layout queries and the D3D12 MakeResident/Evict callback model.
The Release 1 build-3 diagnostics callback maps those same heap/resource
identities to physical-adapter zero, KMT handles and overflow-checked,
exclusive-end GPUVA ranges. Its two-call count/capacity ABI and the ARM64
structure widths are pinned by the callback and compile-only probes.

RTV translation retains the selected mip and the complete array/W-slice range
from the DDI descriptor through the shared renderer.  A frontend-neutral,
host-tested range resolver validates ordinary array layers and the mip-reduced
depth of 3D images.  Render-segment compatibility, dimensions, AGX background
loads, PBE stores and partial clears all consume that resolved view, so legal
1D attachments and nonzero-mip 2D/3D attachments no longer collapse to level
zero.  Nonzero RTV plane slices fail closed because the currently advertised
renderable format set has no planar render-target representation.

Depth/stencil allocation and view creation use the same attachment-range seam.
The renderer now accepts legal single-sampled 1D depth/stencil resources and
1D/1D-array DSVs, and preserves 2D multisample-array DSV slice ranges instead
of rejecting the DDI's array spelling.  ZLS load/store, mip dimensions,
layered clears and submission attachment tracking remain the existing shared
AGX paths; no parallel DDI-only depth implementation was introduced.

Typed UAV clear routing also follows the descriptor's actual texture
dimension.  The shared image-fill path now clears selected mips and layer
ranges for 1D and 2D UAVs, and selected mip/W-slice ranges for 3D UAVs; 3D
view-local addresses are decoded back into the owning miptree before each
bounded slice fill.  UAV creation reuses the common attachment-range validator
and rejects unrepresented nonzero plane slices instead of silently clearing a
different plane.  Buffer UAVs retain their dedicated linear fill path.

Multisample resolve capability and command validation use one host-tested,
frontend-neutral typed-format table.  It binds all 52 supported DXGI typed and
depth/stencil plane-view spellings to a fixed AGX shader format, element size,
numeric class and legal resolve modes.  Non-integer render targets and depth
planes expose `AVERAGE`; integer formats and stencil expose only `MIN`/`MAX`,
matching Microsoft's D3D12 contract.  Resource formats must either match the
resolve format exactly or belong to its validated typeless/depth-stencil
family.  The common RGBA8, scalar-float and float4 paths retain specialized
kernels, while a precompiled native typed kernel covers 8/16/32-bit scalar and
vector UNORM, SNORM, float, uint and sint formats plus BGRA, 5:6:5, 5:5:5:1,
10:10:10:2 and R11G11B10 packed encodings.  It addresses AIL's interleaved
2x/4x sample blocks directly, works with linear or tiled destinations and
never creates a transient resource/view or performs a CPU readback.  Format
support now advertises MSAA resolve only where that command path is real.

The public capability policy also reports predication because both COM and
DDI `SetPredication` reach the shared GPU-ordered predicate implementation.
The later `HARDWARE_COPY` query is understood and returns `Supported=FALSE`;
copy queues remain native AGX command queues, but the distinct optional
hardware-copy capability is not overclaimed.

Query-heap translation includes the pinned
`D3D12DDI_QUERY_HEAP_TYPE_0032_COPY_QUEUE_TIMESTAMP` contract. Copy queues
use the same calibrated AGX timestamp writer, 64-bit heap storage and GPU
resolver as direct/compute queues, while the shared host-tested policy rejects
ordinary timestamp heaps on copy lists and copy-queue timestamp heaps on other
list families. Compile-time ABI assertions bind the public and DDI enum values
to that frontend-neutral policy rather than duplicating numeric cases. The
public COM frontend now reports copy timestamps and direct/compute/copy
`WriteBufferImmediate` through `D3D12_OPTIONS3`. The host-only Release 3
WDDM 2.3 DDI target exposes the matching
`D3D12DDI_D3D12_OPTIONS_DATA_0032` fields and
57-slot command table. Region resolve and immediate writes translate into the
same frontend-neutral renderer paths; copy timestamps use the existing shared
query policy and calibrated AGX timestamp writer.
The host-only Release 4 target carries those operations forward through the
58-slot command table and reports `Native16BitOps` directly from the same
frontend-neutral capability record used by the public COM device.

Resource-only creation implements both reserved resources and the baseline
`ReuseBufferGPUVA` capture path. For capture, the frontend validates the source
as a same-device, backed, non-reserved buffer; checks the requested resource's
actual size and alignment against both the source's logical range and its WDDM
allocation; and requires the renderer to reproduce the exact effective source
GPUVA plus offset. It creates neither a heap nor a KMD allocation. The shared
renderer retains the source BO and copies its heap description into the placed
resource, while the DDI wrapper caches the allocation identity and absolute
heap offset by value. Neither layer retains a pointer to the source resource or
the runtime's heap-private storage, so later wrapper destruction cannot leave a
dangling ownership edge. This is the WDDM capture-GPUVA model; it does not
manufacture a second mapping or a private paging operation.

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
opened by dxgkrnl. Compatible single-subresource BGRA/RGBA color surfaces also
carry Aurora's existing `agxw_standard_desc` in the allocation blob. This lets
the D3D12 frontend strictly reconstruct resources emitted by the Triton D3D11
UMD's unchanged 16-byte resource contract, and lets Triton recognize matching
D3D12 allocations, without adding a KMD ABI or private side channel. Richer
foreign metadata remains rejected instead of being guessed.

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
command segments through the callback-backed winsys. The shared D3DKMT winsys
publishes the version 3 `agxw_submit_private` record for ordinary work and the
version 4 record when a DDI placement fence is involved. Both carry the
complete canonical CPU-read range set through `pfnSubmitCommandCb`. Version 4
adds a bounded pointer-free WAIT/SIGNAL vector. No side-channel escape or
private IOCTL was added.

The callback transport implements WDDM monitored-fence synchronization and
the existing AGX timeline policy. Resolving a DDI fence placement creates a
driver-owned heap-storage record which retains the VidMm allocation, renderer
heap and coherent CPU mapping independently of runtime-owned DDI private
memory. Successful renderer fence creation takes that reference; the final
asynchronous renderer fence reference releases it. `DestroyFence` can
therefore precede retirement of an already-queued Signal or Wait, and
`DestroyHeapAndResource` defers unlock and deallocation until that work drops
the last placement reference. Queue Signal emits an ordered AGX compute write
to the retained placement. Queue Wait passes the retained GPU placements to
the paired KMD rather than reading or polling them in user mode. DXGI identity
rotation takes the same storage lock, rejects live
placement/CPU mappings transactionally, and updates both public and retained
allocation identities while locked. A portable state-machine unit test covers
retain, destroy-before-release, finalization, invalid release and overflow;
the ARM64 callback probe additionally checks deferred heap teardown.
Every driver-owned heap-storage record is also linked into a device-owned
registry independent of the runtime heap list. `DestroyDevice` first prevents
new queue/storage publication, destroys any remaining queue wrappers so their
workers join and drop queued placement references, then requests destruction
of every registered storage before releasing the shared AGX device. MapHeap
and monitored-fence retains are counted separately: the final device boundary
may abandon an unmatched application CPU map and issue its one corresponding
Unlock, but it rejects a remaining fence retain as an internal ordering error
rather than misclassifying asynchronous renderer ownership. The portable
lifetime test covers exact-count abandonment and the ARM64 callback probe
constructs a live mapped heap to verify that device teardown unlocks and
deallocates it before runtime-private device memory is cleared.
The public COM renderer queue uses separate ordered submission and retirement
workers. Its submit worker feeds successive command lists to the same AGX
timeline without waiting for each list to finish. The retirement worker waits
the associated WDDM monitored-fence point, then publishes public fence signals
and releases the exact command-list/BO snapshot retained by that submission.
Signals snapshot the current queue point instead of stalling submission, so
work following a Signal can remain in flight while cross-queue Wait still sees
the required completion ordering. A 64-submit high-water mark bounds retained
recording storage and applies backpressure only when the GPU falls that far
behind. Sparse GPU-VA updates explicitly drain the preceding point before
changing mappings; they do not rely on incidental per-submit serialization.

The DDI queue uses a different submission mode. `ExecuteCommandLists` packs
the native AGX work and calls `pfnSubmitCommandCb` on the entering runtime
thread. Fence Signal does the same with the prebuilt GPU-store command. Fence
Wait records the runtime's CurrentValue and MonitoredValue placements without
polling or blocking the caller. The next submit carries those WAIT records in
the v4 private blob. A full vector is materialized as a tiny dependency packet
before accepting more waits, so the per-packet ABI bound does not become a
per-queue semantic limit. AppleAgxWddm accepts an unsatisfied wait, retains the
bounded blob in the WDDM context's FIFO, and publishes its DMA only after the
placement reaches the requested value. A PASSIVE completion pump handles GPU
signals and a 2-ms work-only fallback detects CPU signals. Retirement remains
asynchronous in the UMD. This preserves legal wait-before-future-signal chains
and meets the same-thread submit callback rule without changing the COM path.

DDI tiled-resource updates use the WDDM interlock defined for
`D3DKMTUpdateGpuVirtualAddress`, not the renderer's synchronous DRM-shaped
`VM_BIND` compatibility hook. Pending placement waits are first materialized
on the entering queue thread. The update then names that queue's real render
context and internal monitored fence at point N; VidMm runs the dedicated
paging context after N and signals N+1. `DoNotWait` is deliberately clear;
setting it would discard the required wait on N. The UMD reserves N+1 in the
same queue timeline, so a later AGX submit waits in Dxgkrnl while the
tile-mapping DDI returns without a CPU fence wait. Update and copy mapping
calls therefore remain ordered with Wait, Execute and Signal without a side
channel or a second private queue.

Submit, retirement, sparse-bind, timeline and CPU-fence failures publish one
atomic device-removal transition in the shared renderer. The public COM
frontend retains the first exact DXGI reason for `GetDeviceRemovedReason`;
the WDDM frontend registers a thin notifier before any queue worker exists
and forwards that transition exactly once through `pfnSetErrorCb` as
`D3DDDIERR_DEVICEREMOVED`. A later queue fault cannot overwrite the first
cause or issue duplicate runtime removal callbacks, so Windows cannot wait
forever on a fence whose underlying submit already failed.
Each queue also prebuilds a 64-slot native AGX placement-fence signal arena:
the control stream and USC binding words are generated once, Signal patches
only one 16-byte address/value argument and uses an inline submit payload, and
the slot returns to the bitmap only after its timeline point retires. This
removes two WDDM BO allocations, two maps and repeated command encoding from
every DDI fence Signal while preserving a real ordered GPU write.
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
the runtime callback returns `E_PENDING`. WDDM 2.1 uses
`pfnOfferAllocations2Cb`/`pfnReclaimAllocations3Cb`, preserves AllowDecommit,
maps enum reclaim results back to original pageable objects and returns the
real paging fence and wait mask. Tier-1 sparse mappings use the same paging
queue/GPUVA ownership instead of a private memory path.

Queue and fence construction is transactional across the renderer seam. The
DDI requires paired create/destroy operations before calling the renderer,
destroys an object returned alongside a failure, rejects success without an
object, balances every retained fence-placement mapping, and publishes the
runtime signature/list linkage only after all validation succeeds.
The existing direct-D3DKMT winsys remains valid for the public COM DLL only.

### Performance and WDDM memory-model invariants

- No D3D12-to-D3D11, DXVK or command-stream translation layer exists.
- The runtime owns the adapter/device. The renderer calls its callback table
  directly; there is no hidden `D3DKMTCreateDevice` in the DDI path.
- Allocations, residency, paging fences and GPU-VA updates remain explicit
  WDDM objects. UMA does not mean “always resident” or justify bypassing VidMm.
- Submit coherency metadata uses each BO's exact mapped GPU VA. LOW_VA chooses
  the USC heap but never rebases the address; portable tests cover both heap
  bounds and the otherwise-silent double-base failure case.
- A frontend-provided `agx_submit_virt` read vector bypasses the winsys's
  O(total live allocations) conservative scan only when explicitly marked
  complete. Placement-fence Signal uses the bounded fast path for its
  control/argument arena, low-VA USC arena and immutable helper-program BO;
  the winsys adds its own staging BO. General command lists keep the fallback
  until descriptor, resource, executable and indirect reads are all tracked.
  That fallback's already-canonical vector is cached by a 64-bit visibility
  generation, so steady-state command-list submits perform a bounded copy
  instead of repeatedly walking the BO map. BO activation/retirement, CPU
  map/unmap, successful GPU-VA updates and DXGI identity rotation advance the
  generation. Cache publication requires identical generations before and
  after the locked scan; continuous resource churn retains the original
  uncached scan result rather than publishing a possibly stale entry. The
  fixed zero/scratch/printf allocations are included at their renderer ABI
  addresses even though their process-root mappings intentionally have no
  ordinary per-wrapper `agx_va` owner.
- Context HANDLEs are stored at native pointer width in a bounded bridge table;
  renderer-side 32-bit IDs are tokens and never truncated HANDLE values.
- Hot submission callbacks do fixed-field translation only. Heap allocation is
  absent from submit. Residency batches of up to 16 runtime objects use fixed
  stack storage; larger `MakeResident`, `Evict`, `OfferResources`, and
  `ReclaimResources` batches use one bounded spill allocation. A fixed-size or
  spill-local open-addressed table coalesces pageable objects that share one
  VidMm allocation before invoking the runtime, while the per-object index map
  fans a reclaim result back out to every aliased heap/resource. The remaining
  variable-length GPU-VA update operation array is outside draw/dispatch
  submit.
- Normal queue submission is asynchronous with respect to GPU completion.
  Timeline points provide in-order AGX dependencies, while a dedicated retire
  path owns command storage through completion and asynchronously publishes
  D3D12 fence values. Deterministic host tests cover monotonic submit points,
  same-point Signal markers, ordered retirement, overflow rejection and the
  bounded in-flight policy.
- Placement-fence Signal performs no BO allocation or shader/control-stream
  construction after queue creation. Its fixed 16 KiB stream and USC arenas
  use 64 independently retired slots; host tests exhaust, release and reuse
  the bitmap and prove every control/data/USC range remains in bounds.
- Version/capability exposure is correctness-gated. Performance claims require
  ETW/GPUView traces, residency-pressure tests and application benchmarks on
  the Windows target.

## 6. DXGI presentation

Presentation has two connected DDI surfaces:

- the core table reports present-private-data size and the command-list table
  implements `pfnPresent`, producing `D3D12DDI_PRESENT_0003`; and
- `FillDDITable(D3D12DDI_TABLE_TYPE_DXGI, ...)` fills the exact 21-slot
  WDDM 2.0 `DXGI1_4_DDI_BASE_FUNCTIONS` or 22-slot WDDM 2.1
  `DXGI1_5_DDI_BASE_FUNCTIONS` surface. Its
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
batched WDDM callbacks. Query, offer and reclaim coalesce repeated resources
and distinct UMD resources sharing one VidMm allocation, then fan the one
kernel residency/reclaim result back to every DXGI resource in the original
order. Residency aggregation returns the exact legacy Direct3D success status
(`S_OK`, `S_RESIDENT_IN_SHARED_MEMORY`, or `S_NOT_RESIDENT`) required by the
DXGI DDI while also filling every per-resource status. DXGI
Offer/Reclaim validates each driver resource and translates it to its VidMm
allocation. WDDM 2.0 uses the legacy allocation callbacks. WDDM 2.1
`OfferResources1`/`ReclaimResources1` use Offer2/Reclaim3, preserve the
decommit flag and the full reclaim-result enum. Because the DXGI 1.5 reclaim
DDI has no paging-fence output, the frontend consumes the Reclaim3 fence before
returning: the winsys first checks the CPU-visible paging-fence mapping and
uses the existing kernel fence wait only when needed. This is a cold reclaim
path and never spins or enters draw/submit hot paths. A null DXGI results array
uses bounded internal scratch storage while satisfying Reclaim3's required
output-array ABI.
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
A single frontend-neutral display-format table drives resource capability
advertising, DXGI blit validation and the native recorder's shader-format and
element-width selection. It includes both RGBA8 and BGRA8 sRGB encodings, and
the AGX kernel decodes or encodes gamma only when the corresponding source or
destination view is explicitly sRGB; ordinary UNORM-to-float conversion stays
linear.
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

WDDM 2.1 present batching adds `PFND3DDDI_SYNCTOKEN` entrypoints to the
D3D10/11 device table and `PFND3DDDI_SYNCTOKENCB` callbacks to
`D3DDDI_DEVICECALLBACKS`. The pinned `d3d12umddi.h` core and DXGI tables do
not expose D3D12 Acquire/ReleaseResource entrypoints, so the D3D12 adapter
must not manufacture them or reinterpret a D3D10/11 table. The callback
layout and 24-byte `D3DDDICB_SYNCTOKEN` width are compile-time asserted for
the WDDM 2.1 build. Functional sync-token forwarding in the separately owned
legacy Triton D3D11 frontend remains a package-level compatibility gate; it
does not alter the native D3D12 ABI or command ordering.

## 7. Compileable delivery gates

| gate | success criterion | state |
|---|---|---|
| A. adapter | ARM64 DLL loads, `OpenAdapter12` validates Apple identity, returns exact adapter table/version/caps | implemented, including conservative texture-layout discovery, and statically/host tested |
| B. device/core tables | `CreateDevice` succeeds only with complete versioned device, command-list and queue tables; void-call failures use the correctly scoped runtime error callback | Exact `0003`, Release 1, Release 3, Release 4 and Release 8 `0099` shapes are implemented and statically/host tested. Release 8 fills all 122 device and 73 command-list slots plus extended-feature table 27; every changed prefix callback has its pinned PFN type. Runtime pools/recorders map onto the shared AGX allocator/list core without steady-state object churn. The ARM64 Windows callback probe is compiled but was not executed in this host-only pass. |
| C. heaps/resources | allocation-size checks, committed/placed/reserved/capture-GPUVA resources, mapping and descriptor heaps use runtime/KMD callbacks and `agxw_alloc_private` | implemented for native buffers/textures, descriptor/view creation, exact aligned `ReuseBufferGPUVA` placement without a new KMD allocation, explicit-pitch cross-adapter row-major textures, GPUVA, map, private/standard foreign-open ABI and allocation/subresource queries; Tier-1 reserved-resource sizing and creation share the renderer's exact shape/packed-tail validator, and unsupported foreign metadata fails closed |
| D. PSO/command lists | DDI descriptors translate to the existing root-signature, DXIL, PSO and command recorder core; draw/dispatch/copy tests execute | graphics/compute PSO, deterministic serialization/deserialization, root-signature 1.0/1.1 with exact volatility semantics, shaders including Release-1 stream-aware GS-with-stream-output and passthrough SO, draw/dispatch, bundles, state, barriers, copies, clears, discard hints, resolve, identity blits, queries/predication, stream-output targets and proven ExecuteIndirect forms translated to shared renderer; hardware DDI validation pending |
| E. queues/fences | virtual contexts, submit, monitored fences, GPU/CPU signal/wait | renderer queue binding, pipelined COM aggregation, separate retirement, bounded in-flight storage and placement-fence signaling are host-build tested. The DDI calls SubmitCommand on the entering thread and uses the paired AppleAgxWddm v4 placement-wait vector for nonblocking wait-before-signal ordering. Wire packing, KMD validation/FIFO pumping and the caller-thread source path have host gates; Windows runtime/hardware validation remains pending. |
| F. residency/paging | paging queue, make-resident/evict/offer/reclaim and device-removed paths | paging queue/GPUVA, allocation-deduplicated mixed pageable-object core residency, DXGI batched Offer/Reclaim and generation/budget-bounded renderer cache trim implemented and ABI-probed, including aliased discard fan-out, optional discard output and CPU-only core objects; asynchronous renderer failures now atomically retain the first cause and notify the D3D12 runtime exactly once, while real device-removal and pressure validation remain pending |
| G. presentation | DXGI table, back-buffer present, windowed and fullscreen swap-chain probes | exact WDDM 2.0 21-slot DXGI 1.4 and WDDM 2.1 22-slot DXGI 1.5 tables, core Present, single- and multi-surface Present1 translation, flip callback, priority, residency, synchronous DXGI Offer/Reclaim, kernel-identity rotation with descriptor repatching, direct-queue exact copy/resolve, and one-pass native AGX stretch/rotation/format-conversion blits implemented; MPO and mode-setting fail closed; real swap-chain hardware validation pending |
| H. revision/feature growth | advertise later DDI revisions and FL/SM/options only after version-specific tables and conformance tests pass | WDDM 3.1 exposes build `0099` with exact resource 0088/open 0043, queue 0050, Present 0051, sampler 0096, state 0095/0099 and modern caps. Unsupported cap-gated features have non-NULL, exactly typed fail-closed entries and false/tier-zero caps. Older WDDM 2.0/2.1/2.3/2.4 targets remain compileable. Windows runtime/hardware conformance remains pending. |
| I. packaging | register native ARM64 D3D12 slot alongside D3D11, then make it default after hardware probe | the isolated WDDM 2.1 worktree atomically copies/registers the self-contained native ARM64 DLL at D3D12 index 3, requires it during packaging/catalog generation, and enforces DriverStore paths, `FeatureScore = D1`, and exact `21.17.240.0` INF/SYS/UMD PE versions; source/package gates pass, but no current package was signed, cataloged, installed or clean-loaded in this host-only pass |

### Recorded gates before a production claim

- No hardware command has been sent from this branch while the shared Windows
  target is under concurrent bring-up. `d12_umd_runtime_probe.exe` is the first
  authorized runtime/GPU validation and must be run only by the hardware owner.
- Merge the AppleAgxWddm `codex/d3d12-fence-dependency` patch before packaging
  this UMD. Contract v3 publishes `submit_private_max_version = 4` in the
  fixed-width adapter identity, so pairing this UMD with a
  submit-private-v3-only KMD fails closed during `OpenAdapter12`, before
  device creation or submission.
- `0003` remains the WDDM 2.0-compatible hardware baseline. Release 1
  `0010`/`0011`/`0013` now has matching UMD tables/caps and an isolated WDDM 2.1 KMD
  source/package target, but that pair still requires Windows runtime and
  hardware validation. A higher WDDM number by itself does not prove the
  mandatory KMD contracts.
- Release 3 `0032` is a host-only forward UMD target. It must not be installed
  with the WDDM 2.1 KMD package; the latter intentionally has no `0x8001`
  interface claim or WDDM 2.3 hardware-queue callback uplift.
- Release 4 `0042` is likewise host-only. Its `0x9001` callback layout is
  compiled and statically pinned through WDDM 2.4.2, but no KMD/INF/package
  claim was added and no Windows or GPU execution was performed.
- Release 8 `0099` is the current host-complete target. Its `0x10000` WDDM
  3.1 callback layout, 122/73 tables, extended-feature table and PE are pinned,
  but the hardware-verified KMD branch still needs a deliberate WDDM 3.1
  interface/INF uplift before this DLL may be installed as that target.
- Tier-1 tiled/sparse resources are implemented across reserved resources,
  mip packing, `UpdateTileMappings`, `CopyTileMappings` and `CopyTiles` using
  the shared GPUVA/paging backend. Baseline `ExecuteIndirect` argument forms
  include draw, indexed draw, dispatch, root constants/views and vertex/index
  buffer views; tessellation-indirect remains restricted. Compatible Triton
  D3D11 standard color surfaces now interoperate through the pinned allocation
  descriptor, while arbitrary foreign metadata remains rejected. Baseline `0003` PSO
  object serialization, Release 1 indexed pipeline libraries and native AGX
  shader-disassembly XML are implemented. Persistent library blobs currently
  warm the existing compiler/cache on first load instead of embedding portable
  compiled AGX code; eliminating that first-load latency remains an
  optimization gate.
- Real DXGI windowed/fullscreen presentation, residency pressure, TDR/device
  removal, ETW/GPUView traces and application benchmarks remain mandatory
  before performance or production-readiness claims.
- The paired KMD change lives on isolated branch
  `codex/d3d12-fence-dependency`, based on a source snapshot of the current
  hardware-verified AppleAgxWddm tree. Its version-4 SubmitCommandVirtual
  extension and D3D12 UMD must merge together; the ordinary Honeykrisp path
  remains version 3. No hardware execution was performed in this host-side
  milestone.
- The legacy Triton wrapper currently builds and its PE version validates, but
  its complete aggregate build is blocked locally by the absent external
  renamed-DXVK `BUILD-MANIFEST.txt`. This is a source-input/package assembly
  prerequisite, not a native D3D12 compile failure.

## 8. Reproduction

From the Mesa worktree root:

```sh
export AGX_WDDM_CONTRACT_CORE=../AuroraSilicon-d3d12-fence-dependency/drivers/AppleAgxWddm/core
src/asahi/d3d12/umd/run-host-tests.sh
for target in 20 21 23 24 31; do
  D12_UMD_WDDM_TARGET=$target \
    D12_UMD_BUILD_OUT=_build-d3d12-umd-arm64-wddm$target \
    src/asahi/d3d12/umd/build-windows-arm64.sh
done
D12_UMD_WDDM_TARGET=21 \
  D12_UMD_MESA_BUILD=_build-d3d12-umd-mesa-arm64-shared-wddm21 \
  src/asahi/d3d12/umd/build-mesa-windows-arm64.sh
D12_UMD_WDDM_TARGET=31 \
  D12_UMD_MESA_BUILD=_build-d3d12-umd-mesa-arm64-shared-wddm31 \
  src/asahi/d3d12/umd/build-mesa-windows-arm64.sh
```

When `AGX_WDDM_CONTRACT_CORE` is omitted, these scripts use a compatible main
Aurora tree first and then the paired isolated KMD worktree. They reject a
pre-v3 or submit-private-v3-only header before compiling.

The standalone UMD and Aurora-owned source use `-Wall -Wextra -Werror` plus
conversion, shadow, prototype, undef and pedantic diagnostics. The full Mesa
renderer artifact is a release/NDEBUG build while retaining those fatal flags
on the UMD adapter targets; unrelated upstream Mesa utilities remain at the
project warning policy. The WDK staging directory is a system include path
because Microsoft's published C headers intentionally use MSVC extensions.
`check_pe.py` audits PE machine type, exact exports and every imported DLL,
rejecting non-system dependencies such as MinGW `libwinpthread-1.dll`.
It also rejects a missing or mismatched fixed/string file version and original
filename, closing the WDDM 2.1 INF/SYS/UMD version-consistency requirement at
build time rather than deferring it to HLK.
The isolated Aurora package builder repeats the ARM64/exact-export/system-only
import audit on the supplied UMD before signing, and the signed-package
verifier repeats it on the exact cataloged bytes. A developer-only MinGW
runtime sidecar therefore cannot re-enter through package substitution.

`tools/run-d12-ddiprobe-qemu.sh` is a networkless ARM64 WinPE harness. It
first clean-loads and unloads the full renderer DLL, then loads the
callback-only DLL and executes the ABI probe. This catches constructor and
dependency failures in an environment without build-runner DLLs before any
DDI call. It performs no driver installation, adapter open or GPU access;
among other table checks it proves the exact identity-rotation direction,
fixed runtime handles, mapped-resource rejection, presentation-blit flag
validation and empty-list semantics on Windows ARM64. Load failures include
the numeric `GetLastError` value and `FormatMessageW` text.
The harness was not executed in this workspace because its pinned custom QEMU,
firmware and WinPE inputs are absent. The probe binaries compile for ARM64;
static PE auditing is the clean-environment dependency evidence available in
this host-only pass.

The standalone build also emits Windows ARM64 loader, KMD-callback, queue,
runtime-transport and installed-D3D12-runtime probes. The transport probe covers allocation, residency,
paging, GPU-VA map/update, full-width context-token mapping, submit, monitored
fence and escape fields. The full build produces both frontend DLLs from the
same renderer archive and audits `d3d12agx_umd.dll` for the exact
`OpenAdapter12` export set and a Windows-system-only import closure. This
closes the earlier `libwinpthread-1.dll` packaging failure without adding an
unsigned sidecar. A future installable package must embed-sign the final DLL,
regenerate/sign its catalog and pass the WinPE clean-load probe; none of those
state-changing or Windows-execution steps was performed here.

`packaging/stage_package.py` adds the full-renderer DLL to a throwaway
AppleAgxWddm package stage, patches only D3D12 registration index 3, updates
the CDF and removes the stale catalog. Regenerate and sign the catalog before
installing. After installation run `d12_umd_hwprobe.exe` first, followed by
`d12_umd_runtime_probe.exe`; the latter enters through the Microsoft runtime
and verifies upload -> default -> readback GPU transit with a bounded fence
timeout.
