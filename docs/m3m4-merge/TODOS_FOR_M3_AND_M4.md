# M3 and M4 driver roadmap

Created 2026-09-27 at the user's request. This is a long-term work plan for
M3 Pro J514S and M4 J713, covering the kernel, shared Mesa, display integration
and validation tools. Hardware success on these boards does not qualify other
Apple GPUs, boards or firmware versions.

The workspace copy lives at `TODOS_FOR_M3_AND_M4.md`. A tracked copy is kept in
`mesa/m3m4-merged/docs/m3m4-merge/TODOS_FOR_M3_AND_M4.md`; update both together.
Existing detailed plans remain useful, particularly
`M3_AND_M4_DRIVER_CLEANUPS.md`, `DRIVER_CLEANUPS_STATUS.md` and
`M3_M4_MESA_MERGE_PLAN.md`. This roadmap includes behavior changes that the
earlier representation cleanup intentionally deferred.

## Rules and priorities

- Fix crashes, corruption and regressions before performance work.
- Commit small, reviewable changes and push kernel/Mesa checkpoints often.
  The game benchmark/capture repository remains local-only.
- Preserve original test results, traces, reference images and binary hashes.
  A successful retry does not erase the original failure.
- Keep the 60-second watchdog, bounded experiments, 0.5-second all-channel
  thermal telemetry, startup gate and authorized 110 C regulation / 120 C
  emergency development policy. Do not relax these to make tests pass.
- Keep normal accelerated login/autologin usable after experiments. Preserve
  screen blanking for burn-in protection and the user's existing sleep policy.
- Share mechanisms when evidence supports it. Keep real hardware/firmware
  differences explicit; do not remove chip checks merely because ISAs are close.
- Treat an unchecked item as incomplete. Source compilation, a CTS pass and
  normal desktop validation are different milestones.

## Current baseline

- [x] Shared Mesa branch: `eryk_m3m4_merged_mesa`.
- [x] M3 progress/health interface independent of printk, with routine logging
  opt-in. A deliberate overrun of the default 128 KiB ring completed 88 rendering
  tests successfully. Published kernel commit: `c07786baa30b`.
- [x] Fix the discovered XFB metadata overflow, advanced blend-state ordering,
  unbound static vertex inputs, float16 tessellation IO and M3 sample-location
  limits. See `docs/m3m4-merge/cts-fixes-20260927` in Mesa.
- [x] Check 19,635 surrounding cases on each GPU: 5,125 Pass and 14,510
  NotSupported on each, with no failures. Final M4 reproducers: 135 Pass;
  M3: 134 Pass and one unsupported 128-component XFB case.
- [x] Finish M4's 3,206,030-case frozen Linux catalog. No further failures
  appeared after the known fixes. Original 134 failures, one crash and four
  QualityWarnings remain in the versioned campaign record.
- [ ] Finish M3's catalog. Paused at 2,712,963 recorded cases after firmware
  error event 4. A test process was subsequently killed by the guard; its
  `Crash` label is not proof that userspace caused the GPU fault.
- [ ] Eventually run a complete qualification campaign on a single final
  kernel/Mesa revision. The current campaign spans explicitly recorded fixes
  and uses an existing corrected CTS binary; it is not an official unmodified
  Khronos conformance submission.

## P0: diagnose the new M3 fault and finish correctness coverage

Status: in progress. Evidence:
`build/cts-paired-20260926/m3-pte-fix/segments/2712954-2713022`.
The runner reported `float16.logical.opfordequal_vector_tesse` at catalog index
2713005. Firmware reported a render failure, no faults in the sampled MMU banks,
and a nonzero VDM state on one core. These are observations, not a root cause.

- [x] Reproduce the identified case in isolation with the exact failing binaries.
  Also fails with the pre-CTS-fix Mesa; current M4 passes the same case.
- [ ] If isolation passes, replay the original case sequence and investigate
  accumulated state, lifetime and allocation effects. Do not assume the last
  reported test caused the fault.
- [ ] Capture the relevant shaders, generated code, submission state and first
  firmware error. Compare against the M4 result and a known-good reference.
- [ ] Fix the responsible compiler, submission or lifetime bug; retain a small
  reproducer and add a focused regression test where practical.
- [ ] Validate the original sequence, adjacent float16/tessellation cases and
  relevant game captures on both GPUs.
- [ ] Resume only missing full-catalog indices after explicit evidence review.
- [ ] Retain and explain allocation-callback QualityWarnings separately from
  failures; do not add dummy allocations simply to silence the tests.

Acceptance: the fault is explained and fixed, original reproductions pass under
the normal guards, the desktop remains usable, and the remaining M3 catalog
completes with every non-pass classified.

## P1: reliable failure handling and GPU recovery

### Error propagation and ownership

- [x] Latch M3 errors after queue publication, including fallible submission and
  retirement reads, before the next packet can release retained resources.
  Preserve the primary error when diagnostic reads fail. Development build
  validated with the real fault; published-source adaptation preserves its
  existing unlocked-wait and recovery-diagnostic code.
- [x] Retain unhealthy host-progress snapshots before stopping watchdog renewal;
  distinguish unhealthy state from malformed or incompatible progress ABI.
  Six offline checks pass; the real M3 fault retains `healthy=0` evidence.
- [x] Keep partial CTS regression outcomes when the guard kills the runner before
  its final batch receipt. Missing receipt means incomplete, never success.

- [ ] Audit every submit, timeout, firmware crash, MMU fault and teardown path.
  Document which jobs, queues, VMs, buffers and callbacks each path owns.
- [ ] Ensure affected fences complete with an error, waiters wake, new work on
  lost contexts is rejected, and Mesa consistently reports device/context loss.
- [ ] Distinguish the first hardware fault from secondary canceled jobs and
  watchdog actions in machine-readable evidence.
- [x] Add bounded M3 active-job firmware snapshots through devcoredump and
  collect them under the guard before watchdog recovery. The real fault retains
  478,763 bytes across 12 owned-RAM records; five parser/collection tests pass.
  Development `90d936e42cac`, published adaptation `7c64de6e7964`.
- [ ] Complete first-fault snapshots across all error paths and both GPUs.
  Evaluate devcoredump and DRM wedged notifications rather than bespoke large
  log dumps. Include shader addresses, queue positions and firmware identity.
- [ ] Exercise application exit, SIGKILL, device destruction and failed
  initialization while work is queued or executing.

### Safe reset and reinitialization

- [ ] Identify and validate the hardware mechanism that stops GPU DMA. Stopping
  the firmware CPU alone is not sufficient evidence that DMA has stopped.
- [ ] Define a recovery state machine: healthy, quiescing, resetting,
  reinitializing, available, or irrecoverably wedged.
- [ ] Prevent submission/reset races and stale interrupts, callbacks, events,
  completion generations or firmware pointers from crossing a reset.
- [ ] Keep mappings, backing allocations and module code alive until DMA and
  callbacks are proven stopped. Preserve current conservative retention on
  unqualified paths; never trade a reboot for a use-after-free.
- [ ] Rebuild firmware queues, memory mappings and power state after a qualified
  reset, while marking old contexts lost.
- [ ] Allow a fresh process/context to render after recovery. Preserve unaffected
  clients only if hardware isolation and API semantics have been demonstrated.
- [ ] Recover the compositor/display session where possible; document when a
  compositor restart is required. Retain watchdog reboot as the final fallback.

Acceptance: repeated, bounded injected hangs/errors return predictable failures;
new contexts render afterward; no stuck fences, DMA to freed memory, resource
growth or routine whole-machine reboot. Test both GPUs independently.

## P2: predictable memory pressure and resource lifetimes

- [ ] Inventory GPU/CPU allocations, pinned pages, virtual-address ranges,
  descriptor storage, command arenas, firmware pools and shader caches.
- [ ] Add useful counters for current/peak usage and retained-on-fault resources;
  keep accounting bounded and cheap when detailed tracing is disabled.
- [ ] Audit allocation-size arithmetic, alignment, indexing and release-build
  bounds checks, including serialized shader metadata and cache validation.
- [ ] Audit allocator callbacks and reference-counted object lifetime contracts.
- [ ] Inject allocation failures at initialization and submission boundaries;
  unwind partial construction without leaks or stale published pointers.
- [ ] Return appropriate allocation errors or apply bounded backpressure when
  queues/pools are full. Never block indefinitely waiting for a failed GPU.
- [ ] Test concurrent clients and constrain resource exhaustion so a single
  process cannot unnecessarily destabilize the desktop.
- [ ] Investigate reclamation/eviction for eligible resources. Do not introduce
  reclaim waits that depend on fences which cannot complete under memory pressure.
- [ ] Implement parameter-pool growth, arena relocation or greater pass capacity
  only with explicit ownership, publication, rollback and retirement rules.
- [ ] Stress sparse mappings, map/unmap churn, dma-buf import/export, application
  exit and shader-cache churn under controlled memory pressure.

Acceptance: allocation failures are bounded and recoverable, resource usage
returns to baseline after clients exit, imported/exported ownership is correct,
and memory stress preserves system responsiveness without leaks/corruption.

## P3: submission, synchronization and CPU overhead

- [ ] Measure separate CPU preparation, mapping, queue wait, GPU execution,
  retirement and fence-notification times using stable counters/trace events.
- [ ] Profile representative CPU-bound and GPU-bound captures on both devices.
- [ ] Remove redundant address-space scans, mapping updates, cache maintenance,
  descriptor rebuilds and state uploads only after proving their dependencies.
- [ ] Inspect forced dirty-state paths and unconditional rebuilds; use precise
  invalidation with pipeline-switching regression coverage.
- [ ] Cache reusable validated state with explicit VM/resource generation keys.
- [ ] Reduce avoidable polling and host round trips while retaining reliable
  event/stamp completion verification and bounded timeout behavior.
- [ ] Increase work in flight and overlap CPU/GPU or independent engine work
  where hardware dependencies permit; preserve Vulkan synchronization semantics.
- [ ] Evaluate scheduling fairness and priorities with a game, compositor and
  background compute active together. Prevent starvation and unbounded latency.
- [ ] Track median and tail frame times, CPU time and energy/temperature context,
  not just average FPS or short peak clocks.

Acceptance: repeatable improvements in retained captures or desktop latency,
with correct reference images, synchronization tests and no correctness/stability
regressions. Record cold-cache and warm-cache measurements separately.

## P4: shader compiler and rendering performance

- [ ] Establish per-workload shader cost profiles: instruction count, register
  pressure, spills, occupancy, memory traffic and compile time where observable.
- [ ] Investigate register allocation, instruction scheduling and expensive
  conversions using the slowest shaders from the retained captures.
- [ ] Optimize descriptor/constant access, redundant loads and memory access
  patterns with bounds, aliasing, barriers and sparse-residency rules preserved.
- [ ] Improve branch/divergence handling and stage interfaces where measurements
  demonstrate a cost. Preserve integer bits and floating-point requirements.
- [ ] Revisit texture/image and render-target layouts, compression, resolves and
  attachment traffic using explicit image comparisons and synchronization tests.
- [ ] Turn validated experimental optimizations into maintainable defaults;
  remove obsolete toggles only after both GPUs and relevant workloads pass.
- [ ] Keep chip-specific instruction encodings and capabilities explicit.
  Similar ISA does not establish identical instruction behavior or performance.
- [ ] Benchmark native Vulkan, DXVK/vkd3d captures and OpenGL through Zink.
  Keep application settings, resolution and frame selection fixed.
- [ ] Check normal application behavior after minimal replay validation. A replay
  improvement alone does not establish a stable desktop/game experience.

Acceptance: correctness matches retained references within declared tolerances;
CTS covers affected operations/stages; repeated timings improve without hidden
quality reductions, application-specific shortcuts or unexplained regressions.

## P5: shared implementation and everyday-system validation

### Shared code and capabilities

- [ ] Extend the prior shared-mechanism audit to error handling, progress,
  resource accounting, synchronization and submission helpers.
- [ ] Consolidate mechanisms that truly match; leave packet layouts, firmware
  contracts, calibration data and reset sequences separately described.
- [ ] Centralize capability predicates and associate each exposed feature with
  implementation and tests. Remove stale M4-only gates only after validation.
- [ ] Keep kernel/Mesa interfaces versioned and reject incompatible structures
  cleanly. Preserve supported combinations during coordinated changes.
- [ ] Improve clean-build reproducibility, dependency provenance and deployment
  receipts. Preserve firmware inputs and historical test oracles.

### Desktop, display and interoperability

- [ ] Repeatedly test internal-panel blank/unblank and brightness restoration,
  including interaction with external outputs and power-management firmware.
- [ ] Test HDMI and every USB-C port/orientation, repeated hotplug, two external
  displays, connector state removal and compositor mode changes.
- [ ] Test display wake, scaling/DPI, fullscreen transitions and multi-monitor
  presentation. Preserve the user's screen-blanking preference.
- [ ] Exercise dma-buf sharing and explicit synchronization among compositor,
  browsers, games and capture applications; check direct scanout where supported.
- [ ] Test OBS capture/recording, browser WebGL and concurrent GL/Vulkan workloads.
- [ ] Exercise suspend/resume as a separate capability project if pursued;
  do not silently enable sleep on these development machines.

### Continuous regression coverage

- [ ] Keep a fast per-change suite of previously broken CTS cases and minimal
  replays, then broader feature suites and scheduled full catalogs.
- [ ] Add error injection, low-memory, lifetime, concurrency and repeated reset
  tests; successful isolated rendering is insufficient for these contracts.
- [ ] Add reproducible long-duration desktop/queue/resource-churn campaigns,
  split into bounded monitored segments with checkpoints and explicit stop rules.
- [ ] Pin source/binaries, environment, firmware, settings and reference images;
  preserve indexed coverage and report unsupported cases separately.
- [ ] Publish local HTML results showing correctness, frame times, distribution
  changes and machine identity without mixing third-party and own trace suites.
- [ ] Detect performance regressions with repeated measurements and noise bands.
- [ ] Control evidence disk usage through retention policies and verified
  deduplication/compression; never silently discard first-fault evidence.

Acceptance: clean builds and reproducible tests on both machines; stable normal
login and display operation; regressions detected before deployment; failures
carry enough retained evidence for another developer to reproduce them.

## Work log

- 2026-09-27: Created this roadmap with all five improvement areas. Started P0
  by preserving the M3 firmware-fault segment and running an isolated guarded
  reproduction. M3 recovered via the existing watchdog and its normal display
  manager is active. M4's full catalog is complete; no new stress campaign has
  been started there by this roadmap task.

- 2026-09-27: Isolated M3 tessellation timeout reproduces with current and older
  Mesa; current M4 passes. Both captures contain the same 17 compiler-input
  hashes. Register-spill and no-scheduler diagnostics still fail; neither is a
  fix. The `wait` compiler flag does not affect the native AGX3 submit path and
  does not rule out synchronization bugs. Root cause remains open.
- 2026-09-27: First P1 implementation: M3 development commit `00f119c45501`,
  publishing-branch adaptation `58a9b568a9af`. Module SHA256
  `3b3aacb0ff561063a4ee5da6f02e038b8388776aeff2fcc945ac6f6f468537d3`.
  Real-fault validation now retains an unhealthy progress snapshot and reports
  `GPU runtime latched unhealthy`; the regression tool preserves its partial
  outcome without a Python traceback. This improves containment and evidence;
  it does not fix tessellation or implement GPU reset. Evidence is under
  `build/driver-roadmap-20260927/`.
- 2026-09-27: Follow-up validation: all 88 rendering smoke cases pass on the
  installed development module after watchdog recovery; the normal display
  manager is active. The publishing adaptation also builds with its own
  generated kernel/Rust dependencies. It is source-build validated separately
  from the development module's hardware results.
- 2026-09-27: Disk-pressure cleanup: cleared the regenerable uv download cache,
  losslessly compressed 1,494 inactive driver binaries and shared storage for
  607 byte-identical compressed archives. Each removed original was SHA-256
  verified against decompressed output. Sources, replay captures, fault logs and
  active builds were retained. Free space increased from roughly 12 to 50 GiB.
  Restoration instructions and per-file receipts:
  `build/driver-roadmap-20260927/ARCHIVED_BINARIES.md` and adjacent JSONL files.
