# M3 fragment resource-program reuse, 2026-09-30

Implementation: `d46b16b03bf`. On G15S, consecutive indexed indirect draws
inside one Vulkan multi-draw command can share an immutable native fragment
resource program. Minecraft's OpenGL capture, rendered through Zink, previously
rebuilt that program for every terrain draw even though its fragment bindings
did not change. This changes Honeykrisp command recording, not Zink or the
application's shader arithmetic/settings.

Reuse is scoped to one API call and one graphics control stream. The shader,
linked variant, image/sampler heap addresses and fragment-visible root contents
must match. The root's own address and vertex-input arrays may differ; the
shared program retains its earlier command-buffer-owned root. Shader memory
writes, meta work, geometry/tessellation, pipeline statistics, conditional
rendering and preambles retain their existing paths. Vertex programs remain
per-draw. GPU execution still loads actual buffer contents; no CPU snapshot of
UBO data or application indirect arguments is introduced. The diagnostic
`HK_M3_MULTIDRAW_FS_PROGRAM=false` disables reuse.

## Performance

Same 30 completed 3024x1837 Minecraft gameplay frames, first/middle/last images
pixel-identical. Nine alternating pairs on one normal boot:

| Build | Replay FPS samples | Median FPS |
|---|---|---:|
| prior robustness-batching control | 75.7239, 64.5481, 76.4129, 69.1130, 64.1900, 64.1654, 74.9945, 68.8890, 75.9970 | 69.1130 |
| final fragment-program reuse | 79.4141, 73.5384, 67.5264, 78.6681, 74.1337, 72.3330, 78.6931, 73.7648, 72.4818 | 73.7648 |

Median gain: **6.73%**. Every sample is retained. These short intervals vary
substantially: an initial three-pair build comparison gave +12.70%, an explicit
same-binary on/off comparison gave +6.72%, and a later final-build three-pair
comparison was effectively tied (-0.007%). The nine-pair result is the expanded
measurement, not a claim that every individual run improves. These are replay
rates, not live Minecraft/Java FPS or a native M1 Vulkan comparison.

Final isolated prefix: `/opt/asahi-m3-multidraw-fragment-final-3926973c5c1f`.
Vulkan SHA256:
`3926973c5c1f71a8d366aedc312b40a29cd6daeabfa0ca76086a6cc386726417`.
The final staged patch against `481f95d7d4d` is exactly the implementation
committed as `d46b16b03bf`; the JSON retains its precommit provenance and hash.

An instrumented interval reduces the three terrain shaders' resource-program
uploads from 40,860 to **90** (30 per shader). Total fragment-program uploads
drop from 46,751 to 5,981. These count uploads, not shader executions, covered
pixels or GPU cost. Terrain shader hashes, instructions and register footprint
remain unchanged. Global tiling/fragment observations remain about 80/137 ms;
those overlapping counters include other clients and are not an additive
frame-time breakdown.

A separate diagnostic pair **without per-shader logging** reports process CPU
time across all replay threads of 135.912 ms control versus 84.564 ms candidate
over 30 frames (37.78% less). It is a single instrumented pair, not an approved
FPS measurement. The earlier verbose upload profile also includes the logging
cost of tens of thousands of now-avoided program uploads.

## Qualification

- Frozen union of the existing indexed-draw and resource-binding selections:
  **2,404 cases: 1,434 Pass, 970 NotSupported, zero failures**, all 51 guards
  complete. The separate CTS JSON preserves every catalog index, case and
  unsupported reason; no capability/outcome filtering or renumbering.
- All **38 exact-pixel indexed multi-draw witnesses** pass on the final
  binary. They cover batch boundaries, GPU-produced arguments/counts, partial
  and wholly out-of-bounds 8/16/32-bit indices, zero work, concurrent generated
  allocations and two submissions of each recorded command buffer. These are
  synthetic witnesses, not CTS passes.
- Host resource/root tests pass, including rejection of changed resource
  addresses, dynamic bounds, views, query state, epilog inputs and push data.
  Descriptor-prefetch tests and the ARM64 build pass.
- Six retained game capture checks pass: Minecraft GL and Vulkan,
  Satisfactory Vulkan gameplay and 0 A.D. Vulkan are exact; Factorio Wayland
  and SuperTuxKart Zink stay within the already accepted one-RGB-level policy.

Boot remains `4b361b98-fdd1-4b11-ae68-508522fecbd9`. Every hardware workload
retains its 60-second watchdog and 0.5-second all-channel thermal monitor.
CTS peak observed temperature is 77.5 C; the six game checks peak at 90.75 C.
No GPU fault, watchdog recovery or reboot was required for this qualification.
The normal display manager remains active. These results qualify the isolated
M3 candidate and targeted regression coverage; they do not establish full
conformance, M4 behavior, or a new desktop installation.

Original evidence: workspace `build/m3-multidraw-fragment/`, benchmark
`diagnostics/m3-multidraw-fragment-{program,toggle,final,nine-pairs}-20260930/`
and session `m3-multidraw-fragment-final-20260930`. Benchmark tooling now accepts
`--pairs 9`; an individual guarded workload and its 30-frame interval stay
unchanged.

## Other investigations

Compact descriptor prefetch initially produced an apparent +8.71% paired
median. The actual default-policy candidate did not repeat it (-2.68%), so that
default change was rejected despite exact images and a healthy 1,429-case CTS
selection (965 Pass / 464 NotSupported). A per-draw option-caching prototype
was also rejected (-10.70% paired median, exact images). Both original sample
sets and candidate prefixes are retained; neither change is enabled here.

The frequent terrain fragment programs perform a real four-tap application
texture-filtering loop. Removing it would change the captured workload. Offline
compilation with LICM instead of the existing GCM makes the three terrain
programs grow from 260/280/280 to 290/310/310 instructions, with a higher logged
register requirement. The large 1,098-instruction shader grows to 1,685.
All compile, but this provides no hardware correctness or timing qualification;
the compiler policy is unchanged.

Remaining areas to investigate include per-draw vertex resource-program
construction, root uploads and the terrain shader's actual texture/arithmetic
cost. This change specifically removes duplicate fragment resource setup.
