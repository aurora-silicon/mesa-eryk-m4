# G16G early fragment tests with sample mask and discard

`dEQP-VK.fragment_operations.early_fragment.{sample_count,samplemask}_early_fragment_tests_depth_samples_{2,4}{,_maintenance5}`
and `discard_early_fragment_tests_depth` (7 cases) stored colour for samples a
fragment shader with `layout(early_fragment_tests)` had killed through
`gl_SampleMask = 0` or `discard`. Single-sample discard kept only its query
count wrong; at 2x/4x one or two samples per pixel kept the fragment colour.

## Witnesses (AIR-NT 32023.917, applegpu_g16g)

`g16g-early-sample-mask.metal` and `g16g-sample-mask-values.metal` compiled
with `build/claude-fix/witness/mcompile.py --stage render --samples N`
(outputs `build/claude-fix/witness/out-<function>-s<N>/text-1*.bin`):

- `fsearlydiscard` (1x): `pixwait 512; pixwait 1; movmsk 571454000001; ...
  movmsk 572454022001; pixrel 0x201; end`. Class 1 is acquired once before
  the initial full-coverage MOVMSK and released after the discard's MOVMSK,
  which stages its word in halves 2..3 on slot 2.
- `fsearlymask0` (2x/4x): one MOVMSK with the full sample set (imm 3 / 15),
  `pixrel 0x201`, no colour store. `fsearlymaskdiscard` likewise ends
  without a store.
- `em2`/`edyn` (2x, early tests, mask 2 or dynamic): the initial MOVMSK
  carries the full set; the mask is applied only to the colour store:
  `pixwait 8; lldcol` (current tile colour), then two `lstcol` with register
  sample masks (f51=2, f44=3/2) built from SR39 re-read after the tests
  (`rld 34c21426`, `rld 34c21046`, `bit_and` with the mask and its
  complement) under `pixwait 4`, then `pixrel 0x20c`.
- `m1..mf`, `fsmaskaa` (no early tests): the staged coverage word's low
  nibble is the live mask itself (1, 2, 3, 5, 15, 0xA); `m0` folds to a
  constant kill and no store; `m2` at 1x stores with an immediate mask 1.

So after forced early tests the hardware coverage of a multisample pixel is
not reduced by a later MOVMSK; the shader masks its own colour store.

## Change

- `agx3_pack_binary.c`: coverage updates in a straight-line fragment shader
  acquire class 1 once and release it after the last update; the second
  update stages its word in halves 2..3 on slot 2.
- `agx_nir_prolog_epilog.c`: the G17P split keeps `demote_samples` in the
  main shader (grounded 1x MOVMSK) and also accumulates the written-sample
  mask, exported as `AGX_ABI_FOUT_WRITE_SAMPLES`; the epilog masks its
  `LSTCOL` with it through the register sample-mask form. At 1x the native
  canonicalization folds the operand to sample zero.

Hardware: the 7 cases pass individually (`claude-ef-one-1..7`, QPA
`Success`); reg1000 815 Pass / 186 NotSupported (`claude-reg1000-earlymask`);
1,500 previously passing multisample, sample-mask, discard, depth-stencil
and occlusion cases from the 1.0M-1.2M results pass (`claude-earlymask-reg3`)
and 387 from the 0-1M results pass (`claude-earlymask-reg1/2`). The
old-colour restore Apple performs for the masked-out samples was not needed
for the cleared attachments these cases use and is not implemented.
