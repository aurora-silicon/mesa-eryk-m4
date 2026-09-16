# M4 DirectX submission/compilation investigation, 2026-09-16

J713, G16G, Vulkan CTS 1.4.6.2. This measures driver/test behavior, not a
DirectX game or desktop stability claim. All hardware runs used the 60-second
watchdog, six-channel thermal monitor and unchanged 60-second case bound.

The handoff's 11 ms "submit/wait" cost combined a five-command submission
with CPU work between iterations. `HK_SUBMIT_TIMING=1` now also logs
`HK_FENCE_TIMING` around the common WaitForFences implementation. Its result
and device-loss checks remain unchanged. The submit gap accumulator is
thread-local to avoid a diagnostic data race with multiple submit threads.

The kernel's optional `debug_flags=8388608` (bit 23, SubmitTiming) logs
per-command retirement, firmware event receipt, and fence signal entry/exit.
This is a module-load parameter on the current Rust kernel, not a writable
sysfs parameter. Do not enable the much more verbose KTraceCh for this.

`submit-timing.py RUN_DIRECTORY --output report.json` consumes one case/worker
from cts-bulk and correlates explicit CLOCK_MONOTONIC timestamps. It rejects
ambiguous retirement matches and incomplete submit/wait pairs. The kmsg prefix
clock differs from CLOCK_MONOTONIC and must not be used for this correlation.
An event-to-retirement sample is the latest event, not proof of event ownership.

## Finding and fix

`pipeline.monolithic.blend.dual_source.multi_attachments.r16g16b16a16_unorm`
performs 2,500 iterations, each with **one** submission containing three
compute commands and two render commands. It recreates pipelines/records
commands between submits. The shader compiler still compiles internal blend
epilogs, even though the two main fragment programs are cached.

The old key retained blend factors for MIN/MAX, although NIR's blend lowering
ignores them. Canonicalizing those factors to ONE shares equivalent epilogs
and also avoids asking for unused dual-source exports. Factored operations
retain their exact factors. The change applies to all callers of the common
blend-key packer, without a test or format-specific path.

| Serial measurement, full CPU caps | Before | After |
| --- | ---: | ---: |
| Runner elapsed | 29.315 s | 17.464 s |
| Median submit to retirement | 5.022 ms | 4.976 ms |
| Median wait return to next submit | 6.035 ms | 1.092 ms |
| Median retirement to wait return | 15.0 us | 11.1 us |

These are individual runs, not a game FPS measurement. The before/after
kernel differs only by opt-in timing instrumentation. The additionally
profiled baseline ran in 27.700 s; it measured a 2.75 us fence signal and
12.0 us signal-to-wait-return median. Commands took about 0.74/1.16/0.69/
1.17/0.58 ms from publication to observation, plus preparation. These include
firmware/host turnaround; they are not pure GPU execution times. The median
latest-event-to-command-retirement interval was 47.8 us.

The user CPU profile found NIR/AGX compilation and validation, with
`util_vma_heap_validate` also accounting for 9.72% of samples. This was an
assertions-enabled build. No validation or correctness checks were disabled.
`perf-command.py` wraps a CTS binary inside the existing outer guard, using
`M4_PERF_BINARY` and `M4_PERF_OUTPUT_DIR`; copy its per-process output into
the run evidence. It restores the application's library path after loading
the private perf libraries. The first perf attempt failed before case execution
because that private library path was missing; the corrected run passed.

## Validation and remaining work

- New host test: 54 blend-state pairs compile to byte-identical AGX3B binaries
  before/after canonicalization, including live ADD and ignored MIN/MAX
  source/destination factors and dual-source factors.
- Host and ARM64 Vulkan builds pass. Rebuilt Asahi suite: 102 pass, the same
  11 previously documented failures. A stale register-bank test still called
  `reserved_size`; it now calls `agx_reserved_register_count` and passes.
- Hardware regression at 12 workers: 815 Pass, 186 NotSupported, no failures
  (the unchanged 1,001-case baseline).
- All 11 cases in `timeout-53f483ee2a` still time out at 12 workers. The group
  stays open. This optimization does not resolve serialized firmware command
  throughput; the next investigation belongs there, not fence wakeups.
- Sparse aliasing/residency and texel-buffer single-texel alignment remain
  separate DirectX capability work.

Small reports are under `directx-latency/`. Full evidence roots:

- `_provenance/experiments/20260916T065852918665Z-dx-fence-baseline`
- `_provenance/experiments/20260916T070628346104Z-dx-perf2`
- `_provenance/experiments/20260916T071045300187Z-dx-blend-key`
- `_provenance/experiments/20260916T071136438854Z-mt-timeout-53f483ee2a-20260916t091134`
- `_provenance/experiments/20260916T071254898249Z-dx-blend-key-reg1000`
