# Isolated Neo compiler experiment

Compiler sources imported from aurora-silicon/mesa agx3
259af84735247b697cd4148ccdcb1f43fdd5ad5f. Original copyrights retained.
This is an opt-in experiment, not a replacement for the qualified compiler.
The compiler is rebuilt against this checkout's common NIR definitions; its
own AGX IR and generated encoders are isolated by symbol namespacing. A field
adapter preserves the M3 frontend/launch contract and rejects unsupported
resource requirements. No kernel or device identity is spoofed.

Texture and ray-query frontend lowering stays in the M3 compiler library:
Neo's versions consume a different resource descriptor ABI. The imported
backend's prepared NIR entry is used, not its full Vulkan frontend.

The driver-facing Ioto/graphics/compute program constructors are also excluded:
M3 retains its original launch builders. The symbol rewriting step materializes
Meson's thin archive before renaming definitions, leaving shared NIR APIs alone.

2026-09-30 checkpoint: native build and offline G15S DRM-shim replay of the
3024x1837 Minecraft GL capture completed, compiling all fragment pipelines with
`HK_M3_NEO_FRAGMENT_COMPILER=true` and `ZINK_DISABLE_SAMPLER_MINMAX=true`.
No GPU execution or performance result is established by that shim run.

M3 integration correction: retain the qualified compiler's masked-shift rewrite
restriction in agx_nir_algebraic.py. A dynamic NIR shift wraps its count whereas
ubitfield_extract cannot replace that expression for arbitrary counts. Neo's
unrestricted fusion broke Zink sparse-residency checks and made ordinary UBO
reads return zero. Restricting fusion to literal in-bounds extracts fixes the
CPU-checked sampler/UBO readback (16 draws, ordinary and custom-border samplers).
The original imported compiler failed that same probe; constant colour passed.
No cache-hint or scoreboard diagnostic changes are needed for this correction.

Hardware replay result (2026-09-30): all three retained Minecraft gameplay
images are pixel-identical to the original backend. Same-binary alternating
30-frame timings at 3024x1837: original 22.3887/22.5901/23.7507 FPS, Neo fragment
23.1442/22.1272/22.1677 FPS. Medians 22.5901 versus 22.1677 (-1.87%); sample ranges
overlap, so this demonstrates no performance win. Both arms disable Zink sampler
min/max. This is an isolated fragment-backend experiment, not full Neo Mesa,
application startup testing or broad compiler conformance.

A focused GL wrapped-shift shader (explicit count mask to keep GLSL defined)
also fails with the imported optimizer and passes with the correction. Evidence
and retained tooling are in gpu-replay-bench/diagnostics/neo-hybrid-m3-20260930.
