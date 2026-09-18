# Standalone Factorio performance, 2026-09-18

Public official Linux x64 demo 2.0.77 (full standalone requires account access),
run directly through the installed FEX/muvm graphics wrapper, without Steam.
Archive SHA256: f2fc889de1924b551cf52ca2ce2c73251f9c20ee2ae38cc8e973415baffef2a1.
Installed under `/home/e/Games/factorio-demo-2.0.77`; isolated configuration and
saves under `/home/e/.local/share/factorio-standalone/demo-2.0.77`.
Launcher: `/home/e/.local/bin/factorio-standalone`, also in the desktop menu.
Guest graphics remain the R10 x86 Mesa overlay, not the native host libraries.

## Initial measurements, before optimization

* First initialization: 207.263 s, including an initial ~35 s sprite crop step.
  Sprite loading occupied 44.841–203.734 s; GPU compression was enabled and its
  self-test passed. VRAM 1117.04 MB. This run exposed a configuration-version
  prompt, so it is profiling evidence rather than a clean usable-menu result.
* Corrected valid configuration (same graphics preset, independent paths):
  169.474 s game initialization, 177.018 s observed launcher time including
  startup and final capture. Existing crop metadata avoids the first-run crop
  work; this is NOT a driver speedup. Menu visible, service remains active.
* Startup guest sampling: 74.28% idle, 16.17% PreloadWorker translated-code
  samples. Host sampling: about 78.8% idle, 3.6% systemd-journald. This phase
  differs from Steam's earlier compiler-heavy CPU profile.
* During the first sprite phase all-client kernel retirement logs recorded
  105,344 commands: sum preparation 8.434 s, active 119.686 s, recorded GPU
  span 37.704 s. Active includes host observation/firmware turnaround. Includes
  compositor work and does not establish exact utilization or causation.
  Investigate many-small-command overhead before blaming translation.
* User reports roughly 5 FPS in the menu. Instrumented frame-time/FPS
  qualification and optimization are still pending.

Both launches used the 60 s watchdog, six 0.5 s thermal channels, 95 C cutoff
and normal requested CPU maxima with CPU thermal throttling enabled. Peaks
65.25 and 66.5 C respectively. No quality reduction was made.

## Tools and evidence

Run the mirrored `factorio-standalone-observe.py` on the target under
`tools/gpu-experiment.py --seconds 520 --cpu-max-khz 4104000` with
`--output {output} --state <state> --launcher <launcher> --seconds 480`.
It refuses an existing VM, requires the armed watchdog, starts the ordinary
application in `m4-factorio-standalone.service`, retains logs and a screenshot,
and leaves it open only after initialization and liveness checks.
`factorio-profile.py OUTPUT [--toggle-fps]` samples an already active game
for 12 seconds under the same guard; it does not launch another game.

Full private provenance remains in `_provenance/experiments/`:

- `20260918T094424744162Z-factorio-standalone-demo-first-launch`
- `20260918T095204147117Z-factorio-standalone-demo-menu`

The first observer's direct SSH child disappeared after collection; the second
uses a service lifetime. The first minimal config lacked Factorio's version
marker; the second retains a valid version-13 config with only isolated paths
and windowed mode selected. Neither change is a rendering optimization.

## Fragment counter contention fixed

The FS statistics prolog issued one global atomic per covered pixel to the same
address. It now sums covered samples within the non-helper lanes of a subgroup
and elects the lowest active lane for one atomic. This preserves counts and
helper lifetimes; statistics remain enabled. The generic `elect` intrinsic was
not lowered in the secondary-prolog path (one retained compile assertion), so
selection explicitly uses the already supported subgroup minimum operation.

The CPU-checked GL microbenchmark verifies full 2560x1664 coverage, odd
2557x1661 edges, 17x13 and 1x1, five iterations each, statistics on/off.
Median warm fullscreen GPU query time: stats off 3.045 ms, stats on 7.569 ms;
with reduction off 2.936 ms, on 3.013 ms. Counts match exactly in all cases.
These timer results include the timestamp stream implementation overhead.
Original two-frame native Factorio replay has **zero differing pixels** in the
2560x1664 frame-2 image. Both use identical offscreen/rebinding adaptations.
The derivative/helper regression retains 121680 invocations and 0/116400 bad
atlas pixels. Targeted CTS: 35 Pass, 13 NotSupported, no failures, 1.822 s.

Evidence:
- `20260918T100550535469Z-factorio-statistics-baseline`
- `20260918T100632913525Z-factorio-statistics-reduce` (compile assertion)
- `20260918T100742115678Z-factorio-statistics-reduce-explicit`
- `20260918T100836716656Z-factorio-menu-stats-baseline-replay`
- `20260918T100908600687Z-factorio-menu-stats-reduce-replay`
- `20260918T101006395956Z-factorio-stats-reduce-helper-regression`
- `20260918T101033809360Z-factorio-stats-reduce-cts`

The running-menu baseline sampled 90.70% guest CPU idle. Roughly 12 render
passes plus 42 computes occurred per menu frame; a heavy render pass reached
36 ms. End-to-end 60 FPS and improved application startup remain pending.

## Real application and atlas cache

The x86_64 candidate runs via a private ICD under
`/home/e/.local/share/factorio-driver`; desktop/Steam libraries remain untouched.
First cache-producing launch with subgroup counters: 162.379 s. Then enabling
Factorio's supported `cache-sprite-atlas=true` allows the next initialization in
**12.372 s**, versus original 169.474 s (13.7x); same preset, compression,
resolution and 1117.04 MB reported VRAM. Cache file 1070486358 bytes.
The observer's 20.100 s elapsed includes VM startup, its final 3 s liveness
wait and screenshot/process collection. Do not present it as pure game time.
Most gain is the application cache, not a claim of faster cold GPU compression.
Original configuration is `config.before-performance.ini` in isolated state.

- `20260918T101130506324Z-factorio-candidate-create-atlas-cache`
- `20260918T101514148510Z-factorio-candidate-cached-start`

Live-game FPS qualification remains ongoing.

## Adjacent timestamp reset batching

EndCommandBuffer may merge consecutive reset-only compute streams, while
retaining the completion boundary before a timestamp producer or consumer.
Each reset formerly forced a separate firmware command even when followed by
another independent reset. The memory-dependency and attachment checks remain.

`vulkan-query-copy-probe.c` mode 3 writes 64 queries, individually resets each,
rewrites even queries, and copies all results and availability. Even queries
wait for availability and must be nonzero and monotonic; odd queries must be
unavailable (their values are undefined). Passes on lavapipe, baseline and
candidate hardware. Frozen timestamp CTS: 908 Pass, 26 NotSupported, no failures,
28.187 seconds runner time. Original native frame 2: zero differing pixels.
This qualifies correctness; live FPS gains are not yet measured.

Evidence: `20260918T103448735616Z-factorio-reset-merge-candidate`,
`20260918T103511642334Z-factorio-reset-timestamp-cts`,
`20260918T103600276298Z-factorio-reset-merge-frame`.

A separate kernel polling experiment (eight 10us busy polls instead of four
sleeping polls) did not improve checked tiny-command turnaround: median
0.651644 ms baseline, 0.663076 ms candidate. Source, module and initramfs were
restored and the original kernel rebooted. `gpu-turnaround-probe.py` retains
this bounded, CPU-readback-checked experiment. Do not deploy the rejected build.

`factorio-ui-step.py` checks that the actual game window exists before input or
capture. Live guest perf sampling coincided with game exits; causation remains
unresolved. Avoid it for subsequent FPS qualification.

## Dynamically uniform sampling avoids reduction setup

Implicit texture footprints can remain inside the reduction branch when both
sampler and texture descriptors are dynamically uniform. Nonuniform descriptors
retain unconditional derivatives. This preserves all advertised sampling modes.
The native retained menu frame remains pixel-identical. All 2187 selected
texture-mipmap and GLSL derivative cases completed: 1929 Pass, 258 NotSupported,
no failures, 40.444 s runner time. This run does not replace specific min/max
reduction oracle coverage.

Full-resolution replay passes (same 36 renders, includes restored state) summed
111.43 ms recorded GPU spans before, 103.87 ms after; this is not live FPS.
Prior live menu observation after reset batching: 97 downsample passes in
12.173 s (~7.97 frames/s), workload still far from the 60 FPS target.

Evidence: `20260918T104819717843Z-factorio-reset-menu-timings`,
`20260918T105006719316Z-factorio-uniform-sampler-frame`,
`20260918T105817575885Z-factorio-uniform-sampler-cts`.

The user requested no more UI interaction: measure/optimize the main menu only.
`factorio-replay.py` stops our benchmark service and runs the retained trace
with optional compiler captures, without mouse/keyboard input.
`gpu-command-timings.py` summarizes serialized retirement logs by ASID/geometry;
GPU render spans include the interval between tiling and fragment work.

## Rejected broader shader specialization; metadata-query fix

A broader whole-body ordinary path for up to eight scalar 2D/3D samplers
was pixel-correct but regressed GPU time (138.74 ms, compared to uniform
baseline 103.87 ms across 36 full-size replay passes). Disabling GCM reduced
it to 96.03 ms, still no improvement over the simpler shader with GCM disabled
(93.19 ms). The broader specialization was removed, not deployed.

It exposed a generic NIR bug: sampler metadata queries were treated as
LOD-selecting instructions and received an invalid bias source. Only actual
sampling and LOD-query operations should receive sampler LOD bias.
The retained failure is `20260918T112433607365Z-factorio-multi-sampler-frame`;
corrected replay `20260918T113035994267Z-factorio-multi-sampler-bias-frame`
is pixel-identical. Six implicit sampler oracles (ordinary, custom border,
min/max, minimum LOD, combined border/minimum LOD) pass in
`20260918T113613570583Z-factorio-implicit-sampler-oracles`.
The generator now has `--implicit`; explicit-LOD remains its default.

`gpu-turnaround-probe.py` brackets the checked command sequence with
`counter-clock.c` readings. Interpolating the architected-counter/monotonic
offset avoids NTP drift across samples. Evidence:
`20260918T111317290086Z-factorio-turnaround-clock-correlation`.
Cross-domain clock offset uncertainty means this is diagnostic correlation,
not yet proof of exact firmware-versus-host latency attribution.

## Constant-program feasibility checkpoint (not enabled)

Appending a compiler-generated fragment preamble to the native Ioto program
is not qualified. Ordinary implicit sampling passed, but custom-border sampling
returned incorrect pixels. Exporting only 32-bit values avoids the first encoder
rejection; larger application shaders still require uniform writes outside the
compact encoding's range. The opt-in prototype was removed from the driver.
The diagnostic patch remains in the workspace build directory (it also contains
pre-existing native-Buoy context and must not be blindly applied).

Evidence: `20260918T115739889858Z-factorio-native-preamble32-oracles`,
`20260918T120456449395Z-factorio-native-preamble32-frame`,
`20260918T121529486376Z-factorio-preamble-border-ir`.

`factorio-standalone-observe.py` now avoids screenshots and secondary guest
process launches by default. `--observe-seconds N` observes the untouched menu
for a bounded interval and stops the benchmark service afterwards;
`--screenshot` explicitly requests a visual reference. This implements the
user's request to stop using UI interaction while optimizing the driver.

## Reuse the preceding render's timestamp

A timestamp immediately after EndRendering can use that completed VDM's
firmware timestamp, without launching an otherwise empty CDM. Only the actual
tail render with no existing timestamp is eligible, and no active compute
stream may intervene. Post-render availability writes and transfers retain
their order. The timestamp still measures hardware completion, not a CPU clock.

All 934 frozen timestamp cases: 908 Pass, 26 NotSupported, no failures,
27.168 s runner time. Retained Factorio frame: zero differing pixels. Bounded
untouched-menu observation improved from roughly 7.7 FPS to 8.4 FPS; this is
still far below the user's 60 FPS goal. The comparison uses the same quiet
kernel, graphics settings and private x86 Vulkan build apart from this change.
Evidence: `20260918T125509398864Z-factorio-tail-timestamp-cts`,
`20260918T125553028973Z-factorio-tail-timestamp-frame`,
`20260918T125401742537Z-factorio-quiet-menu-fps`,
`20260918T125705839090Z-factorio-tail-menu-fps`.

Actual FPS is available without input, overlays or screenshots using Zink's
`GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_DUMP_DIR=<directory>`.
Read the resulting `fps` file after the bounded observer stops the process.
This is application swap cadence; earlier geometry counts were inferred FPS.
The Vulkan overlay-layer attempt aborted during initialization, so it is not
used. The same driver without that layer started normally.

Follow-up constant-program diagnostic with explicit GPR copies of uniform
sources still failed custom-border pixels (`20260918T124424938263Z-factorio-preamble-gpr-oracles`).
That prototype was again removed. Register-demand, no-schedule and no-constant-
promotion variants showed no material replay improvement. Disabling the FS
counter prolog for diagnosis also showed no material improvement after the
subgroup reduction fix. Descriptor/UBO prefetch produced incorrect pixels and
remains disabled. None of these rejected variants is installed as a default.

A loop-only GCM heuristic was also rejected: the replay stayed pixel-identical
and all 2187 sampler/derivative cases retained 1929 Pass and 258 NotSupported,
but actual menu FPS fell from 8.43 to roughly 7.5. The retained Buoy compiler
input produced identical backend text after host-pointer normalization with
default versus forced GCM. Nevertheless, this was not a Factorio improvement,
so the default compiler policy remains unchanged. Evidence:
`20260918T130019041344Z-factorio-loop-gcm-frame`,
`20260918T130058834535Z-factorio-loop-sampler-cts`,
`20260918T130233565435Z-factorio-loop-menu-fps`.

The bounded observer accepts `--fps-log <HUD-directory>/fps` to retain fresh
samples automatically and summarize the final 12 samples in its result JSON.
It rejects stale or invalid sample files. No screenshot or input is involved.

## Native compute clears

The empty-render optimization still checked for an AGX2 precompiled kernel,
even though HAL200 now compiles libagx kernels through the native dispatch
path. Permit that compute clear, retaining a real VDM if a firmware timestamp
was already assigned to it. This avoids tiling/fragment setup for clear-only
passes and allows adjacent compute work to merge normally.

Validation exposed a null RGBA-packer call for D24 attachment views. Those
views use Z32_FLOAT backing (and a separate stencil plane). Depth/stencil
clears must pack each physical plane, not the logical attachment format.
The initial SIGSEGV and backtrace are retained; no GPU fault occurred.
After fixing this, all 483 selected clear cases completed: 475 Pass,
8 NotSupported. All 934 timestamp cases retained 908 Pass and 26 NotSupported.
The retained Factorio frame is pixel-identical. Untouched menu mean over the
last 12 HUD samples: **9.470 FPS** (9.123–9.691), versus **8.434 FPS** before.
The goal remains 60 FPS; no graphics settings or quality were reduced.

Evidence: `20260918T130549206801Z-factorio-compute-clear-frame`,
`20260918T130800406321Z-factorio-compute-clear-cts` (initial CPU crash),
`20260918T130957757014Z-factorio-compute-clear-depth-cts`,
`20260918T131132691470Z-factorio-compute-clear-timestamp-cts`,
`20260918T131239876715Z-factorio-compute-clear-menu-fps` (peak 70.25 C).

## Resource-loader completion-slot correction

The GPU0 resource-loader round waited on bit 1 because it incorrectly treated
register-source metadata (`data.value == 1`) as a scoreboard index. Native
LDDEV actually signals slot 1, whose wait bit is 0. Build the wait mask from
each load's returned `signal_slot`, including previously emitted loads in
a two-phase round. Register allocation must not choose completion slots.

With fragment descriptor prefetch explicitly enabled, this fixes all 32
failures in an 80-case combined-image-sampler descriptor-binding selection:
80 Pass after the change. The retained Factorio frame also becomes exactly
pixel-identical, from 2,108,616 differing pixels before. Core, native packer
and uniform-program unit tests pass, including the two-phase-round check.
Evidence: `20260918T132016065064Z-factorio-prefetch-sampler-bindings-cts`
(before), `20260918T132624096055Z-factorio-prefetch-wait-cts` (after),
`20260918T133030029070Z-factorio-prefetch-wait-frame`.

This is a correctness fix, not yet a qualified performance improvement.
The initial untouched-menu observation with fragment descriptor prefetch
measured 8.058 FPS, below the previous 9.470 FPS result. Prefetch remains
opt-in while the wait correction and prefetch cost are measured separately.
Evidence: `20260918T133129518087Z-factorio-prefetch-wait-menu-fps`.

## Rejected arithmetic-only constant program

The corrected resource wait does not alone qualify arbitrary constant-program
memory loads: custom-border sampling still fails with either uniform or GPR
load bases. Prefetched descriptors plus a native constant program pass all six
focused sampling oracles. Restricting hoisting to arithmetic over prefetched
uniforms also produces a pixel-identical Factorio frame. A larger preamble
requires appending directly to the bounded command allocation, rather than the
resource builder's 6144-byte staging array.

Nevertheless the arithmetic-only prototype fails 32 image-view minimum-LOD
cases (1897 Pass, 258 NotSupported out of 2187) and does not improve menu FPS
(initial observation 7.174). It was removed, including the proposed NIR
capability-filter callback. The experimental patch and compiler captures are
retained outside the deployed driver. The independent wide-uniform COPY
encoding fix remains, matched against the existing resource-loader fixture.

Evidence: `20260918T133635211344Z-factorio-prefetch-preamble-oracles`,
`20260918T134049312781Z-factorio-arithmetic-preamble-frame`,
`20260918T134129293516Z-factorio-arithmetic-preamble-sampler-cts`,
`20260918T134234141897Z-factorio-arithmetic-preamble-menu-fps`,
`20260918T134335098155Z-factorio-arithmetic-preamble-minlod-capture`.
Performance attribution remains under investigation; these short dynamic-menu
observations alone do not establish the cost of each changed instruction.


## Reuse a following render start for isolated timestamps

A native timestamp-only compute immediately before a render now uses the
render's tiling-start hardware timestamp. This preserves the serialized
ordering while dropping the dummy dispatch. Reset and memory-dependency
boundaries remain intact, and streams with appended work cannot be removed.
The optimization does not apply to the non-native submission backend.

934 timestamp cases: 908 Pass, 26 NotSupported, no failures. Retained Factorio
frame: zero differing pixels. Untouched menu observation: 7.503 FPS; this
short run does not establish an FPS improvement over the earlier 8.884 sample.
The current menu scene changes over time. End-to-end 60 FPS remains unresolved.

Evidence: `20260918T140326152890Z-factorio-timestamp-start-cts`,
`20260918T140648327070Z-factorio-timestamp-start-frame`,
`20260918T140755367963Z-factorio-timestamp-start-menu`.

The command timing parser now uses every-command SubmitTiming records rather
than only the last command in each packet. A 15-second baseline contained
5347 compute commands, median GPU execution 8.79 us but median host-observed
active duration 629 us. Render fragment work is also substantial. Separately,
a temporary x86 resource-loader profiler measured only about 12 ms of CPU
work per second across its two threads. That profiler was removed after use;
retained guest mappings confirm the intended private ICD was loaded.


## Zink query initialization and timer-copy batching

Zink allocates query slots monotonically, never reusing a slot in a pool.
Initialize a fresh pool once with the supported host-reset operation instead
of issuing a GPU reset for every slot. Remember that initial state for all
subsequent allocations, including maintenance9 reset-on-create pools.
Older devices without either facility retain the existing GPU-reset path.

A TIME_ELAPSED begin previously copied its start timestamp immediately when
outside a render pass. Mark the results pending and let ordinary query
synchronization copy the endpoints together. A flush between the begin and
end still preserves the start slot and its result.

CPU readback, query-buffer readback, empty occlusion, timers, and timers
spanning a flush pass for 520 iterations per mode on both lavapipe and M4.
This crosses the 500-slot pool boundary. The fragment-statistics benchmark
also retains exact covered-sample counts and checked edge pixels.
Hardware: `20260918T142512786638Z-factorio-zink-query-batching-checks`,
peak 63.25 C. All checks use private libraries; guest maps confirm the new
Gallium and intended native Vulkan driver are loaded under FEX.

Untouched main-menu observations with the same Vulkan ICD: host-reset only
8.722 FPS; adding timer batching 9.420 FPS (8.961–9.804). In the final
15-second timing window the latter executes about 30 compute commands per
frame, versus about 45 with the earlier guest Zink. These are short dynamic
scene observations, not a 60 FPS or long-run stability claim.
Evidence: `20260918T142125424648Z-factorio-zink-host-reset-menu`,
`20260918T142311507753Z-factorio-zink-timer-batch-menu`.

The attempted extension of timestamp absorption to compute was rejected.
A flush-only stream does not produce firmware engine timestamps. Requiring
an actual non-timestamp dispatch avoided that case but still timed out in
broader CTS. Both attempts were reverted; only the validated render-start
optimization remains. The guards retained evidence and recovered normal
SDDM/autologin after each timeout. Evidence:
`20260918T141347465541Z-factorio-timestamp-compute-start-cts`,
`20260918T141627894996Z-factorio-timestamp-compute-work-start-cts`.

## Alias metadata residency work

The strict-sparse pass was checking the residency of the driver's own alias
lookup directory and leaves. Follow only the exact buffer_alias_table root
field through two pointer levels to omit those checks. Application addresses
loaded from the leaves, and arbitrary pointers from descriptors/push constants,
still receive strict residency handling. Host tests cover both pointer chains
through four dereferences, in addition to the existing byte/page tests.

The retained Factorio frame is pixel-identical (zero differing pixels). The
heaviest pass falls from about 15.3 ms to 14 ms; this is not a 60 FPS result.
All 32 selected sparse-alias CTS cases pass. Evidence:
`20260918T143742040678Z-factorio-alias-metadata-frame`,
`20260918T143943993571Z-factorio-alias-metadata-cts`.

## Read CPU timer results without a temporary GPU copy

Zink now keeps timestamp slots as the source for CPU timer results. Pool slots
remain owned by each query and are not reused. CPU reads flush unsubmitted work
before calling GetQueryPoolResults; timer results requested in GL buffers use
the same synchronized result path. Query synchronization no longer eagerly
copies timestamps to temporary QBOs.

Six GL query modes pass 520 iterations each on lavapipe and M4, crossing the
500-slot pool boundary: immediate timestamps, elapsed time, empty occlusion,
timestamp QBOs, render-separated timestamps, and flush between elapsed-time
endpoints. The M4 fragment-statistics/pixel checks also pass. The untouched
menu averages 9.812 FPS (9.465–10.060); it is still far short of the target.
Evidence: `20260918T144831199903Z-factorio-zink-cpu-timer-checks`,
`20260918T144925992674Z-factorio-zink-cpu-timer-menu`.

The direct CPU result path also covers single pipeline-statistics queries.
Contiguous retained slots are read together and accumulated with the existing
per-start conversion (including line-loop vertex counts). Eager QBO copies
remain for other query types. The extended M4 probe checks statistics across
a flush and GL query-buffer output, plus the existing exact edge/pixel cases;
all pass. The untouched menu reaches 10.950 FPS (10.840–11.160).
Evidence: `20260918T145301857201Z-factorio-zink-cpu-statistics-checks`,
`20260918T145341775521Z-factorio-zink-cpu-statistics-menu`.

## Opt-in direct UBO prefetch with sparse fallback

The old opt-in UBO matcher ran after alias/residency lowering and matched no
Factorio loads. For fragment prefetch, defer those lowerings until immutable
UBO descriptors have been recognized. Each prefetched scalar has a shared
per-descriptor eligibility uniform. The encoder selects only ordinary primary
addresses; sparse/capture-replay UBOs use their read-only shadow address and
retain the original shader access with execution-time alias/residency checks.
The CPU reads descriptor metadata only, never UBO contents or sparse mappings.
Update-after-bind, partial and dynamic bindings remain on the original path.

Host NIR tests check descriptor matching before/after uniform lowering and
that direct/sparse branches retain the appropriate memory access. Initial
hardware UBO binding selection: 80 Pass. Final shared-eligibility variant:
1,929 Pass / 258 NotSupported sampling cases, no failures. The original frame
is pixel-identical. Its heaviest pass is now 8.7–9.0 ms versus 15.3 ms originally.

The first version alone averaged 8.618 FPS, so it was not enabled by default.
With shared eligibility and direct CPU timer/statistics results, the untouched
menu averages 13.625 FPS (12.950–14.360). This remains opt-in through
HK_M4_PREFETCH_UBOS=true; sparse fallback has host coverage but still needs a
focused fragment UBO sparse-rebinding hardware oracle before default enablement.

Evidence: `20260918T144332924568Z-factorio-ubo-direct-frame`,
`20260918T144424364714Z-factorio-zink-ubo-direct-menu`,
`20260918T144550856932Z-factorio-ubo-direct-cts`,
`20260918T145519391560Z-factorio-ubo-shared-direct-frame`,
`20260918T145640928251Z-factorio-ubo-shared-sampler-cts`,
`20260918T145807777922Z-factorio-zink-stats-ubo-menu`.

## Integer-only native constant-program experiment

Restricting the native preamble to integer arithmetic over prefetched uniforms
fixes the known minimum-LOD failure of the earlier floating-point prototype.
The retained Factorio frame is pixel-identical; its heaviest pass falls to
7.45–7.48 ms. All 1,929 supported cases in the 2,187-case sampling selection and
all six independent implicit sampler oracles pass.

It still regresses live-menu performance: 9.519 FPS with broad descriptor
prefetch, and 12.581 FPS when restricted to shaders with at least sixteen
prefetched UBO scalars, versus 13.625 FPS without the preamble. These are short
dynamic-menu observations, not identical fixed-scene benchmarks. Remove the
prototype and its NIR capability callback; retain the clean experimental patch
outside the deployed driver for later setup/constant-program diagnosis.

Evidence: `20260918T150203174312Z-factorio-int-preamble-minlod-cts`,
`20260918T150242136212Z-factorio-integer-preamble-frame`,
`20260918T150322355929Z-factorio-integer-preamble-sampler-cts`,
`20260918T150522105240Z-factorio-integer-preamble-oracles`,
`20260918T150600508842Z-factorio-zink-integer-preamble-menu`,
`20260918T151130315582Z-factorio-zink-integer-large-menu`.

## Native graphics-query availability token (opt-in)

With HK_M4_QUERY_COMPLETION=true, graphics-only pipeline-statistics pools use
64-bit availability cells registered for firmware timestamp writes. EndQuery
can use an unoccupied render completion timestamp instead of submitting a
compute fill for availability. Empty queries, occupied timestamp slots and
intervening post-graphics work retain the ordered fill path. GPU resets retain
the timestamp cache-ownership boundary; host reads and GPU result copies test
all 64 bits so a zero low counter word cannot look unavailable.

The retained original frame is pixel-identical. All 906 supported cases in the
907-case fragment-statistics/multiple-query selection pass (one NotSupported),
including reset and secondary-command variants. The GL probe checks exact
counts/pixels with both statistics-first and timer-first completion ordering.
Quiet-kernel short untouched-menu means are 11.032 FPS without and 11.314 FPS
with this optimization. These dynamic-scene observations do not establish a
large performance gain; 60 FPS remains unresolved. The change stays opt-in.

Evidence: `20260918T152130828801Z-factorio-quiet-stats-ubo-menu`,
`20260918T152658631699Z-factorio-query-completion-checks`,
`20260918T152736545885Z-factorio-query-completion-cts`,
`20260918T152801354056Z-factorio-query-completion-frame`,
`20260918T152904536029Z-factorio-query-completion-full-cts`,
`20260918T152936687288Z-factorio-query-completion-order-checks`,
`20260918T153021694520Z-factorio-query-completion-menu`.

## Follow-up cost isolation

After UBO prefetch, disabling code motion still does not improve the short menu
observation (10.733 FPS). Diagnostic-only suppression of min/max and border
emulation yields 11.122 FPS; it is not deployed or counted as a correct driver.
Thus the current evidence does not justify building a new sampler variant
system to claim a large end-to-end gain. The cloud shader's offline profile is
7,426 instructions / 187 register halves / no spills with GCM, versus 8,284 /
111 / no spills without it. Static shader size alone does not identify the
executed cost of the fallback branches.

The guest observer now retains per-thread CPU deltas and can collect a bounded
8-second perf sample after initialization, with no screenshots or input. In
`20260918T153903118494Z-factorio-guest-driver-profile`, the last ten seconds
show 17.5% of one core in factorio:gdrv0, 6.76% in the main FEX thread and 1.59%
in factorio:zfq0. The first two are sampled waiting on futexes and the last on
a DRM sync object. This does not support a saturated CPU thread as the current
FPS limit. The recorded sampler/clock-isolation experiments are
`20260918T153346304859Z-factorio-prefetch-no-gcm-menu` and
`20260918T153649299650Z-factorio-prefetch-sampler-cost-menu`.

The optional kernel SubmitSummary flag (bit 24, debug_flags=16777216) adds
one exact-retirement aggregate per second without changing execution policy.
Use `gpu-summary-timings.py LOG --tail-seconds 20` to analyze complete intervals.
The currently installed 7.1.9-dirtyroom-asahi2+ kernel's Rust ABI is 1.94.1;
the old workspace 1.98.0 build recipe failed modpost and was corrected. No
unresolved symbols were suppressed and no incompatible module was installed.

## Grouped immutable UBO fallbacks

The opt-in UBO prefetch path now makes one direct/sparse choice per descriptor
and shares its fallback descriptor fetch. Only nonvolatile, reorderable reads
qualify. Bounds tests and execution-time sparse translation remain intact.
Host tests cover conditional-block reads and both direct and sparse branches,
plus rejection of volatile/non-reorderable accesses. The retained frame has
zero differing pixels and all 80 UBO binding cases pass. Untouched-menu
`20260918T161142362536Z-factorio-grouped-ubo-menu-x86` measured 11.478 FPS
(11.260–11.620), peak 64.75 C, versus 11.203 in the preceding same-kernel
sample. This small difference is within dynamic-scene uncertainty, not a
60 FPS result. The cloud shader shrinks from 7426 to 7218 instructions but
uses 12 bytes of scratch (previously none); further tuning remains necessary.
Evidence: `20260918T160748331245Z-factorio-grouped-ubo-frame`,
`20260918T160837606844Z-factorio-grouped-ubo-cts`,
`20260918T161110062641Z-factorio-grouped-ubo-compiler`.
An earlier live launch used an ARM library in the x86 guest and failed GLX
visual selection; that deployment error was corrected before this measurement.

## Fragment immutable constant tables

Native fragment preprocessing previously lowered large constant arrays to
per-invocation scratch. Extract them before scratch lowering and upload their
immutable blob once per shader bind, through a fragment-specific root pointer.
Lower variable copies first: the initial probe caught the large-constants
pass's explicit copy_deref prohibition, corrected before qualification.

`gl-fragment-constants-probe.c` passes all 8192 CPU-checked pixels over dynamic
indices and switching/reusing two 4 KiB tables. The retained Factorio frame is
pixel-identical; all 53 selected GLSL fragment-array CTS cases pass. The retained
compression shader's scratch falls from 4096 bytes with 1024 stores to a single
4-byte spill; code falls from 5749 to 5170 instructions. This is a compiler and
startup-path improvement, not proof of faster main-menu rendering. The live
menu sample measured 9.620 FPS (9.248–9.922), below the prior 11.478 sample;
cloud shader input is unchanged and per-render GPU spans average 2.490 ms versus
2.481 ms. Initialization was 12.894 s versus 13.403 s. Scene variation remains
uncontrolled and 60 FPS is still open.

Evidence: `20260918T161858416381Z-fragment-constants-copy-lowering`,
`20260918T161921650542Z-factorio-fs-constants-frame`,
`20260918T162002290908Z-factorio-fs-constants-cts`,
`20260918T162111183275Z-factorio-fs-constants-menu`.

## Separate executable for immutable ordinary UBOs

`HK_M4_PREFETCH_VARIANT=true`, together with `HK_M4_PREFETCH_UBOS=true`,
compiles an additional G16G fragment executable. Command recording examines
only immutable descriptor metadata; ordinary primary addresses select the
specialized executable, while sparse/capture-replay shadow addresses retain
the original GPU resolution path. Descriptor rebinding dirties the fragment
shader when eligibility changes. Contents are still read by the GPU. Dynamic,
update-after-bind and partially bound bindings retain the general path.
Shader binary version is 12 because the serialized fragment variant count and
metadata changed. This remains opt-in; experiments disable the shader cache.

Retained cloud: 7218 instructions / 191 half-registers / 12 scratch bytes
becomes 4016 / 143 / 0, with nominal compiler occupancy 512 -> 704 threads.
Original retained Factorio frame: zero differing pixels. The 80 pinned UBO
CTS cases all pass. `gl-ubo-variant-probe.c` verifies 16384 pixels across
16 draws alternating ordinary and sparse buffers and updating their contents.
The audit flag logs executable transitions independently of pixel checks.
Untouched live menu: 9.912 FPS (9.533–10.210), peak 64 C; the preceding
constant-array run was 9.620. These are changing scenes and do not establish
a controlled FPS improvement. The 60 FPS target is still unmet.

Evidence: `20260918T164314104861Z-factorio-ubo-variant-frame`,
`20260918T164438309470Z-factorio-ubo-variant-cts`,
`20260918T164537634001Z-factorio-ubo-variant-sparse`,
`20260918T164612396912Z-factorio-ubo-variant-menu`, and
`20260918T164937489205Z-factorio-ubo-variant-switch-audit`.

## Ordinary sampler executable and masked-field folding

`HK_M4_PLAIN_SAMPLERS=true` extends the opt-in UBO executable with up to eight
scalar combined-image-sampler bindings. Only immutable, fully bound descriptors
are eligible. Command recording checks weighted-average reduction, zero view
minimum LOD and absent custom border; other descriptors select the general
executable. The alternate NIR clears only those proven fields, preserving
native filtering, coordinates, mip bias, texture contents and all other state.
Descriptor binary metadata changes shader binary version to 13.

NIR previously retained `ubitfield_extract(value & constant, constant, constant)`
without exposing the known-zero extraction. Distributing the constant mask
through that extraction allows the branch and its six unreachable filtering
loops to disappear. Offline retained cloud compilation falls to 229 instructions,
55 half-registers, no scratch and no loops (general: 7218/191/12 bytes/6 loops).
The final capture quota excludes shaders below 500 instructions, so this number
comes from offline recompilation of the retained pre-fold variant.

Validation: original Factorio frame zero differing pixels; 16384 CPU-checked
pixels across ordinary/custom-border rebinds pass; compiler predicate tests and
6038 NIR tests pass (9 existing disabled). Pinned combined-sampler CTS: 80 Pass.
Broader pinned uniform/sampler suite: 1929 Pass, 258 NotSupported, no failures,
49.68 seconds including setup, peak 65 C. These checks include shaders without
prefetched UBOs after allowing sampler-only specialized executables.

Untouched menu: 11.1125 FPS (10.780–11.410), peak 63 C, versus preceding
9.912 with UBO-only variant. Dynamic scenes prevent claiming a precise speedup.
The last 19.065 seconds recorded 2964 renders, 5.354 seconds of fragment spans,
1.961 seconds of render preparation, and 2744 compute commands with 3.263 seconds
of active spans. These are all-client aggregates, not hardware utilization.
The 60 FPS target remains unmet; production launcher has not been switched.

Evidence: `20260918T165417692361Z-factorio-plain-sampler-frame` (before fold),
`20260918T165821346087Z-factorio-plain-sampler-fold-frame`,
`20260918T165859883287Z-factorio-plain-sampler-switch`,
`20260918T165928629630Z-factorio-plain-sampler-menu`,
`20260918T170058261250Z-factorio-plain-sampler-cts`,
`20260918T170145386071Z-factorio-plain-sampler-wide-cts`.

## Compact UBO uniform allocation and loader ranges

Each prefetched scalar now reserves its two uniform halves; only the first
scalar of a descriptor reserves an additional eligibility word. Adjacent source
bytes and destination uniform halves merge into one Ioto loader range, after
independent bounds checks. Noncontiguous offsets and zero-page fallback reads
remain separate. This is still within the opt-in UBO prefetch path.

Host NIR checks pass. Retained frame: zero differing pixels; 80 pinned UBO CTS
cases Pass; 16384 CPU-checked pixels across ordinary/sparse UBO rebinds pass.
Untouched menu 10.8725 FPS (10.580–11.390), peak 63.75 C; no measured FPS gain
is claimed over the preceding 11.1–11.6 FPS varying-scene observations.
Evidence: `20260918T171041227691Z-factorio-compact-ubo-frame`,
`20260918T171134968760Z-factorio-compact-ubo-cts`,
`20260918T171159132769Z-factorio-compact-ubo-sparse`,
`20260918T171230299645Z-factorio-compact-ubo-menu`.

The preceding `20260918T170521385580Z-factorio-plain-sampler-profile` retained
a 14.604-second guest CPU sample: render thread 27.46% of one core, main thread
10.75%, submission thread 2.33%, with waits in futex/syncobj paths. This does
not indicate a saturated CPU rendering thread. No input or screenshot was used.

## Command-origin profile

`HK_COMMAND_PROFILE=true` logs per-control-stream API/dispatch counts, timestamp
flags and counts of native fill, timestamp-copy, query-copy and precompiled
kernels (including their generated enum ID). The counters survive CDM merging;
logging is opt-in and can affect timing under FEX.

`20260918T205151789387Z-factorio-command-origins` completed the untouched menu,
peak 64.25 C. A representative frame has five separate CDMs for seven duplicate
render timestamps, three timestamp-only CDMs and three compressed fast-clear
kernels (`LIBAGX_FAST_CLEAR_2`). No input or screenshots were used. This
identifies unnecessary command boundaries, not a 60 FPS result.

## Render-retirement timestamp copies (opt-in)

`HK_M4_TIMESTAMP_COPIES=true` uses the kernel's explicit feature bit 4 to
publish up to eight duplicates of a render's existing fragment-end timestamp
at exact retirement. Each duplicate previously needed an internal compute
shader. Additional copies retain the shader fallback. Query values and timing
boundaries are unchanged; this does not sample a CPU clock. Older kernels keep
the existing path.

`tools/gl-query-copy-probe.c` mode 6 writes twelve timestamps in a render pass,
reuses the queries across rounds and checks GPU query-buffer values against
CPU reads. It covers the eight-copy capacity and overflow fallback. The guarded check
`20260918T210254160016Z-factorio-retirement-copy-queries` confirms eight
retirement copies plus three shader copies, sixteen reused-query rounds all
passing. `20260918T210805216428Z-factorio-retirement-copies-frame` has zero
pixel differences. Menu `20260918T210320175462Z-factorio-retirement-copies-menu`
averages 11.902 FPS (11.51–12.37), peak 62.75 C; no 60 FPS claim.

## Ioto validation-only path

FEX block naming in `20260918T211008616326Z-factorio-driver-block-profile`
resolves steady-state samples to original Mesa function entries: 19 of 54
Honeykrisp samples are in record validation/output clearing. Add an exact-size
validation predicate for callers that only check a generated record, retaining
the same target and fixed-bit grammar without constructing an unused decoded
copy. State and preshader packers copy the validated original bytes.

All five existing Ioto encoding/state/load/preshader/program host suites pass.
The encoding mutation oracle also compares the new predicate, including exact
length rejection. `20260918T211806963591Z-factorio-ioto-check-frame` matches the
retained Factorio frame pixel-for-pixel. The uncached menu observation
`20260918T211610888594Z-factorio-ioto-check-menu` averaged 10.620 FPS, with one
3.198 FPS sample and otherwise up to 11.98; this does not demonstrate an FPS gain.
Future performance observations enable a separate cache keyed by library hash
and the chosen experimental compiler options; replay validation can disable it.

## Timestamp copies after a closed render pass

An occupied tail VDM timestamp can now fan out after EndRendering when no
compute or post-graphics work intervenes, within the existing eight-copy
capacity. Full capacity retains separate hardware timestamp commands. A
statistics query can also use an occupied render completion for its nonzero
64-bit availability cell. Both paths require HK_M4_TIMESTAMP_COPIES and the
kernel feature; query values remain firmware timestamps.

The Vulkan query probe's mode 4 writes one timestamp inside a clear render
and eleven afterward, then copies results on the GPU and compares CPU reads.
`20260918T213528617593Z-factorio-tail-copies-queries` confirms eight retirement
copies, three overflow commands and monotonic matching values.
`20260918T213741536790Z-factorio-tail-copy-statistics` checks exact fragment
counts and edge pixels across query ordering, query-buffer reads and flushes;
its command log confirms use of the shared completion timestamp.
`20260918T213802226186Z-factorio-tail-copies-frame` matches the retained Factorio
frame with zero differing pixels. No broad CTS run or 60 FPS claim.

## Metadata-only decoding while validating generated programs

`20260918T214757636030Z-factorio-tail-copies-cpu-profile` has 107 Honeykrisp
block-start CPU samples, including 17 in load-round construction, 13 in
fixed-prefix matching and 10 in decoded-output clearing. The retained exact
x86 driver and `fex-jit-summary.py` make attribution reproducible. This is CPU
sampling; the profile run itself averaged 12.522 FPS, not 60 FPS.

The new decode_kind entry point shares the original complete/ambiguous/truncated
record matcher but returns only kind and length. Program validators and the
copy-suffix scanner use it, avoiding unused byte copies and output clearing.
The existing mutation oracle compares both APIs, including failure outputs:
35,558,556 encoding checks and 857 program checks pass. Both target builds pass.
`20260918T215749291125Z-factorio-ioto-header-frame` has zero differing pixels
against the retained Factorio reference, peak 63 C. Broad CTS remains deferred.

## Reusable uniform binding templates

The generated uniform body now carries its final COPY boundary from the load
builder. The typed graphics composer uses the already checked body directly;
generic byte-stream composers retain their decoder checks. Only emitted bytes
are copied to the output, avoiding full 6 KiB temporary clearing/copying per draw.

`HK_M4_IOTO_TEMPLATES=true` enables two CPU-only templates per command buffer
(vertex/fragment), retained across resets and freed with the command buffer.
Templates contain the generic load/copy sequence and address relocations, not
application shaders. A hit checks uniform layout, target, register policy,
shared-base address relationships and immediate instruction lengths. It patches
fresh addresses; misses regenerate normally. It neither caches UBO contents nor
keeps old GPU allocation ownership. Shader BO retention remains unchanged.

19,267 targeted uniform-builder/template checks pass: independent COPY-boundary
decoding, validated-vs-typed composition, both instruction families, immediate
length changes, 32-bit address carry, allocation rollovers, and cross-range base
reuse/invalidation. Load-core (211) and generic program (857) checks also pass.
The larger Honeykrisp byte-fixture suite stops on its expected NOP mask 0x08 at
line 866; the existing packer emits 0x04. Do not count that suite as passing.
Full CTS is deferred by user request.

`20260918T221244538640Z-factorio-ioto-generated-frame` and
`20260918T221819314733Z-factorio-ioto-template-frame` both have zero differing
pixels against the retained Factorio frame. The menu observations average
12.877 FPS without templates (`20260918T221319618893Z-factorio-ioto-generated-menu`)
and 15.013 FPS with templates (`20260918T221843718916Z-factorio-ioto-template-menu`,
14.13–15.43, peak 63.25 C). These are separate animated scenes/cache states,
not a controlled speedup ratio or 60 FPS result. The warmed CPU profile is
`20260918T221950369905Z-factorio-ioto-template-profile`.

Kernel f96e39d3e06b additionally wakes completion processing from the RTKit IRQ
callback. Its 2048-readback probe measures 0.632 ms/command median versus
0.658 ms previously. Same-submission scheduler handoffs average only 16–17 us;
`gpu-summary-timings.py` reports them separately from preparation and active time.
