# Opt-in native reciprocal preambles and M3 program reuse

Native preamble arithmetic originally admitted only 32-bit add/multiply/FMA,
negation and absolute value. Uniform reciprocals remained per invocation;
integer texture-size conversions and min/max dependencies also blocked them.
`AGX_NATIVE_RECIPROCAL_PREAMBLE=true`, together with the existing
`AGX_NATIVE_FLOAT_PREAMBLE=true`, admits those full-width operations when NIR
independently proves them uniform. The operation and floating-point controls
are retained. This does not weaken reciprocal refinement, integer division,
rounding or narrow-export restrictions. Both options default false. Vulkan
compiler UUIDs/disk-cache keys distinguish the reciprocal option at bit 47;
the compiler's verbose-only debug bit does not collide with it.

Hoisting alone was counterproductive: native preambles disabled the scoped
multi-draw fragment-program cache. One 260-instruction world shader was
uploaded 32 times in the ordinary diagnostic capture, versus 19,616 times
with preambles and no reuse. These are uploads over the whole capture, **not
invocation counts, GPU time or exclusively the thirty timed frames**.

`HK_M3_MULTIDRAW_PREAMBLE_PROGRAM=true` allows the existing scoped cache to
retain an immutable native preamble program with zero main/preamble scratch.
It defaults false and applies only to G15S fragment programs within one API
multi-draw call and one graphics control stream. Shader/linked executable,
fragment-equivalent root, image and sampler tables must still match. Memory
writes, vertex memory writes, geometry/tessellation, statistics and meta
paths remain excluded. Every draw executes the loader and preamble again;
live buffer contents and computed uniform results are never cached. Retained
command-pool bytes and executable BO ownership survive recording and execution.
The broader complete-loader cache still excludes preambles. M4's recording
path is unchanged; its optional compiler arithmetic is not hardware-qualified
by these M3 results.

## Qualification on M3 Pro

311 compiler unit tests pass, including native reciprocal motion requiring
both opt-ins, full-width values and uniform inputs, preservation of exact
math controls, rejection of narrow/varying operands, and a uniform
conversion/minmax/reciprocal dependency chain. ARM64 Vulkan builds. A retained
current-ABI compiler input also demonstrates the dependency chain changes the
main fragment register requirement from 87 to 79 half registers. This is a
compiler diagnostic, not an occupancy or throughput measurement.

Private Vulkan library SHA-256:
69f733c01406060ffe9a20770134f137342c7868fbd7b08e601fbc2ff28e21de.
Kernel module SHA-256:
42983d472f2d9ffde46adbff65651539eea227fcfe6dd72d36515bc2ee280774.
Boot: 96f0893c-f9c6-44b5-9cbc-2eb5b8df8093.

All compared policies match the three approved Minecraft Vulkan gameplay
images exactly at 3024 x 1837. Fifteen same-boot alternating pairs, with the
same binary and thirty completion-inclusive frames per sample, give:

| Comparison | Baseline median FPS | Candidate median FPS |
| --- | ---: | ---: |
| Ordinary compiler versus reciprocals/dependencies plus scoped reuse | 159.263 | 161.668 |
| Existing arithmetic preambles plus scoped reuse versus added reciprocals/dependencies | 163.496 | 166.053 |

The first paired gains average 1.460%, median 2.099%, with nine wins. The
isolated compiler control averages 1.386%, median 1.389%, with ten wins.
These are small improvements with visible timing variation, not live Java
FPS or macOS parity. A simpler reciprocal-only precursor plus reuse measured
161.884 versus 164.543 FPS, paired mean 1.719%, twelve wins. These separate
studies do not prove the broader dependencies outperform that precursor.
The no-reuse precursor was slower (162.055 versus 159.550 FPS), retained as
a control. Startup, warmup and PNG work are outside the timed interval.

548 frozen catalog cases cover the existing dynamic/secondary/viewport/
scissor/robust/occlusion/empty-geometry matrix plus 42 FP32 division/min/max
float-control cases. Results: 348 Pass, 200 NotSupported, no failures;
unsupported cases and original global indices remain recorded. Catalog SHA-256:
72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
This is targeted coverage, not full Vulkan CTS. Every segment verifies the
60-second hardware watchdog, fresh thermal telemetry and policy restoration.

Normal accelerated Plasma starts and completes its application/screenshot
probe through its unchanged desktop Mesa. The private candidate then runs a
copied, maximized Vulkan Minecraft demo world at 3024 x 1837 with F3 for
22.50 seconds and exits normally. The retained screenshot shows terrain,
foliage and HUD, rather than a loading screen. Native-library mappings,
watchdog completion and policy restoration pass. The original save, desktop
Mesa selection, normal GPU/CPU clocks and idle display policy stay unchanged.

Raw profiles, exact image checks, all alternating samples, compiler inputs,
CTS indices, guard receipts, telemetry and live-game evidence remain in
gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/{reciprocal-preamble,
preamble-program-reuse,uniform-reciprocal-v3}. Captures, images and vendor
binaries are outside Git.

The fresh same-capture macOS reference is 266.406 / 271.307 / 272.169 replay
FPS. Native world TA/fragment diagnostics measure about 1.191 / 1.550 ms;
earlier Linux measurements were 1.361 / 2.226 ms. Native CPU command-buffer
lookahead is independently observed and must not be reported as GPU stage
overlap. Submission/preparation and fragment execution both remain open
performance work.
