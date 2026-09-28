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
the M3 boot and desktop remain healthy.

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
