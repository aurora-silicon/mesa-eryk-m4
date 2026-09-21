# J713 macOS SuperTuxKart comparison, 2026-09-21

STK 1.5's Black Forest race benchmark on the same Mac16,12 M4, macOS 26.6.2
(25G83), native arm64. One completed measured race per renderer:

| Vulkan implementation | Race frames | Race seconds | Average FPS | ms/frame |
| --- | ---: | ---: | ---: | ---: |
| MoltenVK | 4551 | 38.135 | 119.34 | 8.38 |
| KosmicKrisp | 2262 | 38.122 | 59.34 | 16.85 |

These are race counters, excluding loading and menus. Actual Vulkan image
creation confirms 3840x2160 render targets, with a 1920x1080 swapchain in an
external-display-sized borderless window and render scale 2. They are internally
4K, not a 4K physical display mode. Both runs loaded the API-dump layer, logging
frame zero only; shader/archive dumping was disabled. Single-run results and
instrumentation are retained, not a statistically controlled driver ranking.

The earlier 120/145/73 FPS observations are superseded by this matched pair.
SDL fullscreen Spaces transitions and sleeping displays produced inconsistent
dimensions. `molten-geometry-1` even changed dimensions mid-run: its old summary
claimed success because it tested for *any* matching size. Exclude that run.
The current validator requires all observed swapchain sizes to match and rejects
the known wrong render sizes. CSV `real_width/real_height` are stale pre-HiDPI
values, not render-target evidence. No cross-OS claim of an identical Vulkan
command stream is made: STK has platform/capability-dependent rendering paths.

## Reproduction and adaptations

Working copies live in workspace `tools/macos-native/`; these are committed
snapshots. The official 1.5 macOS release archive was extracted privately under
`/private/var/tmp/stk-comparison-20260921` on root@mac-m4. The app's embedded
MoltenVK was replaced with the adapter and the private bundle re-signed ad hoc.
The original archive/library remain intact; system applications were not changed.
Vulkan SDK libraries came from the already installed 1.4.357.1 SDK on mac-m5.

STK insists on loading a bundled MoltenVK and its private Metal feature query.
The adapter re-exports the standard loader, selects the explicit ICD, supplies
only the consumed core base-vertex/instance field, and omits unsupported
**MoltenVK-only** layer settings on KosmicKrisp. It does not implement the whole
private MoltenVK API or fake general Vulkan feature support. macOS STK itself
disables shaderDrawParameters as a MoltenVK workaround for both renderers.

The SDL observer forces a borderless external window to eliminate native
fullscreen geometry transitions. Its optional Metal archive hooks add extra
compilation and must not be enabled for clean timing. KosmicKrisp's SDK binary
uses Metal 4 compiler/pipeline APIs; these Metal 3 archive hooks do not capture
its pipelines. This also explains why hooks based on the older local Mesa
KosmicKrisp source did not intercept the SDK's compilation.

Example on the prepared M4 (Python uses the direct binary because newly updated
Xcode launchers requested a license; no license was accepted automatically):

```
/Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/Current/bin/python3 \
  stk-benchmark.py --base /private/var/tmp/stk-comparison-20260921 \
  --output /private/var/tmp/stk-comparison-20260921/NEW-RUN \
  --renderer moltenvk --api-dump
```

Use `kosmickrisp` for the other renderer. Base config is the retained Linux
quality configuration: dynamic lighting enabled, geometry 2, anisotropy 4,
shadows/SSAO/MLAA/motion blur disabled, texture maximum 512, vsync disabled.
The runner has a 240-second bound, kills its process groups on errors, and
refuses the application's silent OpenGL fallback. `caffeinate` temporarily
keeps the displays awake. Desktop screenshots via SSH failed TCC checks;
a separate screenshot-layer diagnostic produced an image, at the earlier
wrong size, and is not used as final 4K evidence.

## Telemetry and shader evidence

All six PMP records were sampled about every 50 ms using the already qualified
DevMem service and this boot's checked record table. Raw fixed-point/float
values and validity flags are retained. Idle/power-gated invalid records are
not temperatures. Powermetrics sampled CPU/GPU clocks and thermal pressure every
500 ms. On the user's explicit instruction, macOS managed all temperatures:
there were no temperature cutoffs or clock overrides.

Whole-process GPU active frequency median/max: MoltenVK 1456/1470 MHz,
KosmicKrisp 1464/1470 MHz. These include loading and are not race-only residency.
Highest valid recorded channel: tp001m, 113.25 C (MoltenVK), 112 C (KosmicKrisp).
These are PMP channel readings, not qualified GPU-junction temperatures.
`comparison.json` includes every channel's peak. Do not extrapolate Apple
thermal safety thresholds from these observations or relax Linux guards.

Separate `molten-shaders-1` retains 53 Metal pipeline archives, generated MSL
and SPIR-V. `kk-shaders-1/game.log` retains generated MSL, but no Metal 4 binary
archives. `metal-archive-extract.py ARCHIVE --output NEW-DIR` extracted 106
text sections / 212 symbols from the MoltenVK archives. The optional C helper
links the existing host Mesa `libagx3b_disasm.a`; G16 uses AGX3b, not AGX2 or
the older Python decoder. Decode by symbol start/next-symbol or section end:

```
cc -Wall -Wextra -Werror -DHAVE_ENDIAN_H \
  -isystem mesa/m4-asahi/src -I build/mesa-m4-asahi-host/src \
  -I mesa/m4-asahi/include -I build/mesa-m4-asahi-host \
  tools/agx3-archive-disasm.c \
  build/mesa-m4-asahi-host/src/asahi/isa/libagx3b_disasm.a -o OUTPUT
OUTPUT TEXT-FILE START END
```

This is partial disassembly: 199/212 symbol ranges include unknown bits or
padding. Raw bytes and metadata are preserved. It does not establish which
archived variants executed, and no instruction-count explanation of the FPS
gap has been demonstrated.

Raw data remain in workspace `build/stk-macos-20260921/runs/`; shader extraction
is adjacent in `decompiled/`. `evidence-sha256.json` pins 305 fetched run files.
Do not replace incomplete attempts with passes or include their FPS in the
matched pair. App/library identity:

```
03adb10095373cdca69a249f0e9ac9b4d9f9f628edaffd6259ce446081ec74f9  app executable
440b27669262c6feca7ac4ccc40dc6e9aa6c14543a8cb56e1aa1080aed0f1c0d  loader adapter
b9efad7eae9d6325103ebe6bd7d837593ffbcded491d1e761c96088e368bf551  Vulkan loader
99fe9d36457e48d247a4bd15e43738a75f1d7dac724b2ea085212ed59e3254b2  MoltenVK
21bb3e989c3a9e446129525c5dc1e609e5a419ba50e95ec55c6d79172dcb6dd6  KosmicKrisp
e719a385416a74fc8fe2d9f709d3bc8ee78a6f79fbf2234f8db3f163ff07ce49  SDL/Metal observer
cc084cf9823d4c06f7b2e3449dd7a0561bf8ee6ffc42a7ea65966958426bc8b5  API dump layer
```

After fetching evidence, blessed `/Volumes/Omarchy` and rebooted to Linux.
SDDM/autologin returned, both DRM connectors report connected, watchdog broker
is armed at 60 seconds and idle. Audio was muted on both OSes at user request.
No experimental thermal policy or Mesa change was installed by this comparison.
