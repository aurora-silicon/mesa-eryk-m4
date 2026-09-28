# 0 A.D. CPU profiling and template evaluation

Retained 30-frame 4K Vulkan replay, M4; private allocation-fix Mesa
2e2f0cb06def7e6a7407059cb4da2bd295a486965e57dd60280908f61ca2b688.
Benchmark repository local checkpoint: 3c515f1d. Original capture and reference
unchanged; exact image validation passed. Three replay samples per setting:

| Setting | Median FPS | Individual FPS |
|---|---:|---|
| Baseline, inherited CPU caps | 26.7987 | 26.84, 26.62, 26.80 |
| IOTO templates enabled, same caps | 26.7412 | 26.84, 26.71, 26.74 |
| Baseline, restored authorized CPU maximum | 27.1654 | 27.17, 27.35, 27.07 |

No meaningful template improvement is established. The last row is a clock
configuration change, not a driver optimization. Keep templates opt-in.

A system-wide perf capture filtered to the replay process collected 379 CPU
samples during restoration and replay: LZ4 decoding 39.58%, memcpy 12.93%;
uniform load finish about 9.2% inclusive. This includes capture initialization,
not only scored frames, and does not measure GPU utilization. An earlier
screenshot-inclusive capture was dominated by PNG work; its outer expected
success marker was wrong, retained as missing_success_evidence, not a GPU fault.

Expanded existing template equivalence tests to G15S, retaining explicit G16G
composition coverage; host test passes 28,406 checks. This extends byte-level
coverage, not hardware qualification of every generated template.

Evidence: benchmark provenance/roadmap-20260928 and workspace
build/driver-roadmap-20260928/m4-0ad-warmup-perf-{inclusive,self}.txt;
benchmark sessions roadmap-20260928-{baseline,templates,full-clock}.
