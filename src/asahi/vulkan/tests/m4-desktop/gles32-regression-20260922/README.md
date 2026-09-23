# OpenGL ES 3.2 full regression, September 22–23

The full frozen catalog is complete: all 326,602 configuration/case pairs in
87 sessions have exact indexed results. Raw outcomes are 316,008 Pass, 10,552
NotSupported, 15 QualityWarning, 18 CompatibilityWarning, 8 Fail, and 1
Timeout. The eight failing cases and the timed-out case passed separate guarded
rechecks on the unchanged driver. The one new flush/wait timing warning also
passed its isolated recheck; its original warning remains in the raw result.

Three GPU incidents remain open: two tiling job timeouts (segments 22 and 143)
and one firmware stamp-order fault (segment 278). The affected cases and exact
parallel/serial batches passed after guarded recovery, but that does not fix or
explain the GPU faults. Segment 105's per-case point-mode timeout also passed
serially, with the original 60-second bound retained. Segments 42 and 43 hit
the 900-second workload bound; their unfinished cases passed in guarded
continuations at the same bounds. No driver or kernel patch was deployed during
this campaign. The mixed historical baseline is not a single full-build run.

`final/` contains the full raw results, attempts, summary, comparison, failure
list, incident list, reviews, isolated warning recheck, and an exact catalog
coverage validation. The original incident evidence is in `incident-0022/`,
`incident-0143/`, and `incident-0278/`. All 304 main guarded attempts stayed
below the provisional 120 C emergency cutoff (peak 109.75 C). The target was
left with the normal desktop active and the 60-second watchdog armed and idle.

## Launch provenance

User requested the full ES 3.2 run after GL46 validation. Workspace campaign
`build/gles32-regression-20260922`, launch controller PID1846849.

All 326,602 configuration/case pairs, 87 sessions, 298 guarded segments.
Frozen catalog is byte-identical to the preceding descriptor-fixed campaign;
all names, configurations and global indices are preserved. Start at index0
on the GL46-validated driver build. Earlier partial results remain historical.

Same installed Gallium/Vulkan built from clean Mesa9c41de45ddf, including
compiler optimization6a6f211bc51 and descriptor fix2da009f46e1. Exact library
and CTS hashes in installed-hashes.json, checked before every segment. Use
original CTS f6a2970: the separate GL sparse-clamp correction does not affect
ES cases. No rebuild, installation, kernel changes or reboot for this launch.

Private1000Hz headless compositor/X11 EGL, four GL workers, one EGL worker.
Known LOD-bias and tessellation-ordering cases are isolated before execution.
Fixed60s case bound,900s workload segments, mandatory60s watchdog,0.5s thermal
telemetry,95C startup gate,110C regulation target,120C emergency cutoff.
Controller also stops below2GiB target free space and verifies watchdog idle
between segments. Progress is saved after each segment and on exit.

Ordinary comparison failures are retained while remaining coverage proceeds.
A verified batch deadline may continue only unfinished cases under a fresh
guard at the same bounds. Crashes, per-case timeouts and GPU incidents stop
for review; no previous crash/recovery permissions are inherited. Do not
change installed libraries mid-campaign. Keep desktop/autologin enabled.

This full-plan development run is not a Khronos certification claim. Prior
rare GPU incidents remain unresolved until separately investigated; launching
a new campaign does not erase them. Controller copy here is provenance; it
resolves paths from the workspace campaign location before use.

Live monitoring: `python3 tools/gl-cts-plan.py status --campaign
build/gles32-regression-20260922 --live` now reports individual QPA outcomes
as well as completed50-case worker-group progress. Early groups can report
Pass0 for over a minute while individual cases are completing. This read-only
status improvement does not change execution, catalog selection or results.
