# Opt-in M3 vertex constant-program experiment

The G15S-only `HK_M3_VERTEX_PREAMBLE=true` switch appends the compiler's
constant program before the vertex loader's STOP. It is disabled by default.
`AGX_NATIVE_FLOAT_PREAMBLE=true` also permits float arithmetic extraction.
The loader preserves the 128-bit input/output uniform boundary, the existing
input-publication drain and scratch fallback. Other chips are unchanged.

Prototype binary 0ed374fca745b2dbda66a5e74b00bfc62c4351beca3f9025df292887c6796dec
completed guarded Minecraft replays on boot
860d2479-a1a2-453c-b1ad-6481cd4f982c. Three saved frames were pixel-identical
to the original reference. A common VS shrank from 489 to 315 instructions.
The clean three-run session `m3-vertex-preamble-clean` did not improve the
97.26 FPS production result; retain this as experimental, not a default or
performance claim. The public benchmark selection is unchanged.

Host Ioto composition and VDM admission tests pass, including short output,
bad STOP, overlap and unsupported-chip rejection. The full CTS selection has
not been run for this experiment. Constant-program register provisioning and
stage timing need further qualification before any production enablement.

Evidence is under workspace `build/minecraft-inputs-next/` and benchmark
`diagnostics/m3-vertex-preamble-{probe,clean}.log`.
