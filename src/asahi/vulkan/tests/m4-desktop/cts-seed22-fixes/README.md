# Seed 20260922 fixes

Original reports remain frozen in `../cts-seed20260922-1000`.

## X8_D24 rendering crash (697)

Render state now validates the physical depth plane (Z16 or Z32), consistent
with attachment addressing and clear encoding. X8_D24 uses the existing
Z32 emulation, with logical-format depth bias preserved. The original crashing
compare-operations case passes on J713; peak 59.25 C, 60-second watchdog,
no GPU fault. Exact source patch/binary identity in the retained experiment
`20260913T235918400096Z-cts-seed22-depth-fix`.
