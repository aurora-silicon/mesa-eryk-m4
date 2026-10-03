# Native G15/G16 FMA uniform operands, 2026-10-03

An identical FP32 diagnostic was taking 1.530 ms of GPU time on M3 Linux
and 0.652 ms through MoltenVK/Metal on the same M3. Apple's actually created
compute pipeline reports 16 main instructions, an eight-instruction loop,
eight temporary registers and no spills. These register units have not been
qualified against Mesa's half-register units. Offline Apple G15S compilation
of the same arithmetic witnesses direct uniform multiplier/addend operands;
our loop instead materialized eight float constants on every iteration.

`AGX_NATIVE_FMA_UNIFORMS=true` retains aligned F32 uniforms for plain scalar
FMA on AGX3B. One multiplier and/or the addend may be uniform; the other
multiplier remains a register. The packer commutes both multiplier source
indices and their lifetime hints together. Modifiers, half/immediate sources
and other shapes retain the existing legalization. AGX2 behavior is preserved;
AGX3C has no new path. The opt-in has a separate compiler/pipeline-cache key.
It remains disabled by default pending broader workload qualification.

The portable encoder reproduces twelve fresh strict-math Apple G15S byte
vectors (four lanes each for uniform multiplier, uniform addend and both),
with independent cache/discard hints and all six dependency waits. Invalid
widths, alignment, banks and contradictory hints are refused. 314 compiler
unit tests pass, including source commutation and unsupported-shape
legalization; all 384 existing modifier vectors still pass. The wider host
Asahi suite has 118 passing tests and two failures: compile_fail_closed's
half local-ID IADD and graphics_compile's stale G17P fixtures. Both failures
were independently reproduced after rebuilding unchanged HEAD 1a2c05cd8c3;
they are not silently counted as passes or introduced by this change.

On ordinary M3 boot 7e62c644-27a2-4c50-bd1e-2a6885c6d99a, three alternating
uninstrumented process pairs reproduce GPU medians of 1.533458, 1.534583,
1.533750 ms without the option and 0.894292, 0.893500, 0.893625 ms with it.
Each process has nine warmup pairs, 21 alternating queried/unqueried samples
and a full output comparison twice against CPU fmaf (at most one ULP).
The 128 MiB read/write copy control remains 1.964875 versus 1.963875 ms.
This is a repeatable arithmetic benefit, not a qualified clock measurement
or evidence that all Minecraft time has the same bottleneck.

All three approved 3024x1837 Minecraft Vulkan gameplay images remain exactly
identical. Two nine-pair completed-frame studies do not establish a material
Minecraft gain: first median 167.402 versus 169.021 replay FPS, paired mean
+0.698%, paired median +0.274%, seven wins; final-build median 168.325 versus
168.537, paired mean -0.366%, paired median +0.101%, six wins. Setup, warmup
and PNG work are excluded. Native macOS's existing matched median remains
271.307 FPS. Do not publish the arithmetic gain as a Minecraft FPS gain.
A maximized real Vulkan Minecraft copied-world check with the option verified
in the game process completed in 23.01 seconds at 3024x1837 with F3, terrain,
foliage and HUD visible. Normal desktop libraries and the user's save are
unchanged.

231 frozen Vulkan 1.4 cases cover FMA precision across widths, FP32 FMA
float controls in compute/graphics, uniform-buffer compute accesses and float
add/multiply graphics: 210 Pass, 21 NotSupported, no failures or crashes.
Each retains its original global catalog index. Catalog SHA-256:
72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
Case-list SHA-256:
98f6dbd1156bce1f6d99eadd24170716a88ddd7a8c4347dbdfd57eccdb12b22f.
M4 and Neo hardware have not been qualified by these M3 experiments.

Final private Vulkan library SHA-256:
08ca3a2db485c5531d8d88a7709833b79625789f40921ba4de212d2e746798b8.
The arithmetic repeated trials used the prior M3-identical-path build
b3e0eb8fd0a66d8c0b98ca72c5a060c1a6867a5ca774bba75f8a83b8e5dbb399;
the final build corrects preservation of AGX2 high-bank acceptance and test
process environment restoration. Final-build CTS, replay and live-game
qualification use the final hash above. Every Linux experiment owns a bounded
workload, the 60-second watchdog and all-channel thermal telemetry.

Evidence, source patches, exact commands, native actual-pipeline statistics,
Apple offline witnesses and readbacks remain outside Git under
`gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/throughput/`.
Offline archives witness encodings; they are not claimed as the bytes of the
executed Apple PSO. No vendor binary or gameplay asset is added by this commit.
