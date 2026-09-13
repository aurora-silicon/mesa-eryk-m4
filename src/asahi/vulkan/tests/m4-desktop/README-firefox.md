# Firefox / Zink bring-up evidence

Firefox 155.0.1 uses the normal private Mesa/Zink installation on J713. An
isolated Marionette profile supplies graphics diagnostics; native scanout is
used for images (ordinary WebDriver screenshots may use software rendering).
Automation flags are not normal browser-launch settings.

## Native varying size

The former 28-word guard rejected Firefox WebRender shaders with 46 user
words. Apple applegpu-nt targeting G16G accepts 29 float4 user locations. Its
vertex __GPU_LD_MD FlatBuffer root.field1.field3 is 120: four position words
plus 116 user words, using the existing all-f32 UVS layout. No new encoding or
layout bypass is required. The declared Vulkan component limit was already116.

Reproduce from `firefox-varyings.metal` using `native-metal-compile.py --stage
render --function fs --output NEW`. Inspect the vertex __GPU_LD_MD with
`flatbuffer-field.py FILE --path 1.3 --type u32`. Paths are zero-based.
The original archive, tools/commands and hashes are retained in workspace
`build/firefox-native-varyings116/`; vertex metadata SHA256:
`5dfc7ee9bae13911f689e108ceb7c6782cbfb4e4d0f4902fed04792d1c8bbd9f`.
The decode test covers 120 words and rejects exceeding the component bound.
Offline native compilation and packet tests alone are not hardware validation.

## Capture boundaries

- `20260912T031605707425Z-firefox-mdn-reference`: 117-frame,23MiB software
  reference; MDN WebGL textured cube visibly correct. Graphics reports Zink /
  llvmpipe WebRender. No M4 rendering claim for this reference.
- `20260912T031855463816Z-firefox-mdn-native-prefix`: reference replay rejected
  ShaderLayer even though recorded Vulkan1.2 features are false. Preserve the
  mismatch; do not strip the capability or treat the profile as equivalence.
- Native layer/viewport outputs are now correctly unadvertised; neither the
  UVS special-word layout nor layered framebuffer rendering is implemented.

All hardware runs use the 60-second watchdog and fresh six-channel thermals.

## Captured BDA buffer binding

`vk-capture-buffer-bind.c` reproduces Firefox's 1MiB buffer suballocated at
1MiB into a2MiB memory allocation, with capture/replay enabled. Before the fix,
`hk_BindBufferMemory2` includes the extra virtual guard in the BO mapping and
returns VK_ERROR_UNKNOWN. Binding only the page-rounded buffer storage passes.
Evidence: `20260912T032338882082Z-capture-buffer-bind-before` fails;
`20260912T032407605031Z-capture-buffer-bind-after` passes. This tests binding
and address acquisition, not shader execution.

## Bounded capture tools

`firefox-probe.py` runs as a child of the workspace watchdog/thermal guard,
uses an isolated e-user profile and Mesa shader cache, and records Firefox's
Troubleshoot graphics data, browser/compiler logs, process mappings, one native
scanout and optionally Vulkan captures. Example target argv inside
`tools/gpu-experiment.py --seconds 40 --expect '^PASS: Firefox probe completed'`:

```
python3 /root/firefox-probe.py --guard-output '{output}' --observe-seconds 2 \
  --force-gpu --capture
```

The default page is MDN's sample6 rotating WebGL textured cube. `--renderer
lavapipe --capture-profile /root/m4-firefox-common-profile-v3.json` selects the
software reference with explicit capability simulation. A fresh Mesa cache
prevents cached shaders from a previous capability profile being reused.
Keep the profile, trace and environment with the reference; simulation does
not make different devices equivalent. `--force-gpu` is a test preference;
probe completion alone does not establish GPU acceleration or correct pixels.

GFXReconstruct 11891af36f7f0d75213613be96f23d16c36f88d8 plus
`patches/gfxreconstruct-process-id.patch` adds `${ProcessId}` to capture paths,
preventing Firefox's multiple processes from overwriting a single trace.
The patch changes capture naming only. The private replay executable is
`/opt/gfxreconstruct/bin/gfxrecon-replay`.

`vulkan-trace.py replay --end-frame N` starts from the original beginning,
retains initialization and exits after N frames using the replay measurement
range. `--frames` selects screenshot frames, not the endpoint. Native Firefox
BDA captures currently need explicit `--memory-translation rebind` when replay
allocations differ; retain that adaptation in evidence. Use
`--omit-pipeline-cache` after driver changes. Replaying the native startup
capture in `20260912T032428296751Z-firefox-native-buffer-fixed` reproduces the
RGBA32 failure in a fraction of a second without browser navigation.

`install-mesa-library.py BUILD --label LABEL --allow-live` stages, hashes,
backs up and atomically installs a compatible private Mesa library. Existing
processes retain the previous inode; only fresh processes test the new build.
Without `--allow-live`, GPU clients must be stopped. This is not a kernel/UAPI
upgrade mechanism and does not replace the experiment guard.

### Shadertoy page diagnostics and rematerialized vertex outputs

`firefox-probe.py --page-script /absolute/read-only-diagnostics.js` retains
the diagnostic JavaScript and its JSON result after navigation. Use it to
distinguish site verification/error pages from a working WebGL canvas; never
create a context just to claim one exists. Existing guards/cleanup apply.

XdsGDB investigation20260913: initial Firefox aborts on saved WebRender
vertex bundle398b8af55d77. RA rematerialized1.0 between outputs47/48 after
the pre-RA output bundling pass. Allow ordered wide exports separated only
by MOV_IMM and retire their staging COPYs before GPR reuse. Contiguous tails
keep batched waits. Offline replay now compiles6446bytes; guarded092843
Firefox survives15s with WebRender/WebGL reportingZink/M4. Page remains
Cloudflare verification, not Shadertoy content or shader qualification.
Thirteen focused output tests pass; three older SixVertexOutputs tests fail
identically on unmodified99b3302b66d and are recorded as baseline failures.
An ordinary-profile40s run092942 faults the GPU later; snapshotjob8887
retained and watchdog recovery completed. This fix is not a stability claim.
