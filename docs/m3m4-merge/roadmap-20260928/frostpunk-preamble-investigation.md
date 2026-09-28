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
