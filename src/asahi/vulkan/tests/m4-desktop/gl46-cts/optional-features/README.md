# Optional OpenGL features on M4 (2026-09-22)

## Shadow LOD

Zink now exposes the existing SPIR-V depth-reference sampling support for
array/cube explicit LOD and bias (`GL_EXT_texture_shadow_lod`). All 18 targeted
GL46 cases pass: 15 rendering/reference-comparison cases plus three negative
shader tests. The rendering cases exercise vertex and fragment stages,
mipmapped depth textures, LOD, bias, offsets, and cube-array comparison.
No shader special cases or backend capability overrides are used.

Evidence: `20260922T043749462400Z-gl46-shadow-lod-enable`, guarded M4,
64x64 headless EGL, CTS v1.4.6.2. Peak temperature 65.25 C.
Gallium SHA256 `bb00637159016fc57bdb9b039184c4c150fb6c01fee3cf590e37d96f8fda98c1`.
Vulkan SHA256 `06dfc67608b3fb4d1f1ba8fa7d4c6f4343c74890ce0ae8d76ac9d4a68b8273a4`.
This is targeted development validation, not a new full-suite result.
