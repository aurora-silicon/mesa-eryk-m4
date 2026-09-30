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
`20260930T030645822727Z`. Final package timings and desktop deployment are recorded below.

The naive vertex-loader implementation spent too much CPU time constructing
Ioto resource programs. Reusing their structure while patching each draw's GPU
addresses removes that cost; the template path retains bounds and allocation
checks. Shader input loading and CPU template reuse are separate optimizations.

## Final default package and deployment

Clean shared Mesa `f7b0d4dce0a`, full package
`/opt/asahi-m3m4-f7b0d4dce0a`, Vulkan SHA256
`08e1c6978ebeba79750566ab16797a2eb31e1d32ac4a58974a2786e1a4de3b2b`.
With no shader-input overrides, three alternating same-binary Minecraft
Vulkan runs give **97.2551 FPS median versus 54.7870 disabled (+77.51%)**.
All three saved frames are exact in both configurations. M1's prior diagnostic
median is 93.5593; it was not rerun and retains its original thermal-evidence
limitation. These are completed 30-frame replay timings, not live game FPS.

The separate Minecraft OpenGL/Zink capture also passes exactly. Default samples
are 25.5695, 24.4311, 24.4240 FPS; the vertex/template-disabled control samples
are 23.6930, 25.3488, 24.1196. The respective medians are 24.4311 and 24.1196.
This does not establish a substantial GL performance gain. All samples are
retained, not filtered. The original storage-image CTS regression also passes
on the final package with default options.

Persistent and runtime M3 graphics environments select this prefix. A guarded
stop of the login manager and old user session followed by normal autologin
completed in 4.5 seconds, with KWin mapping the new Vulkan library and Plasma
running. Deployment backups, journal, mapped libraries and telemetry are in
`build/minecraft-auto-prefetch/deployment/m3-vertex-production-deployment*`.
The 1,069-case selection and summary are retained alongside this document
under `tests/vertex-inputs-20260930/`; the published benchmark selection pins
`m3-complete-inputs-paired` and `m3-complete-inputs-gl-20260930`.
