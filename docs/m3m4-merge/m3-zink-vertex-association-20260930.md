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
