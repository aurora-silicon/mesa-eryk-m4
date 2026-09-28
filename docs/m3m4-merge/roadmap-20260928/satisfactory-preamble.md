# Satisfactory native Vulkan preamble follow-up

The ordinary current Mesa baseline and the read-only-preamble candidate produce
identical RGB values at frames 1, 16 and 31. Both differ from the old pinned
reference by 341/855/287 pixels, maximum 2/3/3. This matches the already documented
buffer-address compiler interaction in buffer-address-regression-20260926.md.
No new tolerance or reference was introduced. A diagnostic reverting only early
float16 widening leaves the same difference and was rejected; the CTS fix stays.
The old compute-idle-slots binary still matches the reference on today's kernel.

With the full previously qualified native-gameplay profile (preambles, memory
preambles, descriptor prefetch, compact prefetch, scaled branch and diagnostic
direct buffers), current Mesa b970144c6203 matches every reference RGB value.
Three warm 30-frame timings: 3.0572, 3.6303, 3.2270 FPS; median 3.2270. Kernel
8f1de23c1e73, packet32, desktop active. This is broadly the prior 3.33 FPS result,
not a new speedup. Thermal/clock evidence remains in each receipt. Screenshot
checks are separate from scored timings. Output is 3840x2160 with the existing
2880x1620 internal rendering and capture's explicit depth-bounds/shadow fallbacks.

This does not qualify direct buffers as a global default: Frostpunk and Lethal
Company retain the conservative path. Default-path buffer/branch compiler
correctness remains an open task. Results and images are committed only in the
private local benchmark repository; public Mesa documentation contains receipts.
