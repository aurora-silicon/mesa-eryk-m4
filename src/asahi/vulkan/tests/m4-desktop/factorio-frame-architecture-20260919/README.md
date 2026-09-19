# Factorio frame architecture diagnosis, 2026-09-19

User target: 5 ms/frame at unchanged quality. Not achieved. Latest separate
normal-menu observation averaged 41.041 FPS; this diagnostic is not live FPS.

The offline API audit distinguishes restored command buffers from submitted
ones. Frame CB28 has 257 draws across 17 API rendering passes. CB23/955 are
restored but not submitted. Four clears become compute jobs, leaving 13 actual
render passes. Setup and readback must not be counted as application frames.

HK_ARCHITECTURE_PROFILE is default-off submission/stream logging. Combine it
with HK_M4_SHADER_PROFILE and kernel SubmitTiming for a bounded diagnostic.
The uploader address differs from the kernel VDM alias; gpu-frame-cost.py
joins through HK_SHADER_SUBMIT, selecting an explicit ASID and matching exact
address lifetimes. Mismatches are excluded. Profiling perturbs throughput.

Uncompressed replay 070430510825Z: two actual frame submissions have 27.745 /
28.642 ms total fragment execution and 0.555 / 0.603 ms CPU submission. First
TA start to final fragment end spans 33.644 / 35.410 ms, including intervening
compute and scheduling. Compressed allocation adaptation 070504687842Z has
25.066 / 27.921 ms fragments and zero changed pixels against retained reference
88594c10407e243eb0617860b8f2abc1f06c0844ec67ddba3900875d7e36796e.
The compression-enabled replay did not actually allocate compressed render targets:
the retained trace requests HOST_TRANSFER usage, which disallows compression.
These two replays therefore do NOT measure compression benefit. A fresh capture
of the current application is required for representative performance work. Both replays retain normal draw and
shader content, offscreen swapchain adaptation and memory rebinding.

Basic native Zink fullscreen probe uses no timer queries, one triangle and
edge-pixel CPU readback. Kernel fragment spans at 2560x1664: solid 1.018-1.060
ms, texelFetch 1.835-1.974 ms, nearest sampling 1.840-1.881 ms. At 4K these are
about 2.05, 3.65, 3.62 ms. Invalidate hint before the solid draw does not improve
it (1.039-1.131 ms); an API hint alone does not establish which attachment load
operation Zink selected. Baseline behavior does not prove single-core execution
or a specific bandwidth/configuration bug. Investigate basic rendering first.

The mirrored tools are maintained in workspace-root tools/. Build GL probe
with cc -O2 -Wall -Wextra gl-frame-cost-bench.c -lEGL -lGL. Arguments: mode
0 solid / 1 texelFetch / 2 nearest sample; optional 1 invalidates old contents.
Run only through gpu-experiment.py, native Zink/Asahi environment, watchdog60s,
six thermal channels at 0.5s, cutoff95C. All six retained experiments passed;
replay peaks71/65.5C, microbench peaks61/60.5/60.5/60.25C. No full CTS run.
Mesa build passed. No execution policy is changed by this checkpoint.

## Current capture and scalar descriptor prefetch

The new five-frame capture is retained at
`/home/e/.local/share/factorio-driver/frame-architecture-20260919/menu_frames_1500_through_1504.gfxr`
on the target and `build/factorio-basic-vulkan/current-menu.gfxr` in the host
workspace. Capture072639832323Z uses the current native thunks and compression;
initialization9.564s, peak86C. Replay072903948794Z actually allocates23 compressed
images. Each frame submits67 draws,14 API passes,10 hardware render passes.
Its five fragment sums are13.906,14.312,14.362,14.227,14.121ms; spans from first
TA to last fragment18.831..19.136ms, CPUsubmission0.363..0.392ms. This includes
instrumentation and excludes setup; it is not live application FPS.
Compiler capture073451384544Z retains all inputs; peak77.25C.

The simple texture shader c23b3f45 still loaded a16bit sampler index per
fragment. Descriptor prefetch matched16bit data but rejected non-word-sized
loads. Fetch containing words while preserving halfword offsets. The existing
command encoder validates expanded spans against descriptor-set bounds. No
application-specific shader matching. Twelve new host cases cover low/high
halfwords and vectors crossing words, before/after address lowering.
Host tests and ARM64 build pass. Candidate075304344092Z replay is pixel-exact
against the original current-capture screenshot, SHA256
12a681058bb24f962dbb8bed2a69a00e0858924cc138577884561ee27f8b2ba2.
Five fragment sums13.003,13.925,13.739,13.764,13.705ms; total render spans
18.236..18.608ms. This modest improvement does not reach5ms/frame.
Candidate installed SHAede1f5947a54e11785e07aa22e13c11486c855a9cbaeb1e3e919442dd13227de.

Further basic controls: directVulkan clear/load-discard both ~1.05ms for solid
2560x1664 output; full pixels checked. Discarded-store diagnostic ~0.265ms
(no correctness claim). Compressed solid ~0.317ms. RGBA16F solid~1.85ms.
Compute SSBO stores073723597492Z check every outputword;2560x1664 ~0.79..0.87ms,
4K~1.55..1.62ms from native24MHz stamps, peak63.25C. This suggests investigating
memory throughput more generally, but is not a peak-bandwidth measurement.
The GL probe adds mode3 compute stores and optional wide-format selector;
fragment probe adds4096 extents, discard-load and diagnostic-only discardedstore.

Focused sampling oracle075353187879Z: all six PASS, peak63C (ordinary, border, minmax and minimumLOD). Full CTS remains deferred.
