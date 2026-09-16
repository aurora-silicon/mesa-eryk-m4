# G16G sample fragment interlock — 2026-09-16

`VK_EXT_fragment_shader_interlock` now exposes `fragmentShaderSampleInterlock`
on J713/T8132 G16G. Pixel interlock remains disabled by default; shading-rate
interlock is unavailable. This is not full DirectX ROV or Steam qualification.

The compiler emits Apple's native ordered-storage release fence
`070454c40800`, draining outstanding asynchronous operations first. Storage-only
fragment parts enter the pixel protocol before waiting, and dependent passes
retain ownership across the linked sample loop. Native Metal witnesses and exact
composer round trips support the encoding; the hardware runs below establish
the advertised capability.

| Default run | Results | Peak temperature |
| --- | --- | --- |
| Full 576-case interlock family | 192 Pass; 384 NotSupported (192 pixel, 192 shading-rate) | 59.5 C |
| Six independent storage oracles | All Pass | 57 C |
| 283 fragment/coverage/subgroup regressions | 182 Pass; 101 NotSupported; no failures | 59.75 C |

Independent oracles use 64x64 fullscreen geometry, 128 ordered overlapping
instances (checking every prior counter value), or 16 unordered instances with
non-atomic bitmask accumulation. Each runs at 1x, 2x and 4x and checks exact SSBO
contents, colour output and 64 guard bytes. The four 1x/4x configurations pass
lavapipe too. Lavapipe rejects unadvertised 2x; no 2x reference claim is made.

Default tested Mesa SHA256:
`969e9ceefcb63717a5bc7ba2f44f3b3ab39daf7c71c595fbe29ee03de41ab143`.
Kernel module SHA256:
`a5affc7dcc90cd7b29ae230ff7324fbd8712d9aadab7419882038e16b95a4f86`.
Boot: `7ba01d95-ee19-4c0c-9877-e873aaffe7d7`.
Host suite: 104 Pass, the same 11 existing failures as before this change.
Every hardware run used the 60-second watchdog, six thermal channels at 0.5 s,
95 C stop threshold and bounded workload. CTS used 12 workers and 60 s/case.
The requested CPU maximum does not override the active thermal policy.

Evidence IDs:

- `20260916T184730520186Z-dx-interlock-sample-default-full-r1`
- `20260916T184925705868Z-dx-interlock-sample-default-oracles-r1`
- `20260916T185053506934Z-dx-interlock-sample-default-regression`

`fragment-interlock/` retains summaries, complete guarded evidence, case names
and frozen catalog indices, native witnesses, independent inputs/reference runs,
hashes and rejected diagnostic patches. Catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.

## Pixel interlock remains open

`HK_EXPERIMENTAL_INTERLOCK=1` exposes pixel interlock for investigation only.
Focused r1-r3 and r5-r9 each failed the same eight 4x/no-sample-shading cases.
Forcing sample shading passed the small r4 probe but failed 38 cases in the
full family (346 Pass / 38 Fail / 192 NotSupported); that change was removed.
Sample-shading CTS cases use per-sample storage, so their success cannot stand
in for exclusion across disjoint sample coverage of one pixel.

The original plain-store coverage diagnostic was invalid because clipping can
produce multiple partial-coverage fragments. Its corrected atomic-OR union
passes. Native non-atomic pixel storage still fails and remains unqualified.
Entry-gate, input-marker, wait-mask and barrier-placement diagnostics are
retained with their failures; unsuccessful speculative changes were removed.
See the archived `NATIVE-NOTES.md` for details and next investigation points.

## Diagnostic tools

Fragment interlock probes: `vulkan-fragment-probe.py --interlock sample|pixel
--instances N` requests the selected feature and draws overlapping instances.
`--host-env KEY=VALUE` records explicit compiler-shim environment overrides.

`native-form-decode.py TEXT.bin MNEMONIC OFFSET --scan` examines aligned
candidates with exact composer round trips. `TEXT.bin '*' OFFSET --walk` walks
consecutive instructions from a known entry until an unmatched or ambiguous
length. Neither mode proves field semantics or hardware behaviour.
