# J613 (MacBook Air 13" M3) Mesa: 14.x firmware path (daily desktop)

Branch `satchlj/j613-mesa-14x`, on `eryk_m3m4_merged_mesa` at `54e2e5d9738`. That branch has since moved on 235
commits.

## What this branch is

The Mesa the J613 daily desktop uses with the 14.x kernel (`satchlj/j613-kernel-14x` in dirtyroom-linux): Eryk's M3
Vulkan driver (honeykrisp) with Zink on top, admitted for T8122/G15G.

- `bbd70c18c7f`, `ab99aaaf2f8`: Chris Kearney's kmsro and Zink fixes, cherry-picked from `m3-j516s-fixes`.
- `fcdadddcd29`: admits T8122/G15G (J613) to the M3 runtime path behind explicit opt-ins.
- `3968709f51b`: drm-shim J613 identity (`ASAHI_SHIM_CHIP=g15g`) for GPU-less tests.
- `ecca953a528`: AGX3 decomposer over the composer tables.
- `c8e1dbb120d`: **G15G scheduling stalls.** The rule is in `src/asahi/compiler/README-g15g-scheduling-stalls.md`;
  the tools that rederive it from Apple's offline compiler are in `src/asahi/compiler/test/probes/g15g-stalls/`.
- `670684350df`: admission needs only `ASAHI_M3_EXPERIMENTAL=1`. `ASAHI_G15G_UNSCHEDULED=1` turns the stall pass off
  for A/B tests.
- `870b4d8b3cb`: honeykrisp shader caches keyed by the stall switch.

## Build

Two stages: first build the CLC tools with LLVM 18, then `meson -Dvulkan-drivers=asahi -Dgallium-drivers=zink
-Dplatforms=wayland -Dllvm=disabled --prefix=/opt/mesa-j613`. It installs next to the system Mesa and is selected per
session (LD_LIBRARY_PATH, VK_ICD_FILENAMES, `ASAHI_M3_EXPERIMENTAL=1`). The kernel refuses J613 submissions until
its `submit_verified` gate is set.

## Tested on hardware vs compile-only

- The whole branch is the J613's daily desktop Mesa (Hyprland on Zink on honeykrisp, browsers), on the 14.x kernels
  since late September.
- The stall pass runs in that desktop. Its timing effect against `ASAHI_G15G_UNSCHEDULED=1` has not been measured on
  hardware. What is established is that it reproduces Apple's compiler output, below.
- `test-agx3-g15g-stalls` (in the meson test suite) checks the pass against the vectors offline.
- **Index-buffer alignment (2026-10-07):** G15G fetches direct-draw indices from a dword-aligned address, so a u16
  index buffer bound at a 2-byte offset loses a triangle per quad. The fix (`asahi: align G15G direct index uploads
  before drawing`, on `satchlj/j613-mesa-25g83`) is in the Gallium driver, which this build doesn't use. Honeykrisp
  adds `firstIndex × index size` to the bound address (`hk_index_buffer()`), so Zink could hit the same fetch with an
  odd u16 `firstIndex`; not checked yet.

## Key findings

- **G15G scheduling stalls, rule matched to Apple's compiler.** Apple's `metal-nt -arch applegpu_g15g` inserts an
  8-byte stall `06 (W<<2) 06 00 06 00 06 00` before any instruction whose scoreboard wait W overlaps a slot still
  pending from a GPR-writing producer (signal slot set, destination bank 2). The stall value is W ∩ pending, and
  `end` drains. Pending slots follow a forward dataflow over fallthrough, b/bn and conditional-region skip edges, and
  b/bn displacements are recomputed. On 1,540 shaders with 8,347 stalls the pass reproduces Apple's G15G code byte
  for byte: no extra stall, no missing one, every value and branch displacement equal. That includes a 450-shader
  holdout generated after the rule was fixed. With the stalls removed, G15G code equals G15S code. G16G code equals
  G15S in all 1,540.
- `agx3_g15g_stall_vectors.h` holds Apple compiler output for our own test shaders (stall-stripped input and expected
  output). `run-corpus.sh` regenerates everything with the Xcode Metal toolchain. No Apple binaries are stored
  otherwise.

## Commits (oldest first; author is Satya Benson unless named)

- `bbd70c18c7f` kmsro: keep the render-node fd close-on-exec (Chris Kearney)
- `ab99aaaf2f8` zink: treat implicit dmabuf modifiers as LINEAR on Honeykrisp (Chris Kearney)
- `fcdadddcd29` asahi: admit T8122/G15G (J613) to the M3 runtime path behind explicit opt-ins
- `3968709f51b` asahi/drm-shim: add the J613 G15G identity for offline smoke tests
- `ecca953a528` asahi/compiler: AGX3 decomposer over the composer tables
- `c8e1dbb120d` asahi/compiler: insert Apple's G15G scheduling stalls
- `670684350df` asahi: admit G15G with ASAHI_M3_EXPERIMENTAL alone
- `870b4d8b3cb` hk: partition shader caches by the G15G scheduling-stall switch
