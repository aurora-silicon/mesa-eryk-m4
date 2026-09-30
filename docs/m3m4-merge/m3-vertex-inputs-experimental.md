# M3 vertex uniform input loading

G15S uses a third hardware vertex variant for immutable ordinary UBOs.
Software geometry/tessellation paths retain alias lowering; recording selects
the general variant for sparse/shadow buffers and mutable bindings. Uniform
metadata and NIR inputs move together past the 16-half vertex ABI prefix;
admission reserves this space and falls back when the budget is exhausted.
GPU loads read live uniform values at execution. CPU recording examines only
immutable descriptor metadata, not buffer contents.

The default policy enables this path and resource-program templates on G15S;
`HK_M3_VERTEX_PREFETCH_UBOS=false` and `HK_M4_IOTO_TEMPLATES=false` remain
available as independent diagnostic opt-outs. Other chips retain their defaults.
The vertex policy participates in the compiler/pipeline cache key.

## Correctness and pass ordering

The first experiment changed about 1,500 pixels, 20 beyond one RGB level.
Forcing its general vertex variant produced exactly the same changed pixels
as direct loading, isolating the problem from the loaded uniform values.
A redundant second full preprocessing pass after UBO lowering changed the
arithmetic expression shapes. Removing that late pass restores exact saved
Minecraft Vulkan frames while retaining the fast loads. The normal backend
optimization still handles newly introduced address/select operations.

Keep the early full preprocessing pass: replacing it with scalar/constants-only
cleanup restored Minecraft images but rejected a dynamically indexed storage-
image vertex shader. The early-only full-preprocessing candidate passes that
case and the original Minecraft reference. No image threshold was relaxed.

The corrected candidate `/opt/asahi-m3-vertex-early-only-9c0bf9e93d3e` completes
1,069 indexed CTS cases: **716 Pass, 353 NotSupported, no failures or guard
errors**. Coverage includes vertex UBO, geometry, tessellation, vertex inputs,
transform feedback, shader objects, robust UBOs and descriptor fallback cases.
Catalog SHA256: `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Saved game checks also pass for Minecraft OpenGL/Zink, Satisfactory Vulkan,
Factorio Wayland, 0 A.D. Vulkan and SuperTuxKart Zink at their existing image
tolerances. Host descriptor/prefix-budget and Ioto-program tests pass. This is targeted
regression coverage, not a full conformance run. M4 was not exercised.

Evidence is in `build/minecraft-auto-prefetch/{cts-vertex-selection,
cts-vertex-early-only,cts-vertex-image-*,vertex-early-only-stage}` and the
benchmark repository's `diagnostics/m3-vertex-*`. Exact Minecraft check:
`20260930T030645822727Z`. Final package timings and desktop deployment will be
recorded separately after validation with default options.

The naive vertex-loader implementation spent too much CPU time constructing
Ioto resource programs. Reusing their structure while patching each draw's GPU
addresses removes that cost; the template path retains bounds and allocation
checks. Shader input loading and CPU template reuse are separate optimizations.
