# M3 command-recording locality, 2026-09-22

A seven-second native 0 A.D. CPU profile (300 user-space samples) attributed
2.67% to command BO address lookup and 2.67% to getenv. Recording now caches
the last matching command BO, while retaining the same range and alias checks.
Submission is read-only, including simultaneous-use buffers. Reset invalidates
the pointer before pool BO recycling. Immutable profile/preamble/descriptor
trace options use the existing once-only option helpers.

Incremental arm64 build passes. Twenty pinned command-buffer CTS cases pass,
covering pool reset/reuse, implicit reset, secondary execution, simultaneous
use, repeated submission, push descriptors and multiple draws. The saved game
replay, 200 loops, retains the exact reference frame hash
 ec64e34c347316daea3738ef689e1ccc1c4a759f9b3d88a497df360a79c72a2c.
It completed in 4.318 seconds. Live Vulkan was 91.353 FPS versus the preceding
same sixteen-slot kernel sample's 92.367; no live speedup is established.
The change removes repeated CPU work; it does not resolve the dominant GPU
cost or establish the requested 120 FPS Vulkan target.

All experiments used the M3 60-second watchdog, five-channel 0.5-second thermal
monitor and 95 C stop. Profiler data, game captures and screenshots remain
private/outside Git. Build/replay/live evidence resides under
`build/m3-0ad-120/0ad-120-{cpu-hotpath-cts-a,cpu-hotpath-replay,vk-cpu-hotpath}`.
