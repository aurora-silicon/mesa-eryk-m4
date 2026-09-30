# Diagnosing remaining Minecraft Zink parity with M1

The previous deferred-UBO work is committed and pushed as `f182d8d9401`.
Its M3 replay median 51.83 FPS and refreshed M1 51.52 FPS establish parity,
not a hardware performance ratio or a live-game score.

## Newly reproduced pending-push-descriptor bug

Recording-time selection checks UBO descriptors before
`hk_cmd_buffer_flush_push_descriptors` fills the cached `set_sizes` array.
`hk_prefetch_buffer_address` already reads the CPU push storage, but uses
that not-yet-valid cached size. First draws can therefore reject an available
push descriptor and select the general fragment executable.

A bounded opt-in descriptor-rejection log identifies Minecraft's fragment
`4088ae4d` rejecting set 0, offset 64. The selected shader has 7156 instructions,
192 register slots, 48 bytes of scratch and three loops. It appears 30 times
inside the 30-frame interval. This is selection frequency, not pixel coverage
or attribution of total frame time to this shader.

Use the pending push storage's actual capacity when no regular set is bound,
consistent with the sampler predicate. Preserve the regular-set/root paths,
bounds checking, alias qualification and GPU-time content reads. Tests cover
initial zero size, stale previous size, last valid offset, one-past/wrapping
offsets, no storage and direct-bit removal. The sparse-access host suite passes.

Candidate `/opt/asahi-m3-push-prefetch-9e6c63c25cfc` (SHA256
`9e6c63c25cfc6ae302df142732bbbfbb443c32c12200644a437116553f95f38e`)
passes all three Minecraft GL images exactly against the prior optimized build.
Evidence: benchmark run `20260930T150705862467Z-m3-minecraft-demo-zink-check`.
Paired timings and broader checks are pending. The desktop is unchanged.

## Other measured leads and exclusions

A diagnostic replayer records per-call CPU elapsed times and engine counters
only across the completed/predecoded measured interval. Most main replay-thread
time is in swap/fence waits. This does not rule out CPU work on Zink's worker
threads. The M3 counter deltas include about 80 ms tiling, 158 ms fragment and
115 ms compute spans over 577 ms wall time. Global counters include other
clients and overlapping engines; their sum is not utilization or a complete
partition of frame time.

Although the wrapper requests `EGL_PLATFORM=surfaceless`, this binary explicitly
selects EGL's X11 platform and creates a hidden window in Xvfb. A separate
completion-before-presentation diagnostic leaves M3 at 51.92 FPS and M1 at
50.87 FPS for this one instrumented run. Thus presentation is not established
as the cause of parity. These are diagnostic timings, not replacement benchmark
scores. Existing statements that the binary actually renders through surfaceless
EGL must be corrected.

The measured interval records 40860 small compute dispatches with a 32-thread
group and 5450-byte programs, matching the indirect-index robustness path.
The ordinary GL trace has 90 multi-draw-indirect calls in this interval. This
is another optimization lead; no robustness requirement was disabled and no
per-shader GPU cost has yet been established.

Evidence is under workspace `build/m3-m1-parity/` and benchmark diagnostics
`m3-zink-bottleneck-20260930/parity-*-host.log`. The replayer source patch and
binary identity are retained; all hardware runs use existing watchdog/thermal
guards. M4 is untouched.

## Follow-up: CPU descriptor reads dominate a driver hotspot

The push-only candidate is **not a performance improvement**. Three alternating
pairs gave 52.1163 FPS for the previous optimized build and 44.3765 for push-only
(-14.85%), retaining every sample. Its shader profile does remove `4088ae4d`;
the replacement is the existing 260-instruction direct fragment program. The
instrumented global fragment span falls from about 158 to 137 ms, so static
shader size alone cannot explain total replay performance.

An exact-interval CPU profile (all replay threads, 499 Hz, CLOCK_MONOTONIC)
finds `hk_g17p_graphics_uniform_ranges` at 40% of 110 samples for push-only,
versus 26.4% of 106 samples for the prior optimized build. Stacks identify
inlined `hk_prefetch_buffer_address` reads. Descriptor pools are created with
flags zero in `hk_descriptor_set.c`; the kernel maps GEM storage write-combined
unless WRITEBACK is requested. Each promoted scalar was rereading the same
immutable CPU descriptor, even though its metadata is identical for adjacent
words. The exact cause of the push-only whole-frame slowdown is not completely
isolated; do not claim that every additional millisecond was proven to come
from these reads.

Cache the decoded descriptor **within one uniform-range construction**, keyed
by set and descriptor offset. Changes between descriptors/draws still fetch
new metadata, every word retains its own bounds check, and no UBO contents are
cached on the CPU. This reduces the hotspot to 11.2% of 98 CPU samples and
eliminates the observed push-only replay regression.

Candidate `/opt/asahi-m3-descriptor-decode-once-ba83c8b99ff9`, Vulkan SHA256
`ba83c8b99ff9b121cd673b8bb321a399890170da44958c20a435685e70237e91`:

| Build | Three replay FPS samples | Median |
|---|---|---:|
| prior optimized control | 46.7593, 52.8176, 51.7704 | 51.7704 |
| push fix + decode once | 52.9402, 52.4367, 47.2215 | 52.4367 |

The +1.29% median difference is within the observed variation, **not a proven
throughput win over the prior optimized build or M1**. Six game image checks
pass; Minecraft GL remains exact. The normal desktop is unchanged.

Separate matched-interval `getrusage(RUSAGE_SELF)` diagnostics sum CPU time
across all replay threads, excluding the supervisor and setup:

| Stack | User CPU ms | System CPU ms | Total CPU ms / 30 frames |
|---|---:|---:|---:|
| M1 installed Zink | 37.529 | 9.330 | 46.859 |
| M3 prior optimized | 207.503 | 0.709 | 208.212 |
| M3 push + decode once | 171.092 | 11.421 | 182.513 |

Thus the M3 control spends 4.44x as much process CPU time; the new candidate
reduces that by 12.34%, but still spends 3.90x the M1 time. Different stacks
and machines mean this is CPU time, not a normalized instruction-work count.
These instrumented diagnostics include completion fences and per-call clocks,
so they must not replace clean benchmark scores. M3's ordinary GL caller is
mostly waiting while worker threads do native program preparation. This is
why a main-thread-only profile was insufficient.

M3 read-only clock sampling in those same intervals sees state 8 (the configured
1380 MHz state) and state 0, with CPU policies at 4056000/2748000 kHz. State
samples alone do not measure GPU utilization. The paired runs remain below
the regulation threshold and do not implicate thermal throttling. This machine
is the 14-core M3 Pro configuration: validated mask `0x6f5fc` contains seven
cores in each of two clusters; do not compare it as the 18-core configuration.

## Remaining architecture work

- The interval contains 40860 small robustness dispatches associated with
  indexed indirect draws. Batch generation of their bounded native index-list
  commands could reduce CPU and GPU dispatch overhead while retaining runtime
  bounds/restart/zero-draw behavior. No such batching is implemented here.
- M3 `m3_runtime.rs::execute_inner` explicitly requires full retirement at
  engine/VM boundaries, batches only consecutive same-engine commands, and
  verifies idle engines/pipes before advancing. That establishes a scheduling
  restriction, not its isolated contribution to this replay's frame time.
  Improving overlap requires dependency/lifetime/recovery work, not simply
  deleting completion checks.
- Remaining sampled CPU work includes Ioto program bounds/generation and
  copies. Further caching must separate immutable program shape from per-draw
  GPU addresses and actual execution-time buffer values.

The evidence supports several concrete driver inefficiencies. It does **not**
prove a complete additive decomposition of the M3/M1 gap or a hardware ceiling.

## Final validation

The final candidate completes the frozen 1429-case descriptor/robustness/shader
selection healthy: **965 Pass, 464 NotSupported, zero failures**. Catalog and
global indices are retained from `build/minecraft-vertex-cse/production-cases`.
This is targeted regression coverage, not full Vulkan conformance. Host
sparse-access, descriptor-prefetch and native-uniform-prefix tests also pass.
Receipts: `m3-m1-zink-parity-validation-20260930.json` and
`m3-m1-zink-parity-cpu-20260930.json`. The helper
`tools/gl-interval-profile-summary.py` summarizes bounded diagnostic logs;
its source copy is retained in this document's tools directory.

One profiler invocation rejected an unsupported perf `--log-fd` argument before
starting the replay. Its failed receipt remains retained. The corrected helper
separates perf's diagnostic stderr and the workload's stderr, ties the workload
to perf's lifetime, and explicitly selects the monotonic sampling clock. No
hardware failure or reboot occurred during this investigation.
