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
Compression does not explain the main gap. Both replays retain normal draw and
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
