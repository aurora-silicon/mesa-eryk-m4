# OpenGL 4.6 regression rerun, September 22

User requested OpenGL 4.6 again after the compiler optimization. Native release
Gallium and Vulkan were rebuilt from clean Mesa `9c41de45ddf` (including slot
optimization `6a6f211bc51`) and atomically installed with verified rollback
copies. Existing desktop processes retain their old mappings; each CTS and
private compositor process uses the fresh installed build. No kernel change,
reboot, desktop setting change or version override.

Campaign: workspace `build/gl46-regression-20260922`. The durable controller
PID1771326 is RUNNING at this launch checkpoint. Read controller.json for the
actual state. It verifies driver/CTS hashes and the armed/idle 60 s watchdog
before every segment and stops on a failed/incomplete run. Reports are saved
after segments and on exit. Never overwrite or silently retry an outcome.

Coverage: all 19,714 cases of the original frozen GL46 main catalog, identical
names/order/global indices; case-list SHA256
`e28bbbbfd0f6c8d711554a01aa45819bdc7ca963c9426e997dfd1daaeb1d7b17`.
Also repeat the 22 historical depth/viewport checks as distinct configurations.
64x64 seed1 Wayland EGL on a private 1000 Hz headless compositor for the main
list. Normal groups use four workers, copy-image groups two, and 16 previously
slow cases run alone (known duration issues or previously >=25 s). All selected
indices remain covered exactly once in the main list. The 60 s per-case limit
is unchanged; 42 segments are bounded to 900 s each, with existing thermal,
memory/progress and evidence guards. Scheduling changes do not renumber tests.

Comparison baseline is the previous GL46 final-report/latest-coverage.json,
which spans builds and explicitly recorded alternate configurations. The new
rerun pins a single installed build. Results are development regression
coverage; the main list is not the complete Khronos certification plan.

The earlier GLES campaign is preserved at 20,954 completed pairs on its old
build. Do not resume it on these newly installed binaries. No full GLES claim
or resolution of the two earlier rare GPU incidents is implied.

Installed hashes, exact plan, environment, installation records and controller
are retained here. Copy the controller into the workspace campaign directory
before use; the script resolves workspace paths from that location.
