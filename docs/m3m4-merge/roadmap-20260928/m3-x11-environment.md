# M3 Xwayland test environment repair

Six newly encountered XCB swapchain-creation tests failed with SURFACE_LOST.
The same run also marked presentation tests unsupported. The headless Weston
launcher mixed EGL from one installation, inherited GBM from another, and a
Vulkan-only LD_LIBRARY_PATH. GBM could not find its matching libgallium; Xwayland
disabled glamor and DRI3. This is a test-environment failure, not evidence of a
GPU command fault. The six original failures remain in the frozen campaign.

The launcher now selects one complete qualified EGL/GBM/Gallium prefix and
appends its library directory after the selected Vulkan prefix. It connects to
lazy Xwayland and requires DRI3 plus Present before CTS, and records hashes of
the compositor, Xwayland and GL stack. All six original failures pass with the
same current Vulkan binary/kernel. A 16-case original batch improves from
7 Pass / 6 NotSupported / 3 Fail to 13 Pass / 3 NotSupported.

Because missing presentation support also produced misleading skips, all 3,997
already-recorded XCB/Xlib cases are selected again in separate bounded batches.
That audit is in progress; original outcomes are not overwritten. Main M3
coverage is paused at 3,139,946 while auditing this prerequisite.
