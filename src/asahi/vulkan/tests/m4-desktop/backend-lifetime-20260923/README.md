# M4 external-monitor disconnect: compositor backend lifetime

With merged Mesa selected at `/opt/asahi-m3m4-e325b646b16`, disconnecting the
USB-C HDMI adapter aborted Hyprland during framebuffer allocation. Packaged
Aquamarine had terminated the primary renderer's shared EGLDisplay during
secondary renderer teardown (EGL_BAD_DISPLAY before the assertion). HDMI
returned, but Hyprland's supervisor relaunched in safe mode.

The existing Aquamarine fix 9143476 was present only in the old Mesa directory.
The installed priority launcher restored the newly selected Mesa directory,
bypassing that backend. The updated launcher restores the fixed root-owned
`/opt/m4-hyprland/lib` backend directory alongside the Mesa directory read from
root-owned configuration. It never restores a caller-controlled loader path.
The session shell also prepends this directory for launches without the
capability wrapper. Backend installation must precede restarting SDDM.

Qualified library: Aquamarine ec1c95b (includes 9143476), SHA-256
0126afd8035fd89e407d1ac7ba12cf77c0fe18266eafd3d528a940969b9ad88a.
Installed destination `/opt/m4-hyprland/lib/libaquamarine.so.13`, root:root 0755.
No Mesa rendering implementation changed here.

The isolated reproducer uses only renderD128 and the current Mesa environment.
The packaged backend reproduced primary renderer invalidation. The repaired
backend passed three secondary renderer destruction/pixel-readback cycles.
Guarded SDDM restart passed: normal Hyprland PID 7946, repaired library mapping,
both outputs active, ambient capabilities zero. Peak 64.75 C; all hardware
experiments retained the 60-second watchdog and six-channel 0.5 s telemetry.
The first restart check correctly failed because the shell-only path change
was discarded by the privileged launcher; updating the launcher resolved it.
Physical unplug/replug with this restored normal session is still pending.

Build the launcher with the matching aarch64 sysroot and -Wall -Wextra -Werror.
Install root-owned mode 0755, set cap_sys_nice=ep, then atomically replace the
launcher. Preserve the ambient-capability drop at Hyprland main entry.
The retained restart script runs inside gpu-experiment.py --seconds 40;
it terminates the existing session and verifies the actual backend mapping.
Use the archived host installer from the workspace tools directory (it resolves
build products relative to that workspace), not directly from this archive.

## Physical reconnect confirmed

The user repeated USB-C adapter unplug/replug and confirmed HDMI returned
without a compositor restart or safe mode. SSH verified unchanged Hyprland
PID 7946, the repaired backend mapping, both DRM connectors connected and
USB storage negotiated at 5000 Mb/s. Disconnect/re-enumeration occurred at
kernel times 2347.944/2358.329 seconds. This completes the real desktop check
for this adapter, in addition to the isolated renderer lifetime reproducer.
