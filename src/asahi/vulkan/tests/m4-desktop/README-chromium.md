# Chromium Buoy qualification — 2026-09-13

Open `chromium file:///home/e/Buoy.html` on Omarchy. The original XdsGDB
shader and noise texture are embedded unchanged, with attribution/license.
The local page starts paused and its Play button runs ten seconds.

The ordinary `/usr/bin/chromium` launcher, existing user flags and normal
session environment select ANGLE OpenGL -> Zink -> Vulkan Apple M4 G16G B0.
No Chromium configuration or additional Mesa/kernel rendering change was
needed for this task. This is not Chromium's direct ANGLE Vulkan backend.
GPU compositing, rasterization, OpenGL and WebGL are enabled. Video decoding
and encoding remain reported as software; they were not part of this shader test.

## Retained evidence

- `20260913T100041337307Z-chromium-buoy-native`: explicit ANGLE GL test,
  time10,640x360. Pixel output exactly equals the Firefox M4 fixed frame.
- `20260913T100126943783Z-chromium-buoy-normal`: normal launcher, same frame,
  byte-identical output; GPU diagnostics identify Zink/M4, no GL errors.
- `20260913T100244107654Z-chromium-buoy-animation-ready`: normal launcher,
  1180x664, time10 to20.01970000000014,114 animation draws over10.0197s.
  Readback and native imported framebuffer show buoy/ocean in Chromium on
  Hyprland. No GPU/WebGL errors; guarded peak60.75C. This is a short run,
  not broad stability or exact software-pixel qualification.

The same fine ocean/refraction differences as Firefox remain: the fixed
640x360 GPU result is identical between browsers and differs from the retained
Firefox/Zink/lavapipe reference by mean2.52/255 RGBA byte levels.
Existing successful Buoy Vulkan reference/replays are documented in
README-firefox.md. Reuse these for shader regression work.

## Probe improvements

`chromium-probe.py` must run inside the mandatory GPU watchdog/thermal guard.
`--normal-launch` uses the installed launcher and retains user Chromium flags;
otherwise explicit ANGLE/platform test arguments remain available.
`--observe-seconds` bounds observation. `--start-script shadertoy-play.js`
waits for the first shader frame and starts the bounded animation; the failed
100158 test clicked before texture readiness and is a harness failure.
`--page-script` retains page diagnostics and stores pixelsBase64 as pixels.rgba.
Lost/missing contexts and local shader errors fail the probe after evidence
retention. Page metadata includes the local shader's ready/error state.

An optional capture profile is copied into the accessible private test profile;
Chromium's user cannot read a profile directly under /root. The exact original
profile/report remains in evidence.

## Reference-capture limitations

No new Chromium software capture has been qualified as a successful reference:
100345 failed profile access; 100436 exposed the older Firefox-v5 profile's
missing transform-feedback limits and ANGLE refused ES3. Unrestricted
lavapipe captures100513 and100700 lost the WebGL context. Disabling Chromium's
own Vulkan feature did not resolve that. Captures/logs remain diagnostic only.
The earlier100513 helper exit was process-success despite context loss;
new probe validation rejects it. Its black readback is not a valid reference
and its numerical difference is not evidence of a native shader defect.
These software-capture issues remain separate from native Chromium success.

All hardware experiments used the60s watchdog broker and six-channel0.5s
thermal guard. No stress process remains running; normal SDDM boot is retained.
