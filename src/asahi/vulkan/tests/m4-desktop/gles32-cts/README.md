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
