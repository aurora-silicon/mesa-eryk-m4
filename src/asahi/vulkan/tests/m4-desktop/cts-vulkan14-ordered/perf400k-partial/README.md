# Ordered 400k run, partial (segments 1-8), throughput kernel

Kernel `9b93f16a6ffb` (module SHA256
`9b46d8104fa82babe11753df932d3b80f805915f063ebd23feb7d464c167fb59`), Mesa
`317d03eec33`, corrected CTS `deqp-vk-seed30-scaled-blit`. Batch
`build/cts-vulkan14-ordered-perf/run2/0000000-0400000`, forty frozen 10k
segments of the deterministic catalog, 20 workers, 50 cases per group,
60-second case bound, mandatory watchdog/thermal guards. The user stopped the
run after segment 8 to prioritize the completion-visibility fix.

Segments 1-8 (80,000 cases): 36,722 Pass, 41,275 NotSupported, 1 Fail,
1 Timeout, 1,930 Unrun and 71 Interrupted (segment 1 peers cancelled by the
timeout). Runner seconds per segment: 91, 196, 279, 44, 109, 280, 331, 347.
The earlier driver took 104, 453, 749, 141, 157 and 396 s for segments 1-6.

Open: `trim_command_pool` Timeout (6,000 sequential jobs, 13.3 s isolated,
not schedulable in 60 s against 19 competitors on a serialized GPU) and one
16384x6 image copy Fail in segment 4 that passed before, not yet investigated.
`NEXT_STEPS_DRIVER_OPTIMIZATION.md` records the root cause of the remaining
throughput ceiling (results become CPU-visible only after GPU power-off) and
the planned firmware flush qualification. Not a conformance claim.
