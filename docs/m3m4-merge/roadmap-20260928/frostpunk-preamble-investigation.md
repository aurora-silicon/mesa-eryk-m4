# Frostpunk memory-preamble regression

The M3 broad replay campaign reproduces about 17,330 changed RGB pixels per
frame (maximum channel error 27) in Frostpunk DX11 gameplay with native and
read-only memory preambles enabled. The same full package without those options,
and with arithmetic-only preambles, passes the existing reference policy. A
cold-cache repeat of the original package reproduces the failure. No reference
image or tolerance has changed; preambles remain opt-in.

Bisection identifies fragment source
`1e6eb9e054aca0ac56af230d3a751028d52393b57e3f81c02bd5ec5c28a8cd6f`,
also implicated in the earlier buffer-address regression. Excluding memory
motion for that source restores the sampled image. This is diagnostic evidence,
not a shader-specific production workaround.

## Dependency controls

Correctly rebuilt, shader-selected controls for source-register lifetime waits,
scoreboard slot reuse, explicit dependency waits and full serialization all
retain the discrepancy: approximately 17,340 changed pixels, maximum 27.
None is a demonstrated fix. Full serialization also covers the main shader.

An earlier diagnostic build series falsely appeared to pass: a stale object
still contained the previous shader-exclusion experiment after source had been
restored. Its undefined symbol table still referenced `nir_shader_bisect_select`
although the restored translation unit did not. The same-build no-change
control exposed this problem. Those results remain retained but are invalid
for diagnosing waits. Explicitly rebuilding the affected translation unit
restored the failing control. Always wait for a diagnostic build to finish
before restoring source; rewrite restored source with a fresh timestamp and
rebuild the affected objects. A clean Git tree alone does not validate an
incremental build's provenance.

Qualified diagnostic binary (dependency variants): SHA256
`e2c3a3f4ef5fab062c6fe24aff389adeb62975f989520edd88b19eda30ab9a73`.
Full-serialization binary: SHA256
`aebc820536a3648e471a5cd815e35b98967867b4aeb4fb385c205865b6cda941`.
Private compiler captures, diagnostic patches and original/invalid/corrected
results are retained in gpu-replay-bench. No diagnostic default or normal
desktop installation changed. All runs retain watchdog and thermal guards.

## Memory-read boundary

A selected-source diagnostic cumulatively admits memory definitions by their
indexed pre-preamble NIR definition. The last matching limit is 694 (8 changed
pixels, maximum 1); limit 695 reproduces 17,346 pixels, maximum 27. This permits one
additional scalar 32-bit `load_agx`: definition 694, reading UBO byte offset 132 after
native sparse-address translation. Its value contributes to the Y texture
coordinate. The surrounding arithmetic remains per-lane. These are diagnostic
frame 1 results, not a full-capture qualification or a production restriction.

The two retained host compiles have identical floating-op counts in their
lowered main NIR, but different address/control-flow and register allocation.
The complete failing compiler input has 404 uniform halves, 64 preamble GPR
halves, zero preamble scratch and no promoted-constant rodata. That rules out
an overlap with a promoted-constant range for this shader. The compiler replay
tool now prints those resource boundaries alongside its existing statistics;
its generated shader binary remains byte-identical to the retained baseline.
Disabling preamble float comparisons also retains the error.

## Additional isolation controls

Disabling register-cache hints, instruction scheduling or floating move folding
retains the image error. Disabling all backend optimization cannot render this
input: the compiler refuses an unlowered register-shift `extr`, and replay
aborts at pipeline creation. This is an inconclusive diagnostic, not a GPU hang;
the guard subsequently allowed watchdog recovery after the failed command.
The following boot is `83fdd197-a255-45d8-b01d-f1ab64afb228`; no firmware fault
snapshot was recorded. The normal desktop returned.

Clearing the original-coverage marker on the backfacing special-register read
also fails (17,541 pixels, maximum 27). An unconditional control-flow push/pop
at main-shader entry fails (17,341, maximum 27). Shifting all generated uniform
slots by 8 or 16 halves fails as well (18,233 / 17,344 pixels, maximum 27).
None of these temporary changes is included in the production compiler.

A second diagnostic duplicates definition 694 as a volatile main-shader read,
compares its bits against the preamble value, and substitutes a large coordinate
on inequality. Compiler inspection confirms both reads and the comparison remain.
At the 695 boundary, this restores the sampled image (9 pixels, maximum 1), but
it also changes uniform allocation and restores address control flow. With all
memory motion admitted, the discrepancy remains (17,328, maximum 27). This does
not establish a bad UBO value or a repair; it narrows the search toward the code
changed by that instrumentation. All results use the existing fixed reference.

## Draw boundary and normal-buffer isolation

Frame-indexed resource snapshots identify pipeline 117877, secondary draws
42253, 42257 and 42261, submitted at 42721. All four attachments match before
the first draw. Only attachment 1 (A2B10G10R10, the normal output) changes:
509 pixels after draw 42253, then 1,541 and 2,008 after the next two draws.
The other three attachments and the three fragment UBO snapshots match exactly.
This localizes the discrepancy before subsequent lighting/postprocessing.

M4 cold-cache replay matches (8 changed pixels, maximum 1). Its captured compiler
key is identical to M3's; its frontend NIR adds generation-specific texture-LOD
bias correction. Both compile a 5,897-instruction, 64-half-register preamble
without scratch. The M4 main shader has 16 bytes of scratch versus M3's zero.
M4 is therefore not merely avoiding the problem through a preamble fallback.

Explicit fine/generic derivative emulation retains the error (17,340 pixels,
maximum 27). The normal calculation instead uses coarse derivatives; explicit
coarse quad broadcasts change the image further (37,931 pixels, maximum 80),
so neither transform is a repair. An exact nonlinear fine-derivative probe
(x*y, whose derivatives are y and x) passes on both GPUs. A first draft of the
instrumentation inserted fsub after its lowering pass and failed in the CPU
compiler; the corrected diagnostic was compiled locally from the captured input
before hardware execution. These altered shaders are diagnostic only.

## NaN isolation (investigation, not reference approval)

A source-selected diagnostic appends raw register copies after the final main
shader dependency drain, leaving its IR, scheduling and register allocation
unchanged. It routes normal RGB or alpha into attachment 2 (RGBA16F), forcing
that attachment's source alpha to 1 to avoid blending away the evidence.
Alpha readbacks match exactly. Normal readbacks identify 970 pixels where the
ordinary M3 result is NaN in all three RGB channels and the preamble result is
finite. There are no NaNs in the latter, no shared NaNs, and every other finite
pixel agrees exactly. Thus the image discrepancy does not establish that the
preamble path is the incorrect one.

An independent lavapipe replay replaces just the target fragment's attachment-2
stores with its normal RGB and alpha 1. All 970 locations are finite there too,
although normal directions differ from M3. This software replay explicitly uses
unsupported-feature removal and SPIR-V instrumentation; it is diagnostic, not
qualification of the original application or grounds for replacing references.
Investigation now follows the derivative-based normal/tangent normalization.
Explicit fine quad differences for all derivative variants still differ from
the reference (17,335 pixels, maximum 30); this is not a fix.

Diagnostic output-copy Vulkan library SHA256:
`5070dae7b8191de214c20ddfb62c7a032459f13a7e031986e98230bbc264986a`.
All original screenshots, references and guards are retained unchanged.
