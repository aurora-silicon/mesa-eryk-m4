# M4 explicit cube-map levels

SuperTuxKart race pipeline9139 mixes a 2D fetch with cube textureLod at zero.
The cube shape matcher only accepted implicit samples. This prevented indexed
resource lowering for the entire shader, and the backend refused the remaining
bindless fetch. Preserve constant LOD through cube projection and permit the
native immediate-level cube encoding. Register LOD with cube coordinates remains
unsupported because the shared auxiliary operand layout is not qualified.

Offline Metal32023.883 on macOS25G76, applegpu_g16g, source g16g-cube-level.metal:
- level0: text offset164, 05800cb8b0001300000010000100
- level2.25: same source with level(2.25f), 05800cb8b0001380000010080100
These encode cube dimension4, explicit-level mode2, immediate LOD scaled by64.
Artifacts and tool/source hashes: build/stk-native-cube-lod{,225}/.

Validation2026-09-12:5672 texture encoder checks pass, including exact bytes for
both native witnesses. Cube projection and descriptor lowering compile for
implicit, level0 and level2.25. m4_zink_depth_cube --cube-lod fills mip0 and
mip1 with distinct face colours and samples mip1: all six faces/6144 pixels
pass exact readback on software and M4. Watchdog/thermal evidence:
20260912T045428346989Z-cube-explicit-lod-oracle, peak54.75 C. Installed ARM64
library4761e877a47c4448c59b0427a90f29cf8b8697ec687049853a43fc7a5fb4f2d6.
This numerical qualification is separate from full game rendering and stability.
