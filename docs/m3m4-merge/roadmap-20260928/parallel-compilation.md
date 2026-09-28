# Parallel compilation with code-generation debug options

Honeykrisp previously took its per-device debug compilation mutex whenever any
AGX_MESA_DEBUG bit was set. Thus nopromote serialized all backend work even with
eight pipeline-creation workers. Only shaders/shaderdb actually request shader
or statistics output; those modes retain the lock. Code-generation flags keep
their existing executable/cache semantics without taking this output lock.

The backend also assigned the global agx_compiler_debug value on every compile.
Concurrent writes were already possible in ordinary compilation, even though
all threads wrote the same value. Initialize it once before any compilation
reads it. Capture failure streams remain thread-local; the existing capture
publisher retains its file lock.

Host validation: a new eight-thread test races initial flag publication, then
compiles 16 native memory-preamble compute variants per worker. All 128 outputs
match subsequent serial compilation byte for byte, including metadata. ARM64
Vulkan builds. The Meson asahi suite reports 110 Pass and ten failures on both
the unchanged compiler baseline and this change. The exact failures are retained
in parallel-compile-host-results.json: they include obsolete refusal/encoding
expectations and two crashing host tests. They remain open; neither the suite
nor a complete driver qualification is claimed passing. Cold hardware replay qualification is recorded below.


Two baseline host crashes are repaired independently of the compilation change:
`agx_register_bank` left the allocator's tex_low128/low_gpr bitmaps null in its
standalone fixture. Production allocation already initializes both. Initialize
the fixture and add a constrained-register shuffle checked by the independent
parallel-copy oracle (260 cases). `hk_minmax_control_flow` had an obsolete
expectation from before dynamically uniform descriptor branches were enabled.
Expand it to all four texture/sampler uniformity combinations; all 24 cases
require implicit derivatives outside nonuniform branches and allow them inside
uniform branches. Both tests pass. Eight other baseline failures remain open.


## Cold full-capture hardware validation

One paired Satisfactory DX11 gameplay run per GPU, both shader caches disabled,
eight replay pipeline workers, nopromote, nocompress, memory translation none,
and ordinary shader mode (native preambles off). All three 4K RGB snapshots
match the retained reference exactly for both variants on both chips. Watchdog
and temperature/progress gates remain mandatory; desktops restore afterwards.

| GPU | Serialized total wall time | Parallel total wall time | Reduction |
| --- | ---: | ---: | ---: |
| M3 Pro | 100.237 s | 52.194 s | 47.9% |
| M4 | 133.989 s | 75.632 s | 43.6% |

These are total cold-replay times, including compilation and the same 30 rendered
frames, not application FPS or pure backend compiler times. The measured frame
ranges are 15.804/15.802 seconds on M3 and 28.562/29.591 on M4. No sustained FPS
improvement is claimed. This is one pair, not a repeated statistical benchmark.
The M4 test leaves boost disabled and records its effective clock/thermal policy.
Candidate Vulkan SHA256 e79b2737ffbb8e60e3359bb022fe54220c38a1f6af365295068cdc782a84ff40,
source d81a22084f4. Ordinary installed desktop Mesa remains unchanged.
