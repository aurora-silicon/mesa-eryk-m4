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
  20260921T045431781717Z-stk-vulkan-cache-fixed-capture, frames300-302. Replays
  adapt the swapchain to offscreen and rebind memory; omit pipeline cache data.
  Baseline 20260921T055206073042Z-stk-native-pass-profile-baseline has median
  GPU submission 27.7816 ms; candidate
  20260921T060309557771Z-stk-native-dynamic-ubo-recheck has median23.9486 ms,
  13.8% less GPU time. Lighting falls from15.05 to11.23 ms. Instrumentation
  perturbs throughput; these are GPU spans, not measured live FPS.
- Final image differs at94/2073600 pixels, at most one channel level (of255).
  Game settings/shaders/resolution are unchanged. No visual quality reduction.
- Earlier existing native arithmetic preamble switch alone had no measurable
  benefit (27.78 ms); it is not enabled by this change.

Use tools/vulkan-replay-cost.py RUN --render-passes6 --reference IMAGE with
spaces between option names/values. It joins exact recorded/retired submissions,
rejects ambiguous address spaces or unmatched lifetime counts, and saves pixel
and timing summaries. Kernel SubmitTiming and Mesa architecture/shader profile
logging are required for attribution. All hardware runs use the mandatory
60-second watchdog, bounded guard and0.5-second thermal monitor/95C stop.
