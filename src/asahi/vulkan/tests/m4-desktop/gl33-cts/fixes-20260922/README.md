# GL 3.3 failure fixes on Zink / M4

The baseline is the frozen 9,886-case Khronos GL33 main list from
`../results-20260921.json`: 9,554 Pass, 317 NotSupported, 11 Fail,
three Timeout and one real assertion crash. Tests use native ARM64 Zink on
Honeykrisp, headless EGL Wayland, seed 1 and 64x64 surfaces. The 60-second
per-case bound is unchanged. No cases or advertised features were removed.

## Completed main-list recheck

All 9,886 frozen cases were completed across three bounded segments:
**9,569 Pass and 317 NotSupported**. All 15 original Fail/Crash/Timeout cases
now pass, and support status is unchanged for every case. Total test execution
was 762.53 seconds (12 min 43 s), peak observed temperature 103.75 C.

The four-worker segment hit the unchanged 60 s wall-clock bound on the LOD
bias case, then canceled three peers. On the same final build, LOD passes
alone in 40.02 s; all three peers pass in the continuation. No kernel fault
or OOM was reported. This retains a parallel duration limitation, not a claim
that the four-worker run finished uninterrupted.

`coverage.json.gz` retains original global indices and every terminal attempt,
including explicit PassAfterIsolatedTimeoutRecheck and
PassAfterCanceledPeerRecheck classifications. `cancellation-review.json`
records the SIGKILL attribution and memory evidence. `results.json` records
segment paths, timings and the exact validated libraries. Raw per-segment
results remain separate. For future parallel runs, execute the long LOD case
separately with one worker; retain the original catalog index and case bound.

## Fixes

| Layer | Problem | Change |
| --- | --- | --- |
| Honeykrisp | Indexed draws with input-assembly statistics were refused, leaving query waits stuck | Admit the already implemented native indexed path (`b195ca5c237`); preserve firstIndex in direct restart counting (`4900951f515`) |
| Zink | Query suspension/resumption crossed render-pass scope; line-loop QBO results bypassed normalization | Preserve scope and use the existing aggregated result path (`42698efe9ae`) |
| Zink | Multiple Vulkan query pools advanced one logical result offset repeatedly | Advance the copied range once across all parallel pools (`941a4f5c06d`) |
| Honeykrisp | Missing conditional rendering and transform-feedback byte-count draw support | Latch the GPU predicate and predicate draw/dispatch/clear arguments (`6db670204f2`); generate draw arguments from the byte counter (`abf45fe0a86`) |
| Honeykrisp | Millions of tiny dependent buffer copies became excessive firmware submissions | Track complete copy destinations and merge through retained native visibility barriers (`e5a35fceb50`) |
| Honeykrisp | Separately quantized sampler/shader biases exceeded LOD tolerances | Combine biases before hardware quantization and reconstruct biased LOD queries (`e8779daa3cc`) |
| AGX compiler | Copy propagation fed a half input to full-width exp2 | Materialize the widening conversion for native unary math (`dab4eed852d`) |

The Zink line-loop QBO failure reproduced with the same Zink on lavapipe;
its fixed pipeline-statistics group passes on both renderers. The parallel
query-range bug was isolated using raw M4 pool values: 0/3 and 2/2 were
correctly produced, but Zink returned 3 instead of their needed-count sum 5.
These are frontend bookkeeping fixes, not hidden driver failures.

## Targeted validation

- All 11 selected transform-feedback failures pass together, 2.04 s.
- Pipeline-statistics group: 9 Pass, 1 NotSupported, 5.08 s on M4; also
  9 Pass and 1 NotSupported on lavapipe.
- Texture LOD bias: Pass, 40.56 s on the initial corrected query build.
- Sparse copy chain: Pass, 28.57 s; exceeded the unchanged 60 s bound before.
- Vulkan conditional rendering: 247 Pass, 783 NotSupported, 5.08 s.
  This run precedes byte-count exposure; the previously unsupported
  conditional byte-count case passes in the next selection.
- Vulkan byte-count/transform-feedback draws: 39 Pass, 20 NotSupported,
  0.62 s. Includes offsets, multiview, queries and conditional rendering.
- Vulkan mipmap/bias/LOD queries: 1,413 Pass, 352 NotSupported, 37.71 s.
- Vulkan buffer copies and sparse buffers: 138 Pass, 39 NotSupported, 6.49 s.

The compressed result lists retain NotSupported outcomes. Frozen Vulkan
catalog indices and the catalog hash are in the corresponding indices files;
these overlapping selections are not additive full-suite coverage.

The new offline compiler test `agx3_graphics_compile_test
--unary-half-inputs-only` fails before the conversion fix and passes afterward.
The original failing serialized shader is retained in `unary-compiler-capture/`.
It also recompiles successfully with the fixed driver compiler, using
`tools/compiler-replay.py`; no Mac or GPU execution is involved in that replay.

## Investigation history and limits

`investigation-summaries.json.gz` preserves unsuccessful intermediate runs:
three GPU tiling timeouts (watchdog recovery), a duplicate log-flush CLI
harness error, test-compositor compilation refusals, a failed hardware-bias
rounding experiment, and the temporary LOD-query/compiler regressions.
The early suggestion that cold pipeline-statistics caused the tiling hangs
was disproven by a transform-feedback-only hang. Those transform-feedback
cases pass together after byte-count draws are implemented. This is not a
claim that every possible GPU timeout is eliminated.

Complete local raw evidence, including QPAs, device/library hashes, source
patches, kernel fault snapshots and half-second thermal telemetry, remains in
`_provenance/experiments/<id>/` as identified by each retained summary.
All hardware experiments use the mandatory 60-second watchdog, 95 C startup
gate, active kernel thermal management and provisional 120 C emergency limit.
The kernel was not modified for these GL fixes.

Other required Khronos size/seed/FBO configurations are outside this
before/after run. These results are development validation, not certification.
