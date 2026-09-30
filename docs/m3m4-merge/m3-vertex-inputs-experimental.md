# Experimental M3 vertex input loading

Not enabled by default. `HK_M3_VERTEX_PREFETCH_UBOS=true` builds a third
hardware vertex variant for immutable ordinary UBOs. Software geometry/tess
paths retain alias lowering, and recording selects the general variant for
sparse/shadow buffers. Uniform metadata and NIR inputs move together past the
16-half vertex ABI prefix; the admission budget reserves that space.

Host descriptor/bounds tests and ARM64 builds pass. Minecraft native Vulkan
replays complete but fail the existing image comparison: approximately 1500
pixels differ, of which 20 exceed one RGB level, maximum 77-79. Adding compiler
waits has no effect. A diagnostic that marks all vertex ALU exact worsens the
comparison and is not included. No failed run is a qualified score.

The naive path is CPU-bound building Ioto resource programs. Existing
`HK_M4_IOTO_TEMPLATES=true` brings ordinary frame intervals from ~21ms to
9-10ms, but these screenshot runs are not benchmark timings. Native counters
covering initialization plus replay show tiling span ~107ms versus ~181ms
for the fragment-only candidate; these are not isolated per-frame timings.

Remaining work: diagnose the vertex arithmetic/edge differences, qualify
variant switching and sparse/dynamic/robust paths with CTS, measure warmed
completed replays, then consider defaults. Evidence retained under
`/home/council/dev/gpu-replay-bench/diagnostics/m3-vertex-*` and
`/home/council/dev/dirtyroom_m4/build/minecraft-auto-prefetch`.

## Pass-order diagnosis

Forcing the general vertex variant in the original experiment produces exactly
the same changed pixels as direct UBO loading. Thus the discrepancy is not
caused by the loaded values. Skipping the second full preprocessing pass for
vertices restores exact first/middle/last Minecraft images while retaining
9–10 ms ordinary frame intervals. The early full pass must remain: a reduced
scalar/constants-only early pass rejects a dynamically indexed storage-image
vertex shader in CTS. That test passes again with full early preprocessing
and no redundant late preprocessing. An expanded 1,069-case indexed CTS
selection is now being qualified; this path remains opt-in.

Evidence: `cts-vertex-image-{control,cse,early-only}` and
`vertex-early-only-stage` under `build/minecraft-auto-prefetch`; saved Minecraft
check `20260930T030645822727Z`, exact against the original reference.
