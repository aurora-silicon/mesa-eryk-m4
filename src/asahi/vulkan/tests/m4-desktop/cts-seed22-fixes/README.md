# Seed 20260922 fixes

Original reports remain frozen in `../cts-seed20260922-1000`.

## X8_D24 rendering crash (697)

Render state now validates the physical depth plane (Z16 or Z32), consistent
with attachment addressing and clear encoding. X8_D24 uses the existing
Z32 emulation, with logical-format depth bias preserved. The original crashing
compare-operations case passes on J713; peak 59.25 C, 60-second watchdog,
no GPU fault. Exact source patch/binary identity in the retained experiment
`20260913T235918400096Z-cts-seed22-depth-fix`.

## Attachment and tessellation exports (58, 342, 424)

All three original cases pass with the existing CTS binary in guarded experiment
`20260914T000310583679Z-cts-seed22-exports-v1` (peak 57.75 C). The fourth
case in that run is the still-unfixed cube gather and remains Fail.

G16G signed RGBA16 MRTs select the same I16 export as UINT, matching the
previously qualified single-target builtin; G17P retains its distinct path.
The vertex export bundler now accepts side effects before the first export,
without moving exports across side effects or control flow. The original
point-mode compiler capture now compiles offline and the CTS execution passes.
No-output rasterizer-discard TES uses its reserved position-only UVS layout;
its SSBO writes remain executed and checked by CTS.
