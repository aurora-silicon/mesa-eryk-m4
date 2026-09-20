# G15S / G16G shader gate audit (2026-09-20)

Scope: `src/asahi/{compiler,lib,vulkan}` chip/generation/SoC-ID conditionals,
excluding test vectors. Base M4 Mesa: 6a30d044f34. Target: J514S, G15S B1.
The core compiler's G16G switch arms already include G15S and select AGX3b.
Device admission, command layouts and firmware are separate axes.

## Shared paths enabled

`agx_chip_uses_g15_g16_shader_abi` selects G15S and G16G without spoofing
hardware identity or changing the G17P path. It covers:

- ten-slot IOTO helper tables and native fragment sample loops;
- sample-loop preservation, per-sample barycentrics and Vulkan sample shading;
- robust image/texel-fetch bounds and out-of-bounds alpha;
- native texture offsets and shadow-gather offset lowering;
- shader sampler reduction and image-view minimum LOD, including matching
  sampler state, feature/extension advertisement and pipeline cache keys;
- native cube descriptors with matching shader coordinate lowering;
- default layer-zero vertex export and native output selectors (required even
  for single-layer render targets when a fragment shader reads the layer);
- native Gen9 spilled render-target shader lowering.

## Gates retained and why

These are implementation or qualification boundaries, not assertions that
M3 lacks an instruction. They must move with their dependencies:

| Files / gate family | Dependency before sharing |
| --- | --- |
| agx_device, agx_bo, agx_device_virtio, hk_cmd_pool | Executable BO permissions, shader VA windows and pooled program publication must match the M3 kernel VM contract. Exact chip-ID admission remains intentional. |
| hk_cmd_dispatch, hk_cmd_buffer, hk_queue | Strong visibility barriers, batching, deferred flushes and early tiling change cache/order guarantees; M3 currently retains serial firmware retirement and attachment flushes. |
| hk_query_pool, hk_cmd_meta | Firmware timestamp copies and profiling fields are different; M3 currently implements native compute timestamps, not M4 render timestamp packets. |
| hk_device, hk_buffer, hk_queue, hk_nir_lower_sparse_access, hk_shader | Sparse residency and canonical aliases need allocated CPU-maintained tables, queue wait ordering and kernel bindings. Removing only the shader gate dereferences an absent alias directory. |
| hk_shader, hk_device, hk_physical_device | Emulated atomic64 requires the lock-table BO and root binding. Fragment/vertex storage also requires render-to-compute cache publication. |
| hk_shader, hk_physical_device | Geometry, tessellation, multiview and layer/viewport feature exposure require complete submission support. The M3 kernel currently accepts one layer. Supplying default layer zero does not enable layered rendering. |
| hk_image, hk_image_view, hk_buffer_view | R64 storage formats advertise atomic capability and depend on atomic64 lowering. Multisampled depth/integer/storage formats need separate coverage beyond color sample shading. |
| hk_image, hk_cmd_meta | Compressed textures, compression feedback, 3D slice copies and specialized upload batching need format/metadata and execution qualification. |
| hk_cmd_draw, hk_cs_root_size | Tile occupancy limits, render-root splitting and profiling/barrier diagnostics govern command execution, not ISA selection. |
| hk_shader, hk_cmd_buffer | Optional preamble and uniform/descriptor prefetch variants need corresponding IOTO upload and resource-lifetime coverage. |
| agx_bg_eot | Native signed-format export selectors come from chip-specific captures; similar arithmetic ISA alone does not establish identical EOT packing. |
| hk_agx3_tile_core and standalone codec targets | Offline whitelist coverage is distinct from the production packer's existing G15S support; unused codec APIs do not affect Firefox. |

Shared IOTO/VDM/PPP codec switch arms were also inspected: most already admit
G15S alongside G16G. Recognized enum values and identity-to-encoding tables
are not exclusion checks and must retain separate chip names.

## Hardware results

Kernel 37158febe86f, Mesa build including these changes, normal eryk Plasma
Wayland on DCP, 60-second watchdog and five PMP channels sampled every 0.5 s.
Workspace evidence: `_provenance/m3-firefox-20260920`.

- shared-samples02: 2/2 Pass, minimum sample shading 1.0 at 2x and 4x.
  The initial shared-samples01 failed before submission because M3 omitted
  default layer-zero vertex export; the final changes fix that dependency.
- shared-texture01: cube seamless filtering, image-view min LOD and compute
  robust sampled-image access Pass. The fragment robustness case is explicitly
  NotSupported because fragment stores remain disabled; it is not a pass.
- shared-texture02: cube projected derivatives (minimum sampler reduction)
  and integer-texel-coordinate min LOD Pass. The exclusive-compute cube case
  is NotSupported because this kernel exposes one combined queue.
- color16-cts01: RGBA32 float render-to-image Pass.
- firefox05: real Firefox remains running, maps the custom Zink/Vulkan driver,
  and visibly renders its browser UI/private page in a desktop screenshot.

These are focused regressions, not conformance or full M4 feature parity.

## CTS repair checkpoint

The original 42 failing random cases were rerun on 2026-09-20. Native scratch
allocation, single-slice 3D blits, native 1D blits and matching subgroup stage
capabilities produce 24 Pass, 4 NotSupported and 14 comparison failures when
combined with kernel MRT admission. No device loss or process crash remains
in this subset. Evidence: build/m3-cts-fixes-02 in the M4 workspace and the M3
kernel lab validation/cts-fixes-20260920 checkpoint. Image correctness remains
under investigation.

## M3 sampler bias and complete sample rerun (2026-09-20)

M3 uses generation-9 texture descriptors with generation-4 samplers. Unlike
M4 HAL200 samplers, these require software API mip LOD bias. Select the
hardware-bias shortcut by sampler generation rather than texture descriptor
generation. Internal reduction footprint queries and centre taps already
account for this bias; exclude them from generic bias lowering to avoid
applying it twice or producing an unsupported query shape.

The original EAC negative-bias sampler failure now passes. An additional
32-case positive/negative-bias sweep completed with 16 Pass and 16
NotSupported. No unsupported case was replaced. The unchanged original
1,000-case sample now reports 386 Pass, 601 NotSupported and 13 Fail, with
zero device loss, process crash or timeout. All original passing cases still
pass. Four unsupported tessellation cases now report NotSupported instead of
asserting. Peak package temperature was 64.75 C across 16 guarded segments.

Evidence: workspace build/m3-cts-random-1000-fixed-01 and
build/m3-bias-validation-01; frozen cases SHA256
cc3f5792d040ee05fb4a280c518eae9276b03db979e2ac2a159fa27358f8c8bf.
Remaining failures concern scaled-format CTS reference/shader typing and
line-depth coverage comparisons; they are retained as Fail, not waived or
counted as passes. The line controls pass 12 supported non-strict/Bresenham,
wide and 4x-MSAA cases (24 unsupported). Experimental line-width and MSAA
changes were discarded. This is bring-up validation, not conformance.
