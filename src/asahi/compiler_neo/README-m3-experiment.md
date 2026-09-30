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
