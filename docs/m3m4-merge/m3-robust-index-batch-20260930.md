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
| descriptor-decode-once control | 57.5165, 52.2832, 50.9858 | 52.2832 |
| batched robustness | 63.8689, 70.0966, 69.5190 | 69.5190 |

The median improves 32.97%. First/middle/last saved frames are pixel-identical.
A fresh installed M1 Zink control gives 50.6098, 50.7314 and 51.5484 FPS,
median 50.7314: M3 is 37.03% faster in this replay. These are replay scores,
not live Minecraft/Java FPS or native M1 Vulkan scores. M1 has separate kernel,
Mesa and thermal telemetry qualification limits recorded in the JSON receipts.

An instrumented M3 interval consumes 98.218 ms process CPU across all replay
threads, versus 182.513 ms for the earlier instrumented control. Instrumented
runs include profiling/completion fences and do not supply benchmark scores.

## Qualification

Frozen indexed-draw/robustness/multiview/conditional selection: 975 cases,
469 Pass, 506 NotSupported, zero failures, healthy completion. Retain every
catalog index and skip reason; this is not full conformance. Conditional
rendering coverage is unsupported on this device, so its fallback remains a
source-reviewed exclusion rather than a tested application claim.

28 separate exact-pixel witnesses pass, each submitting the same command
buffer twice. They cover 2/127/128/129/257/2048 draws, first/middle/last selected
entries, partial/all out-of-bounds 16/32-bit indices, concurrent padding/zero
allocation, zero counts/instances and indirect-count predication. Arguments and
counts are GPU-written after recording-time zero initialization. The oracle
checks every RGBA8 output byte and both 64-byte readback guards. The probe is
compiled from g17p_offscreen_triangle.c with AGX_ROBUST_INDEX_BATCH_PROBE=1;
these outcomes are synthetic witnesses, not modified CTS passes.

Use workspace tools/m3-robust-index-batch-probe.py with --prefix, --sha256 and
--output; each witness gets its own bounded watchdog/thermal guard. Original
receipts: build/m3-robust-index-batch/ and benchmark diagnostics/
m3-robust-index-batch-20260930/paired-v1/. Broader game checks are pending.
