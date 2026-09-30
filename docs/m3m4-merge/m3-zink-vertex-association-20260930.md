# M3 Zink vertex UBO promotion and floating-point association

The full saturating-bound matcher prototype removed repeated address and
residency lookups, but changed 71–72 pixels per retained Minecraft GL frame
by more than one RGB level. The fragment-only matcher passed the initial Minecraft/CTS selection
and was committed as `b13d03c0bc5`; the expanded Satisfactory check below
subsequently found a small image regression.

## Root cause and offline isolation

The same-source compiler captures have the same input floating-point DAG:
`((a*x + b*y) + c*z) + translation`. In the old program, the translation
load is not recognized as convergent. In the promoted program it is a uniform,
so `nir_opt_reassociate` ranks it ahead of the divergent products. The final
program becomes `fma(c, z, fma(b, y, fma(a, x, translation)))`, whereas the
old program adds translation last. This changes rounding before rasterization
and texture sampling even though both expressions are algebraically equivalent.

The offline compiler replay now accepts `--no-reassoc`. It adds NIR
`no_reassoc | no_transform` only where the opcode supports those controls,
retains all existing controls, and leaves FMA contraction permitted unless the
source already forbids it. It does not disable the usual code-motion policy.
This is a diagnostic transform, not proof of hardware correctness by itself.

Example (from the workspace root):

```
AGX_MESA_DEBUG=nopromote,shaders \
  build/mesa-m3m4-host/src/asahi/compiler/agx_shader_replay \
  /path/to/capture/input.bin --no-reassoc
```

Retained same-source bundles in benchmark diagnostics
`m3-zink-bottleneck-20260930/candidate-vertex/`:

- Old: `09da96b36913aa44b7ca8358eb60bab87b50a9ea9b7adf86714cf5b8d6d00754`
- Promoted: `ccb8bcdc4ebbe2761ebc4cffc3960b9fddff4848d649ccdd4f0348aa12e28b62`

The promoted replay with this transform restores the old matrix arithmetic
order and retains fused operations. Logs are in workspace
`build/m3-zink-bottleneck/vertex-rounding/`. The first diagnostic attempted
`no_reassoc` alone; NIR validation correctly rejected its missing
`no_transform` implication. That failed offline attempt is retained.

## Hardware candidate (qualification in progress)

Candidate `/opt/asahi-m3-zink-vertex-preserve-c754a5c8fe57`, Vulkan SHA256
`c754a5c8fe57543e1082f1a3faf49c625d50b964731ed6d1e13ca4b44748f69a`,
enables the saturating matcher for vertex shaders and preserves association
when the UBO promotion pass changes a vertex shader. It does not relax bounds,
residency, descriptor mutability, or GPU execution-time loading requirements.
This may constrain other vertex arithmetic optimizations; broader replays and
CTS must be checked before considering it qualified.

The first guarded Minecraft OpenGL image replay completed successfully. Each
of the three retained frames differs in only two pixels, each by at most one
RGB level. This meets the user's existing one-level tolerance; it is not an
exact match. Timings and broader validation are pending. The desktop is unchanged
and M4 has not been touched.

## Follow-up isolation checkpoints

- Candidate `c754a5c8fe57`: 51.8992 FPS median against the fragment-only
  control's 32.7036 FPS in three alternating pairs (+58.70%). All samples are
  retained, including the candidate's 47.0712 FPS low sample. Its broad
  vertex arithmetic restriction changed native Vulkan Minecraft, so this is
  **not a production-qualified score**.
- Candidate `02ff32cdd46b`: only newly recognized saturating vertex loads
  trigger the arithmetic safeguard. Native Vulkan Minecraft now matches its
  original reference exactly. Satisfactory showed 4/21/11 pixels above one
  level (maximum 2/3/2). The fragment-only control is exactly identical to this
  candidate on all three frames, locating that discrepancy in the earlier
  fragment change rather than the new vertex change.
- Candidate `ae69e093f92e`: extending the blanket safeguard to newly promoted
  fragment shaders retains Minecraft GL within one level, but increases the
  Satisfactory discrepancy. Rejected.
- Candidate `8058b14266a2`: following ALU/phi dataflow from promoted loads
  instead of restricting every expression reduces that discrepancy, but still
  fails Satisfactory's existing one-level threshold. Rejected for fragment
  use. Its graph walk is iterative and cycle-safe; it stops at textures and
  memory operations rather than constraining independent filtering math.

The current experimental checkpoint retains the vertex improvement and adds
`HK_SATURATING_UBO_BISECT=true` to select expanded fragment promotion through
`NIR_SHADER_BISECT_LO/HI`. This is diagnostic only: every run records its
selection and disables shader caching. The existing guarded
`tools/shader-bisect-m3.py` narrows the first-frame Satisfactory discrepancy.
No tolerance was relaxed; no rejected score or image replaced results.html.

## Late-promotion experiment (not qualified)

The Satisfactory bisection isolated source
`c2004198eb9517e41ac0802487230ec7f0cff85517b773d58d755aea88f0e6b7`.
Restricting reassociation of its adds, multiplies, or FMAs separately, and
skipping the second preprocessing pass, did not remove the first-frame
four-pixel discrepancy above one level. Its exact mechanism remains open.

This experimental branch now retains the original UBO read as the source of
`load_prefetched_agx`, then replaces the marker with its qualified uniform
only after arithmetic optimization. Original read divergence is retained
until then; native uniform-prefix relocation includes the marker. Host
prefetch/relocation tests pass, including a full synthetic compiler run.

The first isolated ARM candidate `3a1517732ad4` failed in the replay process
with SIGSEGV before producing images, in 5.07 seconds. Evidence is under
benchmark run `20260930T134701421451Z-m3-satisfactory-vulkan-gameplay-check`.
The guard retained the watchdog and recovered the machine. This checkpoint
is diagnostic work, not a qualified driver; the desktop remains on its
previous installation. No benchmark score/reference was replaced.

### Crash diagnosis: stale cross-build generator

A guarded GDB replay (`20260930-late-ubo-gdb-noshell`) stops in
`nir_serialize.c:read_intrinsic`, reached through `poly_pre_gs` and
`poly_nir_lower_gs`. The separate host `vtn_bindgen2` executable had not
been rebuilt after adding a NIR intrinsic, so the ARM build embedded helpers
serialized with the old intrinsic table. This is a build mismatch, before
executing the affected shader. Rebuild `mesa_clc`, `vtn_bindgen2`, and
`asahi_clc` in the matching host tree, then rebuild the ARM library.

`tools/m3-stage-vulkan-candidate.py` now performs that ordered incremental
build for its default tree before hashing/staging the library. A source copy
is retained in this document's `tools/` directory. Explicit custom libraries
remain the caller's responsibility.

GDB must use `set startup-with-shell off` here: the SSH shell startup files
otherwise overwrite the requested Vulkan environment with the desktop's
previous driver. The initial GDB probes therefore exercised that previous
build; their captured ABI exposed this mistake. Normal guarded replays use
direct `exec` through Python and were not affected. One such diagnostic hit
its time bound and recovered through the watchdog; neither is a candidate
performance or correctness result. The corrected stack capture completed
within the guard, retaining its userspace fault as diagnosis only.

### Preserving the general executable

Rebuilding the generators removed the crash. Candidate `fd35180bed56`
then produced three **pixel-identical** Minecraft GL frames, but the
Satisfactory first frame still had four pixels above one RGB level. Deferring
promotion in the direct executable alone had not changed the general
fragment variant's earlier transformation.

Candidate `7349063a5845` retains the original newly matched reads in the
general executable, while keeping their metadata to build/qualify the separate
direct variant. Only that qualified variant uses late uniform substitution.
The first Satisfactory frame now matches the original reference exactly
(`20260930T140535920125Z-m3-satisfactory-vulkan-gameplay-check`). A unit check
requires the general variant to retain the original arithmetic source. Full
retained frames, other games, targeted CTS and paired timings are pending.

The generator now emits compile-time checks for intrinsic/opcode counts and
numbering in each generated C++ helper library. Future mismatched numbering
fails compilation with a request to rebuild `vtn_bindgen2`, rather than
reaching the NIR reader. This is deliberately a numbering check, not a claim
that every possible NIR serialization change is detected. The matching
helper builds; negative probes with a stale count and with changed numbering
at unchanged count both fail at the generated assertion. Probe receipts:
`build/m3-zink-bottleneck/vertex-rounding/opcode-guard-test/`.
