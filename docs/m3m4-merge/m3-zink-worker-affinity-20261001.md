# M3 Zink worker CPU placement

Minecraft's command recording and submission workers often ran on M3's
efficiency cores. Linux reports capacity 576 for those six cores and 1024 for
the five performance cores. Mesa's existing L3 placement policy is disabled on
ARM, so it did not steer this latency-sensitive work. CPU-indexed perf samples
showed the recording worker on an efficiency core during the measured interval.

Zink now prefers the highest-capacity CPU group for its threaded-context and
submission queues when running on Honeykrisp. The group comes from Linux
`cpu_capacity`, not fixed CPU numbers or a frequency override. Each worker
intersects that group with its inherited affinity. Missing topology, homogeneous
CPUs, unsupported affinity, and an empty intersection leave placement unchanged.
The application thread, compiler queues, priorities, graphics calls and GPU
clocks are unchanged. `ZINK_PREFER_HIGH_CAPACITY_CPUS=false` disables the policy.

## Measured effect

Both variants used the same isolated Gallium and Vulkan binaries, kernel boot,
replayer, capture, shaders, settings and 30 completed 3024×1837 gameplay frames.
Each series began with image checks and warmups, then alternated nine timing
pairs. Every sample is retained in the companion JSON.

| Series | Ordinary scheduling | Worker policy | Median gain |
| --- | ---: | ---: | ---: |
| First nine pairs | 70.50 replay FPS | 76.21 replay FPS | 8.10% |
| Repeat nine pairs | 76.63 replay FPS | 80.89 replay FPS | 5.55% |
| Combined 18 pairs | 75.24 replay FPS | 78.75 replay FPS | 4.67% |

These are replay timings, not live Java FPS. All first/middle/last frame pixels
match exactly in both series. A separate whole-process performance-core
placement experiment gained 8.55%; that diagnostic changes the caller's CPU
placement and is not counted as the driver improvement.

## Validation

- The 48-case multi-draw resource oracle passes with the option enabled and
  disabled. It covers sparse and ordinary UBOs, ranges/content, sampler border
  modes, complete DrawID/base-vertex/base-instance values, and simultaneous
  indirect-argument/UBO storage.
- Actual worker affinity is checked after completed rendering. An unrestricted
  11-CPU caller keeps its original mask while both workers use the five
  performance cores. An efficiency-only caller keeps all six allowed efficiency
  cores; a caller limited to two performance cores keeps those same two. The
  absent-option default also passes.
- All 239 selected GLES 3.1 indirect-draw/dispatch cases pass, retaining their
  frozen global indices, options and original catalog hash.
- Six game image checks pass: Minecraft OpenGL/Zink and native Vulkan,
  Satisfactory Vulkan gameplay, Factorio Wayland, 0ad Vulkan and SuperTuxKart
  Zink. References and their pre-existing one-channel-level tolerance are
  unchanged.
- A bounded real Minecraft demo enters its copied world on native Wayland,
  loads the candidate Gallium library and renders the world behind the demo
  information dialog. The launch uses no capture layer and supplies no FPS
  score. The demo dialog is not automatically dismissed.

All GPU runs use the existing 60-second watchdog, 0.5-second PMP telemetry,
startup gate and kernel thermal policy. No reboot or GPU fault occurred.
Failed setup attempts are retained: forced X11 waits in SDL's `XIfEvent` during
window mapping with the policy disabled and with the existing desktop prefix;
the initial Wayland attempt displayed the accessibility onboarding prompt.
Those attempts are not gameplay passes or evidence of a policy regression.

The tested candidate replaces only Gallium in a qualified base prefix. It is
not selected for the normal desktop. Hardware evidence here is M3-specific;
source compatibility does not qualify this change on M1 or M4. Complete package
and desktop deployment validation are recorded separately when performed.
