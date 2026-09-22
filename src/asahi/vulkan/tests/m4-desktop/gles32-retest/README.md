# Full GLES rerun after the September 22 fixes

This historical campaign is **STOPPED** at 48,454/326,602 completed:
46,782 Pass, 1,662 NotSupported, one Crash, eight QualityWarning and one
CompatibilityWarning; 278,148 Unrun. Do not resume it on a changed driver.
The allocator race is fixed in `2da009f46e1`; see
[descriptor race evidence](../zink-descriptor-race/README.md).
The separate fixed-build campaign is `build/gles32-descriptor-fixed-20260922`.
The original crash and exact-incident review below remain historical evidence;
that continuation permission does not apply to the new campaign.

The user requested a fresh full rerun. Campaign:
`build/gles32-retest-20260922`. Original results under
`build/gles32-zink-20260922/full` are untouched.

The same CTS binary, XML plan, per-session case lists, EGL configuration
arguments, environment, and all 326602 global configuration/case indices are
retained. `catalog-verification.json` records equality against the original
87 sessions (123165 unique case names). There are now 298 bounded segments.
The three proven long test names occur in 62 configurations and each gets its
own one-worker segment. Other GL segments use four workers; EGL uses one.
`gl-cts-plan.py freeze --serial-case EXACT_CASE` isolates them before execution;
this does not retry a timeout or change a result. Every frozen segment/index
was checked for exact coverage with no gaps, duplicates or changed case names.

The fixed Vulkan and Gallium hashes are recorded in the adjacent gles32-fixes
report. No driver fixes are planned during this rerun. Each segment has the
mandatory 60-second watchdog, all-channel thermal monitoring, memory guard,
60-second case limit, and 900-second workload bound. Reviewed workload-deadline
continuations retain completed results and only resume unfinished cases.
New fatal cases, GPU incidents, OOM or guard failures stop for review. Do not
silently retry a failure. A private 1000 Hz compositor provides synthetic WSI
pacing; the normal desktop remains active.

## Historical incident continuation (subsequently stopped)

Fresh EGL segment 0 completed 1366 Pass, 656 NotSupported, one Crash; 477
cases had not run when scheduling stopped. The crash at global index 1116 is
`dEQP-EGL.functional.sharing.gles2.multithread.simple.textures.copyteximage2d_texsubimage2d_render`.
The retained core shows glibc detecting heap corruption while
`eglTerminate -> zink_destroy_screen -> ralloc` frees the screen. This identifies
the detection site, not the corrupting writer. No driver change or case retry
was made. Guard verdict process_failure, peak 79.25 C, no OOM or memory-floor
crossing, same healthy boot and idle watchdog after collection.

The exact incident review retains the Crash terminal and resumes only Unrun
cases under a new guarded preflight. `--process-crash-review FILE` checks the
incident path, boot identity, exact crashing case set/SIGABRT, retained core
checksum, log signature and guard/memory evidence before allowing that
continuation. It cannot silently permit a different crash or new GPU incident.
The core, backtrace and review are retained here for follow-up after the rerun.
