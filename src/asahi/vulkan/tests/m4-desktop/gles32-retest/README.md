# Full GLES rerun after the September 22 fixes

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
