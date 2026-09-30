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
