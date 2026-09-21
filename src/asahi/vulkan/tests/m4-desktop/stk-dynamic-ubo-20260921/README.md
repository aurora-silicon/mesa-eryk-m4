# Dynamic UBO fast path, SuperTuxKart 2026-09-21

The normal native Vulkan race spent about 15 ms in deferred lighting, out of a
27.78 ms six-pass GPU submission. Camera/light UBOs use dynamic offsets. The
uniform prefetch pass only matched scalar robust reads from ordinary sets;
dynamic descriptors in the draw root and non-robust vector reads stayed on
the expensive per-fragment alias-resolution path.

Extend the existing opt-in HK_M4_PREFETCH_UBOS path to statically selected
dynamic UBO descriptors and constant-offset non-robust reads, scalarizing only
eligible vector loads. Command recording reads immutable descriptor metadata
including dynamic offsets; Ioto loads the actual UBO bytes on the GPU at draw
execution. Bounds checks and the general sparse/read-only alias fallback stay
intact. Unsupported/dynamically selected layouts, partially bound/updateable
bindings and volatile loads retain their existing shader path. The existing
48-word optimization budget is unchanged.

Validation:
- Release ARM64 Vulkan build; expanded hk_descriptor_prefetch host test passes.
  It checks dynamic-root and ordinary descriptors, scalar robust and vector
  non-robust loads, eligibility rejection, dynamic indexing, volatile accesses,
  and correct fallback root uniform, in addition to existing direct/sparse tests.
- 70 targeted Vulkan 1.4.6.2 dynamic-UBO fragment access/graphics-copy cases pass,
  including nonzero view and dynamic offsets, arrays and multiple sets. Frozen
  global indices and catalog hash retained. This is not a full CTS retest.
- Native three-frame capture from
  20260921T045431781717Z-stk-vulkan-cache-fixed-capture, frames 300–302. Replays
  adapt the swapchain to offscreen and rebind memory; omit pipeline cache data.
  Baseline 20260921T055206073042Z-stk-native-pass-profile-baseline has median
  GPU submission 27.7816 ms; candidate
  20260921T060309557771Z-stk-native-dynamic-ubo-recheck has median 23.9486 ms,
  13.8% less GPU time. Lighting falls from 15.05 to 11.23 ms. Instrumentation
  perturbs throughput; these are GPU spans, not measured live FPS.
- Final image differs at 94/2073600 pixels, at most one channel level (of 255).
  Game settings/shaders/resolution are unchanged. No visual quality reduction.
- Earlier existing native arithmetic preamble switch alone had no measurable
  benefit (27.78 ms); it is not enabled by this change.

Use tools/vulkan-replay-cost.py RUN --render-passes 6 --reference IMAGE. It joins exact recorded/retired submissions,
rejects ambiguous address spaces or unmatched lifetime counts, and saves pixel
and timing summaries. Kernel SubmitTiming and Mesa architecture/shader profile
logging are required for attribution. All hardware runs use the mandatory
60-second watchdog, bounded guard and 0.5-second thermal monitor / 95 C stop.

## Original application validation

Native Vulkan run `20260921T060503244154Z-stk-vulkan-dynamic-ubo-race` completed
975 race frames / 44.888 seconds = **21.7207 FPS**, up from 18.9877 FPS
(+14.4%). The same private game configuration renders 3840x2160 and downsamples
to fullscreen 1920x1080. Race-only profiler excludes loading/menu time.
The 30-second screenshot shows the full correct track, kart, sky and effects.
Peak temperature 87.75 C; minimum available RAM 5429.2 MiB.

The main lighting shader drops from 1867 instructions / 192 register slots /
8 scratch bytes to 1613 instructions / 176 register slots / no scratch. The
other lighting-pass shaders drop from 1261 to 877 and from 291 to 110
instructions. These are compiler statistics, not individual shader timings.

Zink run `20260921T060650344591Z-stk-zink-dynamic-ubo-race` completed 765 race frames
in 70.806 seconds = **10.8042 FPS** (previous sample 11.0414 FPS). Correct race
image retained at 65 seconds; minimum available RAM 4869.8 MiB and peak
temperature 87.5 C. This change has no demonstrated Zink throughput benefit.
The observed 2.1% lower live FPS is a single-sample difference; the median
4K fragment pass over 1 ms is essentially identical (2.36204 vs 2.36298 ms),
with a lower mean (2.82367 vs 2.78734 ms). These aggregates mix different
passes and do not establish a CPU-side cause or prove no regression.

Installed the exact tested release library at
`/opt/asahi-m4-port/lib/libvulkan_asahi.so`, SHA256
`2a26dd99b54933ef32e0ec013c3a6106c76ff3809a8a03649a134f39e6660c5f`.
The previous library is preserved beside it with a checksum-named backup.
The binary was built just before commit 8e6543d18d7; its embedded revision
therefore names the previous HEAD. The source patch is retained in each run.
The normal desktop restart passed with this library, both eDP-1 and DP-1
active and DPMS on. No benchmark remains running.
