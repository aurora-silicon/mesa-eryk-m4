# Preserve unused NONE depth/stencil attachments

A color-only render pass with LOAD_OP_NONE / STORE_OP_NONE must not overwrite
an attached but unused depth/stencil aspect. Forced stores for partial rendering
wrote zero tile depth into application memory. Subsequent LEQUAL-tested text
therefore disappeared. The private application trace and assets are retained
only in the local benchmark repository.

Track whether each aspect is actually tested in each control stream, including
replayed secondary command buffers and meta draws. At graphics finalization,
unused NONE/NONE aspects issue no load or store. Used aspects preserve initial
memory through partial streams and conditional writes. Other load/store cases
retain existing behavior. Null image views do not enable aspect tracking.

## Reproducer

`cc -O2 probe.c -o probe -lvulkan` builds the standalone Vulkan 1.4 test.
The embedded shaders come from test.vert and test.frag (glslangValidator -V).
`./probe d24 none` initializes depth to 1.0/stencil to 0x5a, performs a color-only
NONE/NONE pass, then tests and reads back those values. Formats d16, d24, d32,
s8 and modes none, readonly make eight cases. Hardware execution requires the
workspace GPU experiment wrapper, verified 60-second watchdog and thermal guard.
Select the tested ICD explicitly; the probe refuses devices other than M4 or
llvmpipe. This is not evidence of M3 execution.

Before: all four NONE cases failed every pixel; four readonly controls passed.
Fixed: all eight passed. Lavapipe: all eight passed. No new GPU fault; guarded
peaks 68.25 C before and 67.25 C after. The initial fixed probe predates only the
null-view guard, which is exercised by the final-build CTS run.

Final-build CTS: all 273 frozen-catalog load_store_op_none cases passed,
including dynamic rendering, primary and secondary command buffers, mixed
aspects, disabled tests/writes, and neighboring load/store controls. The exact
catalog indices/hash and executable hash are retained under cts/. This focused
regression run is not a conformance submission. Peak 67.5 C; guard passed.
The first CTS attempt used an absent executable path and ran zero cases; it is
retained in workspace experiment 20260924T015935682274Z-depth-none-cts.

The unchanged short application trace now renders its missing menu text.
The original 600-frame stream also completes and its final RGB image exactly
matches the corrected short replay: experiment
20260924T020428637104Z-frostpunk-original-depth-fixed, 771 thermal samples,
peak 106.25 C, valid watchdog and no new GPU fault. Live application and true
4K capture qualification remain separate and pending.
