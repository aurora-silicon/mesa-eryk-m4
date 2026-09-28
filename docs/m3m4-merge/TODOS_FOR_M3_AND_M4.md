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
- [x] Finish M3's 3,206,030-case frozen Linux catalog. Every index/name pair
  was verified against the pinned catalog. Original 31 Fail, one Crash, one
  Timeout and four QualityWarnings are retained; fixes/retests remain separate.
  The intermittent XCB audit timeout remains open.
- [ ] Eventually run a complete qualification campaign on a single final
  kernel/Mesa revision. The current campaign spans explicitly recorded fixes
  and uses an existing corrected CTS binary; it is not an official unmodified
  Khronos conformance submission.

## P0: diagnose the new M3 fault and finish correctness coverage

Status: timeout cause fixed; remaining coverage in progress. Evidence:
`build/cts-paired-20260926/m3-pte-fix/segments/2712954-2713022`.
The runner reported `float16.logical.opfordequal_vector_tesse` at catalog index
2713005. Firmware reported a render failure, no faults in the sampled MMU banks,
and a nonzero VDM state on one core. Isolation and a single-field firmware
experiment identify premature TA progress checking: valid full work takes
35.73 ms and passes with a finite 100-poll interval. See the recorded validation
in `docs/m3m4-merge/cts-fixes-20260927/m3-ta-progress-timeout.md`.

- [x] Reproduce the identified case in isolation with the exact failing binaries.
  Also fails with the pre-CTS-fix Mesa; current M4 passes the same case.
- [ ] If isolation passes, replay the original case sequence and investigate
  accumulated state, lifetime and allocation effects. Do not assume the last
  reported test caused the fault.
- [ ] Capture the relevant shaders, generated code, submission state and first
  firmware error. Compare against the M4 result and a known-good reference.
- [x] Fix the responsible firmware progress policy, retaining the exact CTS
  reproducer. Published kernel `cc3a3b104f91`; original 68-case segment and
  all 140 logical cases pass. Final quiet boot also passes 88 render smoke cases.
- [ ] Validate the original sequence, adjacent float16/tessellation cases and
  relevant game captures on both GPUs.
- [x] Resume only missing full-catalog indices after explicit evidence review.
  Kernel identity and review reason are recorded without rewriting old results.
- [x] Retain and explain allocation-callback QualityWarnings separately from
  failures; do not add dummy allocations simply to silence the tests.
  The four original records on each GPU and reference-counted DEVICE-scope
  allocation explanation are retained in `roadmap-20260928/allocation-quality-warnings.md`.

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

- 2026-09-27: P1 fence/lifetime follow-up: M4 `05cc062c9266` error-completes
  every admitted packet after global GPU failure while retaining all published
  DMA owners; 88 render smoke and 140 logical CTS cases pass on hardware. M3
  `52b06070669f` / published `6fb5196678f7` makes packet completion idempotent
  and ignores scheduler deadlines racing valid retirement; separate builds and
  concurrent callback tests pass. M3 deployment waits for an explicit full-CTS
  checkpoint, so these are not yet M3 hardware-validation claims.
- 2026-09-27: P1 evidence follow-up: M4 `d4a8867ff214` / `38ffd53dec64`, M3
  `6195f48a7970` / published `6664169b2271` extend first-fault capture to boot,
  idle/RTKit and publication errors; retain a versioned primary-errno record;
  bound M4 snapshots to 16 MiB; wake the worker on RTKit crash. Snapshot bounds,
  failed allocation/read rollback and old/new decoder checks pass. Hardware
  fault injection remains in progress. Ownership audit and evidence:
  `docs/m3m4-merge/roadmap-20260928/`. P1 reset and broad lifetime acceptance
  remain incomplete; error fencing does not prove stopped GPU DMA.

- 2026-09-27: Deliberate finite M4 compute timeout: Vulkan wait and subsequent
  submit returned DEVICE_LOST; device destruction returned; guard retained a
  1,970,952-byte snapshot with primary ETIMEDOUT before reboot. Normal desktop
  and 88 rendering smoke cases recovered. This validates containment/fallback,
  not a GPU reset or multiple simultaneous failed-packet fences.
- 2026-09-27: Shared host-only retirement accounting added to both sources
  (M4 `d92ca41aad31`, M3 development `01d0def130ab`, published `2378b7741f31`).
  M4's guard now uses the debugfs counter when available, with no fallback on
  malformed/unhealthy state. Summary logging disabled on its normal boot.
  A 65.19-second run passed 281 compute oracles, verified 283 global retirements,
  renewed the watchdog from those counters, and forced a slow kmsg reader to
  overrun the unchanged 128 KiB ring. Peak 60.75 C; desktop stayed active.
  M3 source refactor builds separately; existing full CTS remains uninterrupted.

- 2026-09-28: P2 checked GEM-size rounding (M4 `2631756846be`, published M3
  `a63738cc7818`) rejects zero/overflow before backing creation. Live M4 ioctl
  checks pass for five invalid and six small valid sizes; map/write/read/close
  returns current allocation counters exactly to baseline. Initial probe-only
  variable-shadowing failure is retained separately from the passing repeat.
- 2026-09-28: Shared allocation-extent/current/peak counters (M4 `ceb7e4cb0d1c`,
  M3 development `0984ca081cd1`, published `d027554bcafd`) follow GEM object
  lifetime and M3 coherent backing. Host churn/overflow/last-reference tests
  pass. M4 also passes 14 queued-work exits, 14 SIGKILLs and 15 intervening
  checked compute readbacks; measured live counts/bytes are unchanged after
  warmup. These are extent counters, not resident/pinned-page accounting.
- 2026-09-28: M3 full catalog checkpointed cleanly at 2,757,077 outcomes to
  validate accumulated P1/P2 changes on hardware. New development module
  `16a30679f5ba0b7bc85b5e017fea0c3202a3c6dfec41e47cd71729a9a8fee5da`
  boots with the normal display manager and healthy progress/accounting files.
  Regression validation is underway before a reviewed continuation.

- 2026-09-28: M3 GEM boundaries, 14 queued exits, 14 SIGKILLs and 15 checked
  readbacks pass with stable live allocation extents. Both GPUs pass failure
  injection at all eight observed Vulkan device-construction allocations.
  A deliberate M3 timeout returns DEVICE_LOST and retains the 478,843-byte
  primary-fault snapshot. Fixed both guards to check terminal GPU health before
  disarming; child exit zero cannot hide a latched failure. After watchdog
  recovery, 140 logical and 88 rendering cases pass. M3 catalog resumed from
  2,757,077 outcomes with explicit identity review. See `m3-memory-validation.md`.

- 2026-09-28: Reserved snapshot backing during healthy initialization (M4
  `63703468d95d`, M3 published `e9268a4002d9`). Serializer tests pass with
  allocation failure armed after reservation. M4 fault injection retains the
  full 1,970,952-byte dump and recovers its desktop; M3 deployment remains
  pending. Generic sysfs publication/mapping allocation remains a proof gap.
- 2026-09-28: Expanded allocation injection found a real shared Mesa leak:
  QueueWaitIdle lost its sync when signal-only submit allocation failed.
  Fixed in `ea84e595994`; both GPUs pass all 15 object/record/submit/idle failure
  points with zero callback-owned allocations and 88 rendering smoke cases.
  Deferred lost-device sync ownership remains separate from recoverable OOM.

- 2026-09-28: Shared dma-buf import rollback (`6b8dc9410c5`) now releases
  partial VA/fd/GEM ownership instead of leaking handles or aborting. Seven
  offline failure boundaries pass; each GPU passes 90 external-memory CTS
  cases plus six unsupported and 88 rendering cases.
- 2026-09-28: Fixed Vulkan memory allocation rollback after read-only alias
  failure, delayed imported-fd ownership transfer until final admission, and
  rejected page-rounding overflow. Explicit ioctl-boundary failure injection
  on both GPUs returns OOM, frees callback ownership and permits a subsequent
  checked GPU fill/readback. Final 184-case regression: 178 Pass, six unsupported
  per GPU. This does not inject allocation failure inside the kernel.

- 2026-09-28: M3 preallocated snapshot deployed and fault-tested; full dump is
  retained and the post-recovery 184-case selection passes (178 Pass, six NS).
  Both GPUs accept four packets before a forced finite compute timeout; all
  four exported Linux sync-file fences complete with errors independently of
  Vulkan's cached loss flag. QueueIdle observes DEVICE_LOST and destruction
  returns. The guard verdict remains fault/reboot, never rendering success.
  Fixed report classification to reject terminal progress errors even if logs
  roll over. GPU-only reset and proof of stopped DMA remain open.

- 2026-09-28: Command BO CPU-map failures now unwind ordinary/oversized ownership
  and record OOM before publication. Extracted-function failure tests pass; M4
  injected ordinary-pool failure returns OOM and same-device fill/readback works,
  followed by 88 passing render cases. Unrestricted injection also exposed shader
  upload failure being hidden as FEATURE_NOT_PRESENT; fixing that separately.

- 2026-09-28: Shader compilation/deserialization now propagates eager executable
  upload/link errors instead of caching an incomplete shader and later reporting
  FEATURE_NOT_PRESENT. M4 injected mapping OOM passes with and without disk cache,
  same-device retry succeeds, and 178 render/memory cases pass (six unsupported).
  Lazy graphics variants and complete memory-pressure qualification remain open.

- 2026-09-28: Final allocation binary 2e2f0cb06def passes mapping-failure/retry
  checks on both GPUs and 178 render/memory cases each (six unsupported). M3
  cached shader deserialization legitimately recovers by fresh compilation;
  retained the original too-strict probe failure and corrected only that warm
  cache expectation. Full M3 catalog resumed from 3,139,686 immutable outcomes.

- 2026-09-28: M3 encountered six XCB swapchain failures caused by the headless
  compositor's mixed EGL/GBM/Gallium paths disabling DRI3. Fixed the launcher,
  added DRI3/Present preflight and compositor-stack hashes; all six pass. Main
  catalog paused at 3,139,946 while separately auditing all 3,997 earlier XCB/Xlib
  outcomes, including misleading presentation-related NotSupported records.

- 2026-09-28: Added bounded read-only native timing counters on both GPUs;
  all three kernel builds and concurrent-reader test pass. Both deployed modules
  retain normal desktops and pass 178 render/memory CTS cases (six NS each).
  Exact-reference 4K 0 A.D. replay improves with experimental read-only preambles:
  M3 42.10 -> 58.80 FPS, M4 27.20 -> 38.47 FPS, three warm samples each. No
  default enablement yet; broader 14,741-case qualification running. Global
  retirement spans include overlapping work and are not utilization.
  See roadmap-20260928/timing-counters.md and retained comparison records.

- 2026-09-28: The complete 14,741-case preamble selection finished on each GPU:
  10,445 Pass and 4,296 NotSupported, no failures. Coverage is the frozen UBO,
  robustness.buffer_access and descriptor-copy families, with ordinary compiler
  defaults. M4 also passes sampler/custom-border executable switching and Boolean
  uniform readback probes. Default enablement still waits for broader replay
  correctness; Satisfactory currently has small image differences to investigate.
- 2026-09-28: Lazy graphics links now retain allocation errors and roll back failed
  cache admission. Both GPUs reproduce the old incorrect FEATURE_NOT_PRESENT,
  return OUT_OF_HOST_MEMORY with the fix, then render/read back 4,096 values on
  the same device. Each also passes 178 rendering/memory cases plus six NS.
- 2026-09-28: M3 headless X11 audit retained all 3,997 indexed outcomes. Fixed Xlib
  multi-swapchain occlusion and reused-window-ID handling without changing CTS or
  deadlines; the original failing selection passes. The separate intermittent
  XCB acquisition timeout remains open despite 80 passing repeats. Full catalog
  continues with original non-pass records retained.
- 2026-09-28: Archived 9,112 older M4 experiment directories using checksum-verified
  transfer before removing target copies. Added trace-staging disk preflight and
  retained a 1-GiB reserve; successful replay cleanup only removes hash-verified
  target capture copies, preserving the complete local capture corpus.

- 2026-09-28: Shader-cache deserialization validates bounded payloads and executable
  ranges before allocating/uploading. Both GPUs pass all 5,521 frozen cache/binary
  cases (5,503 Pass, 18 NS each), plus host ASan/UBSan malformed-cache tests.
  Metadata validation beyond these ranges remains open. M4 DX12 preamble checks
  pass, but matched ordinary controls retain an unresolved one-pixel difference
  also reproduced on older Mesa; no tolerance changes or isolated speedup claim.
- 2026-09-28: Added an opt-in AGX3B constant-rematerialization experiment. All 123
  retained compiler inputs and 306 host tests pass; default hashes are unchanged.
  Sixteen shaders use fewer registers with varying instruction cost. Hardware
  performance and correctness qualification remains in progress.

- 2026-09-28: M4 constant-rematerialization experiment preserves exact Satisfactory
  images and eight complete register-heavy buffer oracles. Five timings per mode
  give 3.4385 vs 3.4548 FPS, an inconclusive 0.47% change within sample variation.
  Keep the option disabled by default; do not count this as a performance gain.
  Exact output-byte comparison confirms all 123 ordinary compiler outputs remain
  unchanged. The next performance gate broadens preamble replay qualification.

- 2026-09-28: M3 completed the frozen3,206,030-case catalog:1,574,713 Pass,
  1,631,280 NotSupported,31 Fail,one Crash,one Timeout,four QualityWarnings.
  Checked all ordered index/name pairs against the pinned catalog and retained
  database hash,original non-pass rows and approved revision identities. This
  completes indexed coverage, not single-revision qualification or the open
  intermittent XCB audit investigation.

- 2026-09-28: M3 completes rematerialization checks after the catalog: 178 Pass,
  six unsupported and eight full-buffer register-heavy oracles pass. Retained
  the initial wrong-device test setup failure separately. A complete shared
  Vulkan/Zink/EGL package passes 178 render/memory cases on each GPU with
  read-only preambles enabled (six NS each); broader game replays are running.

- 2026-09-28: Descriptor pools now initialize rollback state before allocation,
  reject size overflow and unwind CPU-map failure. Host failure tests and M3
  injected error/retry with 4096 fragment outputs pass, then 178 CTS cases (six
  NS). Broader preamble replays found one intermittent M3 SuperTuxKart mismatch;
  controls, preceding-workload replay and ten alternating cold-cache checks pass
  without reproducing it. Preserve the original failure and keep options opt-in.

- 2026-09-28: M4 also passes descriptor-pool mapping failure/retry, all 4096
  fragment outputs and 178 rendering/memory cases (six NS). The broader M4
  preamble campaign finishes all 19 selected replay modes with image checks
  and three timings each. M3 remaining games and matched slow-game controls
  continue; experimental compiler options remain off by default.

### 2026-09-28: Frostpunk preamble regression diagnosis

M3 original and cold-cache controls reproduce the memory-preamble image error.
Source bisection isolates one fragment; corrected source-lifetime, slot-reuse,
explicit-wait and full-serialization controls do not fix it. An initial stale
diagnostic object invalidated the earlier apparent wait successes; same-build
negative control exposed it and the corrected results are retained separately.
See `roadmap-20260928/frostpunk-preamble-investigation.md`. Preambles stay opt-in;
no per-shader workaround, tolerance relaxation or default enablement.

- Frostpunk follow-up: uniform-slot shifts, helper-marker and entry-control-flow
  controls do not repair the error. A duplicated-load comparison repairs the
  partial-motion boundary but not full motion; no production fix is claimed.
  Temporary diagnostics remain outside the shared compiler defaults.

- Frostpunk normal readback now finds 970 NaN pixels in the ordinary M3 path,
  versus finite outputs with the preamble; all other finite normals match.
  Software also gives finite values at those locations. Trace the originating
  normalization before declaring either hardware path correct or updating any
  reference. Original preamble qualification remains incomplete.

- Confirmed and fixed a shared M3/M4 helper-lifetime bug: the backfacing SR
  marker killed neighbouring lanes before derivatives. A 31x31 independent
  shader fails on both original drivers and passes on both corrected drivers;
  all 306 host compiler tests pass. Frostpunk normalization now agrees between
  ordinary and preamble compilation. Full replay/reference qualification is
  still in progress; the roadmap and preamble default remain incomplete.

- Helper correction qualification: both GPUs finish 2,058 targeted CTS cases
  (1,898 Pass,160 NS); M3 full Frostpunk replay matches with/without preambles.
  M4 full preamble replay exposes one unresolved watchdog recovery and, on a
  completed repeat, a localized final-frame mismatch. Preserve both incidents
  and keep preambles opt-in while diagnosing them; no tolerance relaxation.

- Helper-corrected compiler follow-up: matched Frostpunk medians improve
  1.663 to 2.433 FPS on M3 (+46.30%) and 0.762 to 1.236 FPS on M4 (+62.29%).
  M4 clocks vary under its unchanged power policy; these are guarded workload
  results. Offline isolated fragment cost drops from 9,518 to 3,960 main
  instructions and eliminates spills. Exact fresh-cache SuperTuxKart checks
  pass ten times on M3; rebuilt-package memory CTS gives 178 Pass / 6 NS.
  Satisfactory DX11 ordinary and optimized M3 images closely agree but differ
  from old references, so corrected M4 reference checks are in progress.
  Preambles remain opt-in; intermittent M4 failure causes and the roadmap
  acceptance criteria are still open.

- Matched helper-corrected Satisfactory DX11 gameplay medians improve
  2.036 to 3.486 FPS on M3 (+71.25%) and 1.322 to 2.061 on M4 (+55.92%).
  Each timed configuration has an exact image check; three samples follow a
  warmup. Retained timing counters point to fragment/compute savings, with
  little tiling change. Six complete M3 and three M4 optimized checks are
  exact, but the original localized M3 mismatch remains unresolved. The
  diagnostic shader bisection did not reproduce it. Preambles stay opt-in.

## 2026-09-28 compression and progress follow-up

Satisfactory compression diagnostics isolate a captured buffer/image alias-layout
issue; independent copy/sampling probes pass and detaching aliases restores exact
images. This adapted replay is not benchmark qualification. Two cold M3 controls
expose a retirement-only guard limitation during CPU pipeline work; qualification
and a safe idle-aware policy remain open. See
`roadmap-20260928/compression-capture-aliases.md`. No defaults changed.

### Idle/progress qualification follow-up

- [x] Add a separate shared host activity endpoint without changing completion
  ABI v1. Validate pending/idle transitions and eight exact mixed-facing probes
  on each GPU.
- [x] Fix the M3 guard's overwritten total deadline and qualify bounded CPU
  compilation against fresh, healthy GPU-idle evidence on both machines.
  Cold uncached Satisfactory DX11 checks complete with exact ordinary images;
  CPU activity while GPU work is pending cannot renew the watchdog. Preserve
  the failed process-group-only diagnostic. Details in
  `roadmap-20260928/idle-progress-validation.md`.

- [x] Preserve newer verified idle CPU progress when a submission starts; the
  retained completion timestamp must not replace it. Actual guard-branch tests
  reject the old assignment and retain the five-second pending stall limit.
  The previously interrupted shader-isolation segment now completes in 86.713 s.
  M3 published tools commit: `91e0e40a3a78`. Original interruption retained.


- [x] Make the M4 experiment guard recognize boost frequencies when deciding
  whether a requested CPU ceiling needs an explicit cap; retain all thermal
  and watchdog checks. Kernel/tools `7e9e787edea6`, 15 host tests and guarded
  4K GPU-written UBO validation pass. A separate boost-toggle/QoS refresh issue
  remains unqualified; rejected startup records are retained.
- [ ] Resolve the ten existing failures in the complete Meson `asahi` host
  suite (including register-bank SIGSEGV and minmax SIGABRT), with independent
  encoding expectations. Baseline and parallel-compiler candidate both give
  110 Pass/10 Fail; see `roadmap-20260928/parallel-compile-host-results.json`.

- [x] Repair two crashing host-test fixtures: initialize allocator constraint
  bitmaps and validate implicit-derivative placement for all four descriptor
  uniformity combinations. 260 allocator and 24 minmax cases pass. Eight of
  the original ten Meson host failures still need independent qualification.

- [x] Preserve parallel backend compilation for non-printing compiler flags and
  publish debug flags once without concurrent global writes. Eight-thread host
  compilation produces 128 byte-identical outputs. Exact cold full-capture
  pairs improve total Satisfactory DX11 replay time from 100.237 to 52.194 s
  on M3 and 133.989 to 75.632 s on M4. These are compilation/startup gains,
  not FPS claims; see `roadmap-20260928/parallel-compilation.md`. Source
  `d81a22084f4`; desktop Mesa stays unchanged.

- [x] Route native uniform exports through post-allocation discard hints.
  Two-export lifetime regression and all 219 backend packing tests pass;
  four-program 4K GPU-written uniform checks match 265,420,800 values per GPU.
  The separate memory-preamble game image discrepancy remains unresolved.


- [x] Replace two stale host-test contracts without regenerating instruction
  goldens: the opcode metadata test now checks all 18 explicitly reviewed
  mnemonic mappings instead of the obsolete count of eight. Unary encoding
  tests cover valid Gen9 half-registers 128/190 and reject source/destination
  192, while Gen10 still rejects 128; all 66 checks and the mapping test pass.
  The allocator already bounds Gen9 arithmetic to halves 0..191. Six of the
  original ten host-suite failures remain; these changes add no hardware
  instruction-support or performance claim.
- Experimental fragment preamble publication: bounded composition and late
  dependency release committed as `b36c4b1065a`. Both GPUs complete the same
  2,570-case regression selection: 2,262 Pass, 308 NotSupported, zero failures.
  M4 completes ten exact full Satisfactory replays; M3 completes nine exact
  then the previously observed two-pixel baseline difference. Three further
  M3 replays of the final size-hardened build are exact. Keep preambles opt-in,
  retain the baseline difference, and finish matched performance measurements.

- [x] Repair the stale live VDM packing fixture without changing its captured
  bytes: the direct indexed byte range must include the documented dwords-minus-
  one encoding introduced in `25c82a8422f`. Add independent size-rounding, empty
  range and first-index examples, and exercise M3 Pro and M4 target selection.
  This is host-test qualification only; five original host-suite failures remain.
