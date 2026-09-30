# M3 robust indexed multi-draw batching, 2026-09-30

The Minecraft OpenGL capture uses Zink and issued 40860 one-subgroup index
robustness launches in 30 completed frames. Each launch also rebuilt native
compute program/dispatch state. Combine up to 128 draws into one launch,
with one subgroup per draw and the same execution-time robustness kernel.

The CPU only records argument/output addresses. Counts, indices and buffer
bounds are consumed by the GPU. The command-buffer-owned table is finalized
before submission; it is not a snapshot of application indirect arguments.
A graphics/pre-GFX stream change starts another batch, preserving heap reset
and generated-packet lifetimes. Conditional rendering retains separate launches
because it creates a new GPU argument producer inside each draw. Initially
batching is enabled only on G15S, with HK_BATCH_ROBUST_INDEX=false as a control.
M4 and the normal desktop installation are unchanged.

## Measurement

Three alternating pairs, same 30 completed 3024x1837 gameplay frames:

| Build | Replay FPS samples | Median FPS |
|---|---|---:|
| descriptor-decode-once control | 57.0233, 57.9915, 47.5330 | 57.0233 |
| batched robustness with corrected padding | 68.5477, 68.3509, 69.9214 | 68.5477 |

The final median improves 20.21%. First/middle/last saved frames are pixel-identical.
A fresh installed M1 Zink control gives 50.6098, 50.7314 and 51.5484 FPS,
median 50.7314: M3 is 35.12% faster in this replay. These are replay scores,
not live Minecraft/Java FPS or native M1 Vulkan scores. M1 has separate kernel,
Mesa and thermal telemetry qualification limits recorded in the JSON receipts.

An instrumented final M3 interval consumes 99.570 ms process CPU across all replay
threads, versus 182.513 ms for the earlier instrumented control. Instrumented
runs include profiling/completion fences and do not supply benchmark scores.

## Qualification

Frozen indexed-draw/robustness/multiview/conditional selection: 975 cases,
469 Pass, 506 NotSupported, zero failures, healthy completion. Retain every
catalog index and skip reason; this is not full conformance. Conditional
rendering coverage is unsupported on this device, so its fallback remains a
source-reviewed exclusion rather than a tested application claim.

38 separate exact-pixel witnesses pass with batching, and all 38 pass again
with batching disabled, each submitting the same command
buffer twice. They cover 2/127/128/129/257/2048 draws, first/middle/last selected
entries, partial/all out-of-bounds 8/16/32-bit indices, concurrent padding/zero
allocation, zero counts/instances and indirect-count predication. Arguments and
counts are GPU-written after recording-time zero initialization. The oracle
checks every RGBA8 output byte and both 64-byte readback guards. The probe is
compiled from g17p_offscreen_triangle.c with AGX_ROBUST_INDEX_BATCH_PROBE=1;
these outcomes are synthetic witnesses, not modified CTS passes.

Use workspace tools/m3-robust-index-batch-probe.py with --prefix, --sha256 and
--output; each witness gets its own bounded watchdog/thermal guard. Original
receipts: build/m3-robust-index-batch/ and benchmark diagnostics/
m3-robust-index-batch-20260930/paired-v4/. Six broader game image checks also
pass (session m3-robust-index-batch-v4-20260930); Minecraft GL, native Vulkan
Minecraft, Satisfactory and 0 A.D. are exact, with Factorio/SuperTuxKart within
the previously accepted one-level tolerance.

The earlier first batching checkpoint passed 28 witnesses and the same CTS
selection. Its separate paired medians were 52.2832 FPS control and 69.5190
FPS candidate (+32.97%). Keep those receipts as historical measurements;
the final paired timings above establish the current improvement.


## Bounds-padding correctness follow-up

Review found two pre-existing bugs in the shared padding helpers. An unaligned
source was advanced without advancing the destination; a short source tail
could also underflow the unsigned remaining length. Preserve every source and
destination byte offset with a bounded parallel byte copy for unaligned input,
retaining the aligned vector copy. The zero suffix was limited to 32 bytes,
but 32 zero indices require as many as 128 bytes. Each of the 32 lanes now
zeros every assigned byte, with a bounded stride loop.

The updated exact-pixel witness passes 38 cases, each submitted twice, adding
8-bit indices, long (32-index) suffixes and long aligned/unaligned copies.
The buggy short-byte-input path was identified by source review, not deliberately
run to a GPU fault. Results do not prove an additive FPS benefit of these rare
out-of-bounds corrections. The final padding candidate (84f175790579) completes a fresh paired Minecraft
run: control 57.0233, 57.9915, 47.5330 FPS (median 57.0233), candidate
68.5477, 68.3509, 69.9214 (median 68.5477), +20.21%. Candidate images are
pixel-identical. Against the fresh M1 Zink median 50.7314, the M3 candidate
is 35.12% faster. Do not combine separate experiments' medians to invent a
larger paired speedup. Final follow-up completes healthy: the same 975 CTS cases (469 Pass,
506 NotSupported, no failures), all 38 synthetic witnesses with batching
**disabled**, and all six game image checks. Four games are exact; Factorio
and SuperTuxKart differ by at most the already accepted one RGB level.
The replay qualification required no reboot, kernel recovery or desktop
installation change.

Two CPU-bookkeeping prototypes (708107575304, 90351377fa12) produced lower
paired medians than the first batching build, despite exact Minecraft images.
They are rejected and their original samples retained; no additional performance
win is claimed. The final padding candidate retains the first batching layout.


## Final dispatch correlation

An instrumented final replay records 330 robustness dispatches (4096 global
threads / 32 local threads each), replacing the earlier 40860 one-subgroup
launches. Subgroups beyond the recorded table count return without reading
arguments. Other application/meta compute dispatches are retained. The new
kernel uses 58 register slots and 16 scratch bytes, matching the prior helper's
register/scratch footprint. These record counts establish batching, not an
individual shader's measured execution time.

Final instrumented process CPU time is 99.570 ms over 30 frames (89.608 user,
9.962 system), against the earlier 182.513 ms diagnostic. Global compute
retirement spans fall from roughly 115 ms in the earlier diagnostic to 10.963
ms here; tiling/fragment spans remain about 80/137 ms. Those counters include
other clients and overlapping engines, so they are not an additive frame-time
breakdown. Clean paired timings above remain the performance result.

The usual display manager (plasmalogin.service on this installation) remains
active. All hardware experiments used the 60-second watchdog and all-channel
0.5-second thermal monitor. M4 is not qualified by these M3 results.

## Separate application startup check

A bounded SuperTux launch initially requested unsupported GLX on the private
Xvfb display and exited before creating a GPU context. The mandatory guard
retained the watchdog and rebooted; the original failure, empty kernel delta
and absence of firmware fault snapshots are retained separately from the
healthy replay qualification. Plasma returned with healthy idle GPU activity.

Forcing SDL's EGL path and using null audio permits a healthy 12-second startup
with the exact candidate Vulkan/Gallium libraries mapped. Direct GL readback
shows the level-intro title and sprite. The Xvfb screenshot and later GL frames
are black, so this is a startup/context check, not a gameplay image pass or a
live Minecraft performance result. No test application or private X server
remains running. Evidence: build/m3-robust-index-batch/live-supertux/.
