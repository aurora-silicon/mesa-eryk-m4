# J713 native desktop qualification

These are opt-in hardware tests for the identified T8132/G16G firmware and
experimental common Asahi driver. They do not enable another SKU or prove
conformance. Use only with the J713 watchdog and thermal workflow.

`asahi-session.py` starts the actual Omarchy user configuration under seatd,
checks its M4 Zink renderer, compares a red test terminal in Wayland screencopy
and imported DCP scanout, then starts SuperTux's first level. It records the
application's loaded libraries, actual GL renderer, screenshots, panel layers
and KMS completions. `gl-renderer-audit.c` only logs the active context's renderer
when SuperTux queries GL_VERSION; it forwards every query unchanged.

The interactive path runs a fresh real Vulkan computation, verifying 32 output
values, and checks Hyprland IPC and DCP import/completion status every five
seconds. `desktop-guard.py` verifies a 60-second hardware watchdog before any
work, and renews it only for a fresh, sequential successful GPU/compositor
check with a live thermal monitor. It never runs an unconditional heartbeat.
All six PMP channels are monitored at 0.5-second intervals, with enabled CPU
thermal policy, startup below 65 C and workload termination at 75 C. No GPU
clock increase or qualified dedicated GPU sensor is assumed.

The guard defaults to a bounded 1800-second session (maximum 3600). The user
can end it with Super+Shift+Escape using the existing supervised Omarchy
configuration. Failed progress stops GPU clients and requests reboot while
leaving watchdog recovery armed. Normal completion saves evidence before
explicitly disarming. DCP remains pinned until reboot.

Deployed layout on the qualification machine:

- `/root/asahi-desktop/{desktop-guard.py,desktop-launch.py}`
- `/root/hyprland-validation/{asahi-session.py,seat-user.py}`
- `/root/g16-submit-test/{experiment_watchdog.py,experiment_thermals.py}`:
  existing guard helpers only; no lab GPU driver is loaded.
- `/root/m4-vulkan-compute-min`: the Vulkan compute test compiled with
  `TEST_DEVICE_ID=0x8132`.
- `/root/m4_dcp_rtkit-asahi.ko`: existing native DCP driver rebuilt against
  the running kernel; it supplies display scanout, separate from Asahi GPU.
- `/opt/asahi-m4-port`: isolated Mesa, explicit Vulkan/EGL JSON files, and the
  existing split-render/display Aquamarine build (`3939123` in the workspace
  `reference/aquamarine-m4` checkout).
- `/opt/m4-hyprland/omarchy.lua`: existing supervised user configuration.

Build the context logger for arm64 with `-O2 -Wall -Wextra -fPIC -shared -ldl`
and install it as `/opt/asahi-m4-port/lib/gl-renderer-audit.so`.
Start from root, choosing a new evidence directory each time:

```
systemd-run --unit=m4-asahi-desktop --collect \
  --property=RuntimeMaxSec=1900 --property=TimeoutStopSec=8 \
  python3 /root/asahi-desktop/desktop-guard.py --seconds 1800 \
  --output /root/asahi-evidence/UNIQUE-NAME
```

Do not start competing watchdog owners or reload a stopped firmware session
without a fresh reboot. The launch script retains an already-running Asahi/DCP
session. `g16_lab` must be absent. This is a supervised bring-up desktop, not an
automatic boot service or a claim of GPU recovery/thermal throttling support.
