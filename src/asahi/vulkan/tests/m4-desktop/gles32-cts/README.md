# Full OpenGL ES 3.2 test campaign (2026-09-22)

The actual `cts-runner --type=es32 --summary` plan contains 326,602
configuration/case pairs across 87 sessions. It includes EGL, AOSP ES2/ES3/ES31,
KHR ES2/ES3/ES31/ES32, single-config shader cases, no-context tests, and ES
extension tests. Unlike the prior desktop GL main-list runs, every session
and prescribed EGL configuration in this plan is selected. The separate
CTS-Configs inventory is recorded in addition to these test pairs.

Source/binary baseline is upstream CTS f6a29701220f34dd1407513bfe80d74ca7b392ce
(v1.4.6.2). No sparse-clamp diagnostic patch is used for this ES campaign.
Renderer discovery reports native ES 3.2 and Zink/Honeykrisp Apple M4.
Evidence: 20260922T060322693223Z-gles32-headless-plan.

`gl-cts-plan.py freeze` retains exact ordered lists, hashes, per-configuration
arguments, global configuration/case indices, and 2,500-case segments. `run`
uses the existing watchdog/thermal/memory guard, with 60-second per-case bounds
and a 900-second segment bound. It stops on fatal or incomplete execution;
ordinary failed comparisons are retained and execution continues. No automatic
retry or failure baseline suppresses errors. `report` preserves each result,
its original global index, and the owning session (the same case under another
configuration is a separate test obligation).

EGL config IDs are enumerated under a private root Xwayland. Execution uses
that same X11 EGL backend on a private 1000 Hz headless Hyprland; window rules
allow exact CTS resizing. Every segment has a fresh compositor. The panel's
normal desktop remains running. ES version overrides are removed, not raised.
EGL tests run serially; GL/ES shader groups use four workers after qualification.
This does not change Vulkan WSI's one-worker-per-compositor requirement.

The runner cannot perform its renderer check on dEQP-EGL. Those segments first
run KHR-GLES32.info.renderer with the same binary/environment inside the guard,
verify its QPA against Apple M4, then execute the EGL list. Other groups retain
the runner's normal renderer check. There is no unchecked EGL execution.

Two harness setup errors before the first EGL batch (missing provenance field,
and unsupported deqp-runner EGL renderer-check option) started no CTS tests.
Their directories remain under segment 0000 as `run-setup-*`, with full evidence.
The result collector now records Unrun when setup never created runner.json.

Working evidence: `build/gles32-zink-20260922/full`. These are development
results, not a Khronos certification claim. Running state and final results
will be recorded here once the selected sessions complete.


All 15 CTS-Configs inventory checks pass. Their complete QPA and binary
hashes are retained here. The first two EGL partial-update failures and shader
failures are preserved; this task runs tests without changing the driver.

After inspecting the first AGX compiler-rejection/Zink `pipeline` assertion,
`--continue-compiler-aborts` may recognize that exact userspace failure again.
Each fatal attempt still stops all scheduling and saves its evidence. The
controller requires a process-only failure verdict, no GPU/display/thermal
fault, no OOM or memory-floor crossing, and the known assertion plus compiler
rejection in every aborting worker log. It records a per-attempt review before
starting a fresh guarded continuation. Only unrun/interrupted cases and
identified SIGKILL-canceled peers are resumed; the SIGABRT test remains Crash.
Unknown assertions, timeouts, GPU faults, sensor failures, memory pressure and
setup errors stop the controller for inspection. No failed test is retried.
Raw attempts and interpreted latest coverage are separate report files.

Checkpoint: 50,954/326,602 pairs completed; 49,021 Pass, 1,662 NotSupported,
67 Fail, 194 compiler Crash, 8 QualityWarning, 2 CompatibilityWarning.
These compressed checkpoint files are partial results, not the final campaign.
The controller was briefly paused after segment 0020 to archive old
`/root/cts-bulk` staging files off the nearly full target filesystem using
checksum-verified rsync before removing source copies. The active staging
directory was excluded; driver/kernel/test evidence was preserved. Archive
and transfer log: `_provenance/target-staging-archive-20260922`.

The staging archive completed successfully (8,569,226,795 source bytes) and
the target had 5.7 GiB free before segment 0021 resumed. `cts-bulk.py` now
shares a content-addressed target runner binary, rechecks its SHA-256 before
every segment, and verifies uploaded bytes before installation. Each staged
runner path is a symlink; the backend still records the executed binary hash.
No driver, CTS binary, execution arguments, or protection policy changed.

Updated partial checkpoint: 100,357/326,602 configuration/case pairs completed;
97,686 Pass, 2,348 NotSupported, 70 Fail, 220 compiler Crash,
15 QualityWarning and 18 CompatibilityWarning. The last two 2,500-case
image-copy segments passed in full. Full suite execution remains in progress.

Segment 0042 reached its 900-second workload bound after 1,675 Pass, with
821 Unrun and 4 Interrupted. No per-case timeout/crash occurred; guard verdict
was process-only failure, peak 101.5 C, minimum available RAM 7,653 MiB, OOM
counter unchanged at zero. Evidence: 20260922T080335164169Z-gles32-plan-0042-r0.
After this review, `--continue-batch-deadlines` permits only the exact workload
deadline error with demonstrated progress, no fatal case results, no GPU/
display/thermal fault or memory-floor crossing. Original completed results
and attempts remain; only Unrun/Interrupted cases resume under fresh guards
with the unchanged 900-second segment and 60-second case limits.

The no-context session at segment 0046 exposed the same deqp-runner renderer-
check limitation as EGL: it exited before executing any case. The guarded
same-binary/environment ES renderer preflight now also covers KHR-NoContext,
with renderer identity QPA retained. This does not omit or bypass identity
validation. The original setup attempt is retained in `run-setup-renderer-check`
and evidence 20260922T085157948833Z-gles32-plan-0046-r0; its one case was Unrun.

The preflight also covers KHR-GLES2/3/31 (explicitly unsupported by the
runner renderer-check code) and KHR-GLESEXT (no usable renderer-info case).
Segment 0051 stopped before any test for that runner limitation; its original
setup evidence is preserved under `run-setup-renderer-check` and
20260922T085356254676Z-gles32-plan-0051-r0. The 6,053 KHR-Single-GLES32
subgroup cases are actual NotSupported outcomes because ES subgroup operations
are not advertised; these are retained as such, not counted as passes.

Segment 0054 produced one real 60.059-second timeout in
`KHR-GLES3.texture_lod_bias.texture_lod_bias_all`; its three SIGKILL peers were
canceled by the runner, not independent crashes. Evidence:
20260922T085939828724Z-gles32-plan-0054-r0. Guard found process-only failure,
peak 107 C, minimum available RAM 6,038 MiB and unchanged zero OOM count;
no glcts processes remained and the desktop was active.
`--continue-lod-bias-timeouts` recognizes only this exact timeout case with
the original 60-second bound, no other fatal status, no guard/memory fault,
and explicitly coordinated SIGKILL peers. The timeout is terminal and retained;
only canceled/interrupted/unrun cases continue after renderer preflight.
This is a recorded time-bound failure, not evidence of a GPU hang.

A second reviewed per-case timeout is
`KHR-GLES31.core.tessellation_shader.vertex.vertex_ordering` (60.059 s),
evidence 20260922T091109840068Z-gles32-plan-0056-r0. Guard: process-only
failure, peak 96 C, minimum available RAM 7,283 MiB, unchanged zero OOM.
Three coordinated SIGKILL peers are interruptions, not independent crashes.
`--continue-reviewed-timeout EXACT_CASE` now takes explicitly inspected case
names, retaining each timeout and applying all the same guard/memory checks;
unknown cases still stop. The LOD-bias flag remains an alias for its exact case.

The ES3.2 name of the same vertex-ordering test is separately reviewed:
`KHR-GLES32.core.tessellation_shader.vertex.vertex_ordering`, 60.057 s in
20260922T091826411006Z-gles32-plan-0058-r3. Process-only guard failure, peak
93.5 C, minimum available RAM 7,019 MiB, OOM unchanged zero. Its two canceled
SIGKILL peers remain interruptions. This exact case is explicitly listed in
the continuation command; no wildcard or blanket timeout retry is enabled.

A genuine GPU timeout occurred in 20260922T093255497822Z-gles32-plan-0065-r1
(job 4302223, retirement unproven). Guard peak was 102.75 C; evidence was
retrieved and the watchdog rebooted the M4. New boot:
1dcceb0f-e07c-4207-85fa-4f57a732c371; normal desktop restored. All four
interrupted cases passed serially in guarded recovery probe
20260922T093650172372Z-gles32-gpu-recovery-0065 (4 Pass, 9.74 s runner time).
Kernel and common binary hashes match. This does not identify the cause or
erase the original GPU incident. `incidents.json` preserves non-process guard
faults even if interrupted cases later finish. An exact `--recovery-review`
permits this one reviewed incident to resume; no general GPU-fault bypass.
Remaining cases in segment 0065 use one worker; subsequent segments retain
the original worker counts. Review/probe paths are in gpu-recovery-0065.json.

Worker qualification after recovery: identical eight previously passing
copy-conversion cases, same session-18 configuration, one test per process,
unchanged 60-second limit and guarded environment. All 8 Pass in both runs.
One worker: 27.807 s runner time, peak 88.75 C; four workers: 53.154 s,
peak 106.75 C. Evidence: 20260922T094432521733Z-gles32-worker-qualification-1
and 20260922T094504162780Z-gles32-worker-qualification-4. The test is small
and does not prove all case groups scale this way, but supports reducing
contention in the remaining Khronos sessions. From segment 0068 continuation
0003 onward, `--run-jobs 1` is explicit; frozen selections, configuration
arguments and indices stay unchanged, and each attempt records its worker
count. Earlier failures/timeouts and the GPU incident are not erased.
