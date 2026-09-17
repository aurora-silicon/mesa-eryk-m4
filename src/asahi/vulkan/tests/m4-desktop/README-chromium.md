# Chromium WebGL regression fixed — 2026-09-17

The bookmarked Shadertoy is **Buoy / XdsGDB** by TekF. Chromium itself was
unchanged (151.0.7922.137-1). Basic WebGL 1 and 2 context creation and clear/readback
already worked; compiling this larger shader blocked the GPU process in our
Vulkan driver. Replacing Gallium alone did not fix it. Replacing only Vulkan
with the retained September 13 library did.

## Cause and fix

Zink enables samplerFilterMinmax. The G16 sampler reduction lowering expands
texture instructions, but early loop unrolling estimated their original cost.
The failing shader grew from 7,111 instructions / 241 texture operations to
113,151 / 2,651 after reduction lowering, then 736,450 / 4,820 before texture
lowering. A retained GPU-process stack is inside nir_index_ssa_defs from
nir_call_serialized / libagx_txs_gen9 / lower_tex_crawl / hk_lower_nir.
This was a CPU compiler stall, not evidence of a GPU kernel fault. The bounded
runs demonstrate the stall; they do not establish a Chromium watchdog kill.

On G16G, preserve loops containing sampler-dependent texture instructions,
including enclosing loops, before initial preprocessing. Other loops retain
the normal unroll policy. No Vulkan capability or browser acceleration is
disabled. The same captured shader now has 48 early texture instructions;
late texture lowering completes at 153,145 instructions / 864 textures.
The dynamic reduction expansion remains expensive: further compiler/runtime
optimization is a TODO, not part of this availability fix.

The new hk_sample_loops test covers sampled and ALU-only loops, nested loops,
explicit unroll hints, idempotence, metadata handling and optional replay of
a trusted captured NIR input. All eight synthetic combinations pass; replay
reduces early texture instructions from 241 to 48.

## Installed build and validation

Final Vulkan SHA256:
`c932739077989a92cd483a361f38f7a55cd257dc8f6b5061754f766810f6eb6d`.
Installed atomically at `/opt/asahi-m4-port/lib/libvulkan_asahi.so`; the previous
library is retained by checksum. Kernel, Gallium and Chromium flags are unchanged.
Existing compositor processes retain their older compatible mapping; newly
started applications load the fix. No reboot is needed for Chromium.

Normal Chromium with a fresh shader cache renders the retained exact shader
and texture on ANGLE OpenGL -> Zink -> Asahi Vulkan (Apple M4). Fixed frame:
640x360, time=10, WebGL 2, no context loss or GL/readback errors. The image was
visually checked. It is **not pixel-identical** to September 13: mean absolute
RGBA byte difference 2.177/255, maximum 176, concentrated in fine ocean/refraction
patterns. Repeating with the old Vulkan and current Gallium exactly reproduces
the old reference. This check is not a claim of bit-exact or broad rendering
conformance; the remaining visual differences are retained for follow-up.

The normal user profile also completes the bounded ten-second animation. The
48 existing minmax sampler oracle cases pass with the fix (zero watched-byte
mismatches). Host Meson regression test and actual captured-NIR replay pass.
All hardware runs used the 60-second watchdog and 0.5-second thermal guard.

The live bookmark currently returns Cloudflare's verification page, including
with the existing user profile. Basic WebGL 1 and 2 clear/readback pass on that
page, but **the live site's shader has not been validated**. The application
validation above uses the exact previously retained shader and texture in
`/home/e/Buoy.html`, with their original content hashes. This local page starts
paused; Play runs for ten seconds. No challenge bypass or browser security
change was attempted.

## Evidence and reproduction

See `chromium-webgl-20260917/` beside this document for selected reports,
library hashes, compiler-stage counts, the relevant native stack, host test
output and pixel comparison statistics. Full experiment evidence remains in
workspace `_provenance/experiments/`. Browser profiles, cookies and desktop
images are excluded from the committed bundle.

Key runs (2026-09-17):

- `165325356138Z-chromium-buoy-current-animation`: failing original driver.
- `165527479333Z-chromium-buoy-old-gallium`: old Gallium does not fix it.
- `165609296185Z-chromium-buoy-old-vulkan`: old Vulkan alone fixes compilation.
- `170107701936Z-chromium-lowering-capture`: original shader growth.
- `170347087656Z-chromium-preserve-texture-loops`: fixed growth and animation.
- `170723838742Z-chromium-bookmark-user-profile-candidate`: live site blocked
  by verification; WebGL context diagnostics work. No shader canvas tested.
- `171228131876Z-chromium-installed-fixed-frame`: fixed-frame readback.
- `171341062285Z-chromium-minmax-regression`: 48 minmax oracle passes.
- `171408732537Z-chromium-installed-user-animation`: 121 total frames,
  time10 ->20.0292, no GL errors, normal user profile.
- `171536795728Z-chromium-old-vulkan-fixed-frame`: reference reproduction.
- `171617682731Z-chromium-webgl-final`: final installed build, 133 total frames,
  time10 ->20.0306, no GL/readback errors; peak65.75 C.

The `171301434826Z-chromium-installed-animation` attempt failed before WebGL
because the probe read a stale DevToolsActivePort file. The probe now removes
that ephemeral endpoint only after checking that the user profile is idle and
selects the requested URL rather than an unrelated restored tab.

From the workspace root, repeat only under the mandatory guard:

```sh
python3 tools/gpu-experiment.py --name chromium-webgl-check \
  --hypothesis 'Installed driver compiles and renders Buoy' \
  --expect 'PASS: Chromium probe completed' --seconds 50 \
  --cpu-max-khz 4104000 -- python3 /root/chromium-probe.py \
  --guard-output '{output}' --normal-launch \
  --url file:///home/e/Buoy.html --start-script /root/shadertoy-play.js \
  --observe-seconds 12 --page-script /root/shadertoy-readback.js
```

`shadertoy-readback.js` contains `return window.m4Readback();`. The probe closes
only its owned browser group and retains evidence. `--user-profile` is optional
and refuses an already-running user browser. Private profiles are the default.
`--vulkan-library` and `--gallium-library` compare per-process libraries without
replacing the desktop's libraries; `--stacks` captures bounded native stacks.

`--lowering-capture` enables opt-in `HK_NIR_CAPTURE_DIR`: writes raw serialized
NIR at preprocessing and minmax/texture-lowering boundaries for shaders of at
least 1,000 instructions. The directory must already exist and be writable.
These are trusted developer captures, not a stable interchange format. Use the
same Mesa serialization ABI; do not feed untrusted files to the regression test.
The retained `buoy-preprocess.nirbin` derives from the attributed Buoy shader
already stored under `shadertoy-buoy/` (see its source and manifest).

```sh
meson test -C build/mesa-m4-asahi-host hk_sample_loops --no-rebuild
build/mesa-m4-asahi-host/src/asahi/vulkan/hk_sample_loops_test \
  mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/chromium-webgl-20260917/buoy-preprocess.nirbin
```

## Earlier qualification

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
