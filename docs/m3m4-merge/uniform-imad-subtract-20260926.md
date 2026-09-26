# Uniform multiply-subtract legalization

DX12 Satisfactory gameplay rejected a compute pipeline when native preambles
were enabled: `imad r7, r18, r15.neg` had no native encoding. The existing
full-width multiply-subtract legalization ran before uniform materialization
and admitted only ordinary SSA or immediate operands. Uniform multiplicands
or addends skipped it and became registers too late. Admit 32-bit uniforms
to that same modulo-2^32 multiply/subtract lowering. Narrow sign-extension
forms and the native encoder stay unchanged.

Validation:
- A focused end-to-end compiler regression fails before the fix and passes
  after it. It covers uniform multiplicand, uniform addend, both uniforms,
  and an immediate multiplier. All 306 optimizer/encoder tests also pass.
- M4 guarded numerical oracle: 65,536 lanes, four products/subtractions per
  lane, boundary and deterministic random inputs. All 524,288 buffer words
  match the independent Python integer reference. Its retained compiler input
  confirms 19 native preamble instructions; this actually exercises promotion.
  Guard `20260926T084717987012Z-dx12-uniform-imad-oracle`: 60-second watchdog,
  0.5-second thermal sampling, peak 61.5 C, no errors.
- M3 unchanged DX12 35-frame gameplay capture completes. Check
  `20260926T084549005602Z`: RGB frames 6/20 are identical to the prior reference;
  frame 35 differs by 1/255 in one pixel, within the unchanged 32-pixel /
  maximum 4/255 capture budget. No tolerance increase.

Qualified isolated library SHA256:
`9b5dc2efbf6ff11eb092461bc167a8e3d5123893df107ba401a334c5c1edce46`.
The preceding compiler rejections and cold-cache timeout are retained as failed
experiments; their configured watchdog recoveries are not successful replays.
This fixes the opt-in preamble path, with no desktop-default change and no
live-game or new performance claim. Exact libraries, guard logs, numerical
inputs and private capture evidence stay in the local benchmark repository.
