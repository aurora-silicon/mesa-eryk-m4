# Vulkan regression fixes, 2026-09-27

## Transform feedback metadata overflow

The full M4 catalog crashed at index 3071413,
`dEQP-VK.transform_feedback.simple.max_output_components_128`.
The core showed shader serialization reading corrupt pointers after 128 scalar
XFB descriptors had been copied into a 64-entry array. The release build removed
the assertion that was the only bounds check.

Size the storage for all NIR varying components, including both 16-bit halves,
and check the bound in release builds before copying. Increment the shader
binary version because the serialized metadata layout changed.

Candidate library SHA256:
`add52a02148b99f1b0568dc6c932dd40c58aeb71f23a441d1b72b28138b9c507`.
M4 isolated crash reproducer: Pass. All 7,891 simple transform-feedback cases:
4,988 Pass, 2,903 NotSupported, no failures/crashes. Watchdog and thermal guard
passed, peak 69.25 C. The original full-catalog crash remains in its results;
these are separate regression results, not overwritten outcomes.

Host evidence: `build/cts-fixes-20260927/m4-xfb-{fix,neighbors}` and
`_provenance/experiments/20260927T103948885650Z-m4-xfb-metadata-neighbors`.

## Additional fixes

- `cfed98f5d18`: preserve dynamic advanced blend operations when a later
  pipeline bind supplies static blend equations. Also copy the clamp flag
  with the advanced blend parameters. All 36 original failing cases pass.
- `b9c7476511e`: lower unbound static vertex inputs to typed (0,0,0,1)
  constants before compacting locations. Fixes both monolithic pipeline
  creation failures without changing advertised maintenance9 behavior.
- `04f14f001d5`: widen float16 producer/consumer IO to the native 32-bit
  interstage representation before tessellation lowering. Narrow in the
  consumer using the original type; retain independent XFB widths.
  All 96 original float16 tessellation failures pass on both GPUs.
- `0ac1527e3f1`: report the M3 shared multisample mask for sample locations,
  matching the qualified rendering path instead of the old single-sample gate.
- `3c41f856b99`: free the owned input NIR on the XFB capacity error path.

Final library (same bytes on both devices):
`912b52dd8c52f7a693e5ee84193cff891fe728ef308d7706b98a5362ce39ead8`.
Installed in `/opt/asahi-m3m4-cts-20260927`; baseline library retained.
Final source commit `0ac1527e3f1`.

### Regression coverage

| Selection | M3 | M4 |
| --- | --- | --- |
| Original 134 failures + XFB crash | 134 Pass, 1 NotSupported | 135 Pass |
| 19,635 surrounding cases | 5,125 Pass, 14,510 NotSupported | 5,125 Pass, 14,510 NotSupported |

The M3 unsupported original reproducer is the 128-component XFB case: its
advertised limit is 116. Its limit was not changed or bypassed.
The neighborhood consists of all catalog cases matching sample_locations,
pipeline vertex_input.misc, extended_dynamic_state color_blend_equation_advanced,
and spirv_assembly graphics 16bit_storage input_output. It includes 3,621
passing sample-location tests, 252 advanced-blend tests, 46 vertex-input tests,
and 1,206 16-bit IO tests on each machine. See neighbors-summary.json for
exact selection hash, counts and evidence paths.

The initial oversized neighborhood batches hit their existing runner bounds
(100 s M3, 180 s M4); no GPU failure was observed. Original incomplete results
remain retained. Only unfinished cases were continued in separate 512-case
bounded batches; no per-case, watchdog or thermal limits were increased.
The neighborhood build precedes the final commit's NIR error-path cleanup
and comment edits; the final committed binary re-passed all original reproducers.

Four allocation-callback QualityWarnings per GPU remain classified as warnings.
Mesa's shared vk_descriptor_set_layout and vk_pipeline_layout implementations
intentionally allocate from the device allocator because reference-counted
layouts can outlive the API handle. No dummy allocation was added to silence CTS.

### Kernel progress fix and full-catalog continuation

M3 kernel `4acd2bfb5b79` (published `c07786baa30b` on eryk_m3_gpu) exposes
host-verified completion count/time, generation and latched health via
`/sys/kernel/debug/asahi-m3/progress`. The watchdog consumes this state instead
of printk. Routine MMU and submission messages are opt-in. The default 128 KiB
printk ring was restored. A deliberate log-reader overrun alongside 88 passing
render tests produced 376 verified completions and a normal guard completion.
Kernel evidence is tracked under drivers/gpu/drm/asahi/tests/m3-progress-state-20260927.

The full-catalog campaigns resume with revision.json and approved installed
identities, preserving the original manifests, catalog indices and all original
terminal outcomes. Every new segment records its identity, environment and
revision. This is a versioned regression campaign, not a claim that one binary
has passed every catalog case, and the existing corrected CTS build is not an
unmodified official conformance submission.

### Tools

Copy `tools/cts-regression.py` back to the workspace tools directory to use it.
It runs selections with the existing M3/M4 watchdog/thermal guards, refuses
concurrent full-catalog workers using the per-machine lock, preserves metadata,
and accepts `--batch-size 512` for separately guarded segments. Example:

```
python3 tools/cts-regression.py --machine m3 --cases CASES.txt \
  --output build/unique-regression --prefix /opt/asahi-m3m4-cts-20260927 \
  --batch-size 512
```

The paired controller's `--revision` option changes only explicitly pinned
future execution; it does not replace old results. A matching entry with a
reason in approved-identities.json is still required, in addition to the
expected CTS and Mesa hashes.
