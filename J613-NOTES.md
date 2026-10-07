# J613 (MacBook Air 13" M3) Mesa: 25G83 (macOS 26.6.2) native Gallium GL

Branch `satchlj/j613-mesa-25g83`, on `eryk_m3m4_merged_mesa` at `671678ed425` (that branch's head when this was
pushed).

## What this branch is

Native Gallium OpenGL for the J613 on 25G83 firmware (HAL200 / USC3 path), the userspace for
`satchlj/j613-kernel-25g83` in dirtyroom-linux. In order:

- `a6e807087f7`..`7874f943158`: the six J613/G15G commits of `satchlj/j613-mesa-14x` (admission, drm-shim identity,
  AGX3 decomposer, **scheduling stalls**, opt-in, cache key) re-applied on this base.
- `fe1cc0392a9`, `b203300f86b`: target J613 25G83 HAL200 without the legacy M3 runtime encoding; pass the chip
  identity through Gallium PPP finalisation.
- `e00f1a8b7b7`: `util`: building with `-DMESA_DYNAMIC_TLS` drops the initial-exec TLS model. Needed for a Mesa
  installed in its own prefix and loaded late by applications.
- `ac21c665ffa`..`983a9d0c624`: five of Eryk's native-Gallium commits, cherry-picked from
  `experiment/m3-native-opengl-20261001` (there `e4cfe268c11`, `51827de93ff`, `9dc87c50f9f`, `be70ada8cdd`,
  `1a30e34fe54`).
- `a549feb726e`, `266f72243ed`: select the native Gallium shader and tile pipeline for J613 HAL200; drm-shim fixture.
- `37d56e32d7a`, `e320c27291d`: half-precision normalized tile colours packed correctly; normalized tile loads
  unpacked before narrowing.
- `647ca992ed2`: `u_queue` sets a minimum-priority worker's scheduling policy from the worker itself. Chromium's GPU
  sandbox kills the GPU process (SIGSYS) when the creating thread calls `sched_setscheduler` with another thread's
  TID. Includes a seccomp test, `u_queue_sandbox_test`. Generic, not J613-specific.
- `fe1f6faabf9`: G15G MSAA images survive API resolves: store every sample and use the normal image resolve.
- `298de3f32e1`: raw vertex prologs for G15G packed attributes (fixes ushort/packed vertex buffers).
- `eaa3a11e97c`: `tools/j613-d2/`, the session integration for the D2 daily-driver candidate (see its README).
- Added after these notes were first written:
  - `783353fb016`: **G15G index-buffer alignment.** G15G fetches direct-draw indices from a dword-aligned address, so
    a u16 index buffer bound at a 2-byte offset (Qt Quick does this) lost a triangle per quad: the diagonal
    corruption in the SDDM greeter. Same bug as G17P (aurora-silicon/mesa 325e0839), same fix: copy the selected range
    to the aligned upload pool. Also on `j613-d3-index-alignment` as `309170e02c7`.
  - `eca78aa9f20`: `tools/j613-d3/`, D3 session integration: D2's plus SDDM `GreeterEnvironment`, so the Qt greeter
    gets the Mesa environment (SDDM strips its own).

## Build

`meson -Dgallium-drivers=asahi -Dvulkan-drivers=asahi -Dplatforms=wayland --prefix=/opt/mesa-j613-25g83-d3` plus
`-Dc_args=-DMESA_DYNAMIC_TLS -Dcpp_args=-DMESA_DYNAMIC_TLS` (with the CLC tools from a first LLVM stage, as for the
14.x branch). Run with `ASAHI_M3_EXPERIMENTAL=1` and the appledrm card
(see `tools/j613-d3/`).

## Tested on hardware vs compile-only

- The library built from `298de3f32e1` (libgallium sha256 `50edf703…`) ran the native desktop on the G54/G55 kernels:
  Hyprland, browser, sandboxed WebGL with default anti-aliasing. On 2026-10-07 the installed stage 1 booted the ESP
  G54 kernel, and this Mesa rendered 120 native frames with pixel checks passing.
- `fe1f6faabf9`: unflushed 2x/4x full and cropped RGBA8 resolves pass at 32x32 through 900x480. Browser text has a
  separate, still-open bug.
- `647ca992ed2`: the seccomp test is killed by SIGSYS without the fix and passes with it (aarch64 Linux).
- `eaa3a11e97c`: offline checks only; D2 itself failed display coldplug (a kernel bug, fixed in D3).
- **D3, 2026-10-07** (kernel `satchlj/j613-kernel-25g83` with its D3 commit; this branch at `783353fb016`, libgallium
  sha256 `edb7c015…`; `tools/j613-d3/`), booted from the installed J613 stage 1:
  - `783353fb016`: before it, every offset-2 u16 case failed 1953/3844 pixels; after it, 17/17 pixel cases pass, with
    array, aligned, colour and depth controls unchanged. Clean Qt greeter. Indirect indexed draws are not covered.
  - Installed qualification **PASS**: untouched native-GPU SDDM greeter, 17/17 pixel cases, 3,000 EGL/GLES frames
    with readbacks, headless browser WebGL readbacks on "Apple M3" through this Gallium driver.
  - Continuous soak **PARTIAL**: stopped by the owner at 49/120 samples (12 min 20 s) with no failures
    (49/49 WebGL readbacks, 49/49 indexed-quad checks, 898 → 44,086 frames). System power 10.5–13.4 W (mean 12.0 W)
    on battery while the J613's USB-C port also powered the debug host, so not a GPU figure. Details in the kernel
    branch's `J613-NOTES.md`.
  - A texture-core regression run passed 6,482 checks. A G17P-style helper-invocation patch is not needed here: this
    compiler already keeps quad helpers alive across samples.

## Key findings

- See `satchlj/j613-mesa-14x` for the stall rule (Apple-exact on 1,540 shaders / 8,347 stalls).
- On 25G83, J613 uses the HAL200 native pipeline, not the legacy M3 runtime encoding the 14.x path uses.

## Commits (oldest first; author is Satya Benson unless named)

- `a6e807087f7` asahi: admit T8122/G15G (J613) to the M3 runtime path behind explicit opt-ins
- `8f358166913` asahi/drm-shim: add the J613 G15G identity for offline smoke tests
- `032e8b4a797` asahi/compiler: AGX3 decomposer over the composer tables
- `6aacef080d2` asahi/compiler: insert Apple's G15G scheduling stalls
- `5dced515924` asahi: admit G15G with ASAHI_M3_EXPERIMENTAL alone
- `7874f943158` hk: partition shader caches by the G15G scheduling-stall switch
- `fe1cc0392a9` asahi: target J613 25G83 HAL200 without the legacy M3 runtime encoding
- `b203300f86b` asahi: pass chip identity through Gallium PPP finalization
- `e00f1a8b7b7` util: allow dynamic TLS for isolated late-loaded Mesa builds
- `ac21c665ffa` asahi: bring up isolated Neo native Gallium OpenGL on M3 (Eryk Wieliczko)
- `768eafeffd2` asahi: lower native normalized vertex fetches with Neo conversion forms (Eryk Wieliczko)
- `57a2960c91b` asahi: align Gallium uniform ranges to native Ioto dwords (Eryk Wieliczko)
- `a02be768dae` asahi: bound native graphics ranges by the loader workspace (Eryk Wieliczko)
- `983a9d0c624` asahi: preserve Gen9 buffer bounds for native Gallium (Eryk Wieliczko)
- `a549feb726e` asahi: select native Gallium shader and tile pipeline for J613 HAL200
- `266f72243ed` asahi/drm-shim: add an explicit J613 HAL200 construction fixture
- `37d56e32d7a` asahi: pack half-precision native normalized tile colours
- `647ca992ed2` util: set queue worker scheduling policy from the worker
- `e320c27291d` asahi: unpack normalized native tile loads before narrowing
- `fe1f6faabf9` asahi: preserve G15G MSAA images across API resolves
- `298de3f32e1` asahi: use raw vertex prologs for G15G packed attributes
- `eaa3a11e97c` tools: J613 25G83 D2 session integration
- `783353fb016` asahi: align G15G direct index uploads before drawing
- `eca78aa9f20` tools: J613 25G83 D3 session integration
