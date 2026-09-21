# OpenGL 3.3 on Zink


`gl-cts-probe.py GUARD_OUTPUT` runs on the target under `gpu-experiment.py`.
It records the installed desktop renderer environment, hashes the ARM64 GL CTS
binaries, runs `KHR-GL33.info.*`, and saves `cts-runner --type=gl33 --summary`.
The summary command normally exits 1 when there are no saved conformance
results; that is a plan-generation result, not six new driver failures.

The ARM64 `glcts` and `cts-runner` targets are built from the existing pinned
`reference/vk-gl-cts-1.4.6.2` checkout (`f6a29701220f34dd1407513bfe80d74ca7b392ce`).
Stage their `gl_cts`, `gles2`, `gles3`, and `gles31` data directories alongside
the binaries in `/root/gl-cts-1.4.6.2/`. The main GL33 must-pass list has 9,886
cases. Freeze its original order and hash before execution; this campaign uses
`build/gl33-zink-20260921/{cases.txt,catalog.json}`.

`gl33-cts-run.py --cases CASES --output RUN --name NAME --environment JSON`
uses the existing `cts-bulk.py` watchdog/thermal/memory guard and a private
1000 Hz headless Wayland compositor. The JSON is the probe's
`renderer-environment.json`: retain the production Zink/Honeykrisp settings,
including the desktop's existing GL 3.3 version limit. Renderer checks must
identify Apple M4, and the info QPA must identify Zink. This is headless EGL
Wayland coverage, not GLX/window-system certification.

The default is seed 1, 64x64 window surfaces. `--config 113` selects seed 2,
113x47. `--config fbo-width` and `--config fbo-height` select the seed-3
rgba8888d24s8 FBO configurations with one unspecified dimension. The plan also
contains the one-case GL30 no-default-context list. Keep results for each
configuration separate. A single configuration is not a full conformance
submission; a passing process guard is not proof that every CTS case passed.

`cts-bulk.py` and `cts-sample.py` accept repeatable
`--deqp-arg=--option=value` for these GL surface/context settings, forwarding
them unchanged and recording them in the runner manifest. Existing Vulkan
commands with no such arguments are unchanged. No skip list, failure baseline,
or automatic retry masks the first attempt.

`gl-cts-report.py --output REPORT RUN...` writes every outcome, separate
failure JSON and case list, and non-pass TSV. NotSupported and warnings stay
visible and are never converted to passes. Original QPAs remain in each run's
guarded evidence directory.


After qualifying the single-worker run, `--jobs 4` shares the private compositor
for KHR-GL/KHR-NoContext tests only. Vulkan WSI retains its one-worker restriction.
The existing 1.5 GiB available-memory floor still stops the whole segment.
A crash or timeout still stops first-attempt scheduling; save its original
result and start a new guarded segment containing only Unrun/Interrupted cases.
Do not silently retry a timeout or relabel SIGKILL peer crashes as driver bugs:
the runner kills its other workers during coordinated cancellation, and its
raw crash classification must retain that context.

Supply `gl-cts-report.py --catalog catalog.json ...` when combining segments of
the same configuration. It adds each case's original global index and writes
`coverage.json`, resolving unfinished records only with a subsequent completed
attempt. Repeated terminal attempts remain explicitly MultipleAttempts.


### Completed first configuration

The frozen 9,886-case main list finished with 9,554 Pass, 317 NotSupported,
11 Fail, three Timeout, and one genuine SIGABRT. The separate no-default-context
case also passed. About 1,013 seconds of test execution across bounded segments;
peak observed package-channel temperature 108.25 C. No kernel or driver changes
were made during this task. Afterward only the normal desktop compositor remained.

The SIGABRT is `KHR-GL33.transform_feedback2.transform_feedback2_states`,
Zink `zink_query.c:1118` asserting `q->started_in_rp == ctx->in_rp`. Two timeouts
are pipeline-statistics tests; the first logged an unsupported indexed-draw
command before waiting on the query. The other timeout is the sparse-buffer
copy test. Most ordinary failures are transform-feedback state/query tests;
texture LOD bias also failed. See failures-20260921.tsv for exact case names.

Six other raw Crash records were SIGKILL peers from coordinated cancellation.
Both affected segments kept oom_kill at zero. Their original records and the
explicit cancellation review remain retained; all six passed an isolated
follow-up. They are not counted as six independent driver crashes.
The earlier 100-case smoke test additionally failed
`KHR-GL33.transform_feedback.draw_xfb_stream_test`, which passed in the main
list: retain this as an extra order-dependent observation, not a fix or waiver.

This completes the main list at seed 1 and 64x64 using headless EGL Wayland.
The other size/seed/FBO configurations in the Khronos plan remain untested.
This result is not a passing full conformance submission.

Local full evidence and every original outcome are under
`build/gl33-zink-20260921/report/`; `resolved-results.json` maps the frozen case
indices to the final classification while preserving earlier attempts.
`results-20260921.json` records segment paths, hashes and cancellation review.
The no-default-context case uses gl-cts-probe.py GUARD_OUTPUT CASE after a
separate Zink info probe, since deqp-runner cannot perform its usual renderer
check for the KHR-NoContext package. Its direct QPA records Pass.

### Fix investigation tooling (2026-09-22)

`gl33-cts-run.py --renderer lavapipe` retains the same Zink build while
selecting the packaged software Vulkan ICD, to distinguish frontend query
bookkeeping from M4 execution bugs. Record that renderer separately.
`AGX_*` diagnostic variables are forwarded to the private test compositor too,
so startup shader compilation failures retain their reason.

The GL runner now explicitly enables QPA log flushing. The retained
`../deqp-runner-first-attempt.patch` honors an explicit log-flush option instead
of appending a conflicting disable option. This preserves the current case
when a GPU hang interrupts a process. It does not change the 60-second case
bound, shader/test code, skip lists or first-attempt cancellation behavior.
