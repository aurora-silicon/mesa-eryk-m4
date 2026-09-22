# Fresh OpenGL ES 3.2 regression, September 22

User requested the full ES 3.2 run after GL46 validation. Workspace campaign
`build/gles32-regression-20260922`, launch controller PID1846849. RUNNING at
this launch checkpoint; consult controller.json and summary.json in the
workspace for actual progress. Do not start concurrent hardware experiments.

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
