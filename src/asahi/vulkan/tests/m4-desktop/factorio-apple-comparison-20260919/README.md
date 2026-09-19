# Factorio menu: Linux M4 versus Apple M5, 2026-09-19

The same five captured OpenGL frames at **2512x1552**, replayed three times
per machine, produce median completed-frame times of **29.132ms on Linux M4**
and **8.903ms on macOS M5** (3.272x). Images are nearly identical: only 298 of
3,898,624 pixels differ, maximum one 8-bit channel step, 308 differing channels.
The M4 raw-copy adapter was also checked against its native-copy replay and is
pixel-exact. No game settings, GLSL, draw calls or atlas content were changed.

| Metric, 15 samples each | Linux M4 | Apple M5 |
| --- | ---: | ---: |
| Median completed-frame wall time |29.132ms|8.903ms|
| Range |19.441–38.704ms|6.371–20.443ms|
| Mean |28.909ms|9.480ms|
| Median replay/GL-call interval |23.216ms|5.559ms|
| Median final glFinish wait |5.844ms|3.345ms|

**Interpretation:** a large gap remains in this full GL replay path. The longer
replay interval includes CPU decoding, GL/Zink processing and waits inside GL
calls. It is NOT isolated CPU execution. The final drain is NOT total GPU time;
work can execute while GL calls are being replayed. Do not add medians, call
these actual game FPS, or compare directly to earlier 6.4 ms native Vulkan GPU
submission spans. A serialized glFinish experiment suppresses overlap between
frames. M4/M5 hardware, OS, EGL/Cocoa and driver implementations all differ.
These data cannot assign a precise fraction of the gap to our shader compiler.

## What was measured

Apple driver identifies as `4.1 Metal - 90.5`, Apple M5, GLSL 4.10. The M5 Air
has 8 GPU cores. M4 runs normal installed c4304dc7d4f Mesa, kernel c40864f9d5f8
with early tiling opt-in OFF, and the already installed MCC/cache/fabric fixes.
Full allowed CPU clocks, watchdog 60 s and six-channel 95 C thermal guard retained;
final M4 three runs peak 80.75/79.5/80.5 C. Desktop remains active on both machines.

Capture 123334 uses ARM glxtrace at FEX's native thunk boundary. Both LD_PRELOAD
and a private libGL.so.1 search-path wrapper are needed: an earlier path-only
attempt captured just handwritten thunks and is invalid. Native GL capture has
727 frames; `gltrim -k -f 700-704` retains five menu frames plus required setup.
Setup preserves extensive prior FBO history; it is NOT included in timings.
No clicks or UI navigation were performed.

Trace hashes are in `menu-portable.adaptation.json`. The portable trace hashes
identically on host/M4/M5:
`97e6cc3487bb67d32711b16b322cc213d33936fa804e7f3313eb5149c8833ac9`.
Native preserved trim:
`ba56759bfc74d0aaa3a7d7ce9230d57e41abe50f73e67997cbd5b83e5fc5914e`.
M4 traces: `/home/e/.local/share/factorio-driver/gl-comparison-20260919/`.
Mac traces: `/private/var/tmp/factorio-comparison-20260919/`.
Local: `build/factorio-basic-vulkan/menu-five.trace` and
`build/factorio-apple-comparison/menu-portable.trace`.

## Explicit adaptations

1. Texture unit 159, used only as a scratch binding slot, maps to unused 31.
   Audit proves no slot 31 collision and actual sampler slots 0..4 remain intact.
2. App fragment-invocation Begin/End queries and timestamp writes are omitted
   on BOTH platforms (1477 retained setup+frame calls). The replay tool already
   skips query readbacks by default. This does not benchmark the query-heavy
   native Factorio path; it benchmarks its otherwise equivalent rendering.
3. 22 startup-only copies use raw CPU readback/upload on BOTH platforms, because
   Apple GL lacks CopyImageSubData. Integer texels that contain BC blocks are
   copied as raw blocks; no recompression. There are no such copies in the five
   timed frames. Check `reference/apitrace/retrace/FACTORIO_COMPARISON.md`.
4. Same apitrace source commit 252a0d6 builds native ARM64 EGL and Cocoa replay
   binaries. Linux uses surfaceless EGL; Mac uses Cocoa OpenGL. The desktop's
   inherited MESA_GL_VERSION_OVERRIDE=3.3 is cleared for this 4.1 capture.

`APITRACE_FRAME_COMPLETION_TIMING=1` waits for GL completion before ending the
frame sample, then drains presentation before beginning the next sample. Setup,
presentation pacing, stdout flushing and screenshots are excluded. `-b` removes
per-call error/profiling overhead only AFTER separate error/image validation.
Five samples are calls 719447,720631,721815,722999,724183. Do not use apitrace's
whole-process average FPS: it includes all capture restoration and compilation.

Apple elapsed queries returned zero with the core API and garbage with a
legacy EXT retrieval path. A separate CGL clear/timer probe reproduced core
zero results. These rejected GPU-only numbers are NOT in the table. The replay
code now initializes retrieval storage and uses the core result API on Apple;
this does not make zero results useful. Stock `--pframe-times` also prints every
call and includes swap, so its numbers were superseded by the explicit timer.

## Qualification and failures

- 124602 native-copy M4 reference: no GL API/compile/link errors.
- 125406 adapted-copy M4 reference: pixel-exact against 124602.
- Mac `gl-portable-reference`: near-exact final pixels. Apple logs an incomplete
 startup 3D-texture warning; neither driver reports GL API errors. Compilers warn
 about unused outputs/uninitialized local variables, with matching final image.
- 125523/125542/125602: final M4 timings, all guarded PASS, logs copied here.
- Mac `gl-completion-split`: final three timing runs, logs/manifest copied here.

The original Vulkan capture crashes/fails before rendering on both SDK
MoltenVK 1.4.2 and KosmicKrisp 92ec601a34. Both lack native fragment-statistics and
some Zink dynamic state. No valid Vulkan-vs-Metal timing was obtained. The
archived `factorio-portable-trace.py` is an UNQUALIFIED prototype: it bakes
invariant vertex layouts and expands multidraw, but unused restored pipelines
still fail Metal compilation. Do not deploy/use it as compatibility proof.

Attempting to recapture with four optional GL extensions disabled exposed
INVALID_FRAMEBUFFER_OPERATION in the application's alternate path. 123551 also
hit a transient XCB startup assertion; 123639 initialized but had GL errors.
Those captures are rejected despite the observer's initialization-only PASS.
Normal launcher/config and drivers were not changed. Other failed experiments
were replay setup (root Xwayland auth, inherited 3.3 cap, profiler's unwritable
working directory). They produced no accepted rendering/timing result.

This comparison does not achieve the 5 ms full-frame optimization goal. Next
investigation should profile native GL/Zink call processing and implicit waits,
using the preserved trace, while keeping true GPU execution time distinct.
