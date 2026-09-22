# OpenGL 4.6 regression rerun, September 22

Completed the full frozen 19,714-case main catalog plus 22 historical alternate
configuration pairs on one installed Mesa build. No new driver regression was
found after reviewing the CTS-version and capability-gating differences.
No driver or kernel source changes were necessary during this rerun.

## Results and failure resolution

| Record | Pass | NotSupported | CompatibilityWarning | Fail |
| --- | ---: | ---: | ---: | ---: |
| Original CTS, all 19,736 executions | 18,093 | 1,636 | 1 | 6 |
| Original CTS, latest outcome per 19,714 cases | 18,092 | 1,615 | 1 | 6 |
| Corrected CTS, separate 1,914-case feature recheck | 1,914 | 0 | 0 | 0 |
| Latest outcomes including corrected CTS | 18,098 | 1,615 | 1 | 0 |

All six upstream failures are the same depth-texture setup defects already
fixed in CTS commit `de63a4ff063bb962218fd96273849acd549f7622`. The full rerun
intentionally used the original pinned binary. After the failures were reviewed,
the remaining main-list selections continued without retrying or overwriting
those failures. The separately named corrected binary then passed all 1,914
feature cases on the exact same installed driver. The six affected cases retain
57 successful function/mip comparisons across all six texture targets. Existing
fix and original validation: [CTS correction](../gl46-cts/optional-features/reference/README.md).

The corrected test skips unsupported sparse-format allocations, supplies valid
sizes for ordinary textures when sparse page sizes are absent, and uploads depth
values with TexSubImage instead of an invalid R16 image binding. It preserves
the ordinary depth-clamp checks and their expected results. No new patch was
needed: this rerun validates the already committed correction on current Mesa.
The original six Fail outcomes remain in report/, review-0038.json and the QPAs.

Compared with the latest historical corrected-CTS baseline, the only remaining
status changes are eight TextureParameterQueries cases: Pass -> NotSupported.
CTS gl4cSparseTextureTests.cpp:453 deliberately reports NotSupported when
GL_ARB_sparse_texture2 is present, even after its positive query checks pass.
The extension is present in the fresh info log. These are test-gating changes,
not lost sparse-texture support. The old tracking record retained earlier Pass
results because those eight cases were outside the optional-feature rerun.

`final-comparison.json` uses the latest historical baseline including the CTS
fix. The initial `comparison.json.gz` compared with the earlier baseline before
optional features (16,279 Pass); it is preserved as an intermediate comparison,
not the final regression conclusion. Every case retains its original index.

## Build, coverage and guards

Gallium and Vulkan were rebuilt from clean Mesa `9c41de45ddf`, including slot
optimization `6a6f211bc51`, and atomically installed with verified rollback
copies. Installed hashes are in installed-hashes.json and final-target-state.json.
Existing desktop processes retain old mappings; new CTS/private compositor
processes used the fresh libraries. No version override, kernel change or reboot.

Original CTS: f6a29701220f34dd1407513bfe80d74ca7b392ce, binary SHA256
b1b9630265dca8269fc34a3a062f801c259022055a56641bcf4d3fbbbc2a5070.
Corrected CTS: de63a4ff063bb962218fd96273849acd549f7622, separately named
/root/gl-cts-1.4.6.2/glcts-m4-sparse-clamp-0193725ec88f, binary SHA256
0193725ec88f5924619dadab32426a45012656162351753126d6a541cf585bc9.

Frozen main-list SHA256:
e28bbbbfd0f6c8d711554a01aa45819bdc7ca963c9426e997dfd1daaeb1d7b17.
Main configuration: 64x64 seed1, Wayland EGL/private 1000 Hz headless compositor.
Alternate checks: 18 D24S8 FBO cases and four 256x256 cases. All cases ran;
no renumbering or replacement of skips/failures. Four ordinary workers, two for
copy-image groups, and 16 previously slow cases isolated. Fifty cases per worker
group means selections below 50 used one process even when jobs=2. Forty-two
900-second-bounded segments, unchanged 60-second per-case timeout; corrected
feature recheck bounded to 300 seconds with two workers.

All 43 experiment guards passed with mandatory 60-second watchdog and 0.5-second
thermal telemetry. Main peak 108.25 C; corrected selection peak 78 C. No CTS
crashes/timeouts or kernel fault/hang events in these runs. kernel-review.json
excludes routine completion-wait counter telemetry, not actual job timeouts.
Guard results, thermal logs, kernel live logs, binary/environment manifests and
original/corrected six-case QPAs are retained with checksums.

Main campaign ran 14:37:22-15:32:50 UTC, including review/continuation time;
summed segment wall time 3,186.45 s. The 324 copy-image cases all passed and took
1,819.79 s across their segments. These cases rebuild small textures, perform
many copies and synchronous glGetTexImage readbacks, and compare pixels on the
CPU within each named case. Conservative worker counts also increased elapsed
time. This is not a GL-versus-Vulkan throughput benchmark; profiling would be
needed to attribute time between CTS, Zink, kernel submission and GPU execution.

Final target check 15:36:38 UTC: watchdog armed/idle60, normal Hyprland PID771
only, display manager active, persistent user-e autologin intact, no CTS/private
compositor remaining. Same boot and library hashes. All commits local, no push.
The earlier GLES campaign remains historical at 20,954 completed; do not resume
it with these different installed binaries. Its two earlier rare GPU incidents
remain unresolved. This main-list development run is not Khronos certification.

Workspace campaign: build/gl46-regression-20260922. Helper scripts resolve paths
from that directory; retained copies here are provenance, not directly runnable.
