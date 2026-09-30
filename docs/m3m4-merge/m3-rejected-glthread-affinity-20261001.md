# Rejected OpenGL command worker placement

The existing Zink recording/submission policy leaves Mesa's GLThread worker
under ordinary scheduling. A prototype Gallium preference steered only that
worker to the highest-capacity allowed CPUs, reusing the same affinity helper.
It left the caller and the already-qualified Zink worker policy unchanged.

Both variants produced exact first/middle/last Minecraft images. The resource
oracle passed all 48 cases and checked actual thread affinity after rendering:
the caller retained all 11 CPUs, recording/submission workers used five, and
GLThread used 11 with the option off and five with it on.

Nine alternating pairs initially gave 76.20 to 80.37 replay FPS (+5.48%). A
fresh nine-pair repeat gave 80.84 to 81.01 (+0.22%); its mean fell from 79.67
to 79.61 FPS. The apparent initial gain is not repeatable. The prototype and
its experimental oracle extensions were removed, leaving the qualified normal
desktop and merged driver unchanged. The companion JSON and local benchmark
diagnostics retain all samples, build/source identities and guard receipts.

These are 30-frame replay timings, not live-game FPS. No rendering calls,
settings, GPU clocks or references changed. All hardware work used the existing
watchdog and thermal guards; there was no GPU fault or reboot.
