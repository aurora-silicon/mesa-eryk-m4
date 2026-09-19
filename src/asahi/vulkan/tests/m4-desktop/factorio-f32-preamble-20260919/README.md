# Opt-in full-width uniform arithmetic in native fragment preambles

AGX_NATIVE_FLOAT_PREAMBLE=true, together with HK_M4_NATIVE_PREAMBLE=true,
allows only F32 add/multiply/FMA/negate/absolute-value expressions over already
prefetched uniforms. Float comparisons, conversion/narrowing and resource
loads remain excluded. This qualifies a subset; the cause of the earlier
arbitrary-float preamble failure is not claimed fixed. Default remains off.
The option contributes bit39 to the M4 compiler cache identity.

Offline agx_shader_replay INPUT --native-preamble enables the native fragment
compiler key without changing shader values. Set AGX_NATIVE_FLOAT_PREAMBLE
explicitly to compare the subset with integer-only preambles. Captured
67327449: initial 146 instructions; late CSE 133; basic F32 preamble 100
per-pixel instructions plus 84 once-per-draw instructions. 55 register halves,
no spills. Compiler occupancy estimate is inherited from the G13 model and
must not be mistaken for measured M4 occupancy.

Guarded J713 evidence (20260919 UTC, watchdog60s, six sensors0.5s, cutoff95C):
- 062313897290Z: exact GL RGBA32F arithmetic on 32 changed uniform blocks,
  32768 component checks pass. Profile confirms a 37-instruction preamble.
- 062347014608Z: prior minimum-LOD reproducer Pass.
- 062438234298Z: all six retained implicit sampling oracles Pass.
- 062505199752Z: original complete Factorio replay Pass, zero differing pixels
  from reference PNG88594c10407e243eb0617860b8f2abc1f06c0844ec67ddba3900875d7e36796e.
- 062551345594Z: bounded normal2512x1552 main menu, no GUI input. Entire
  window116 samples mean38.928FPS, median39.955; tail12 mean40.175FPS.
  Peak85.5C. Scene changes prevent an isolated speedup claim. Goal60FPS unmet.
- 062927047505Z: every one of the32 failures from the former arbitrary-float
  prototype now Pass,6.844s runner time,peak65.5C. This is focused regression
  coverage, not the deferred full Vulkan CTS campaign.

Observer now records complete-window FPS alongside the historical tail12
summary. Raw samples are retained. No game resolution/quality/input changed.
