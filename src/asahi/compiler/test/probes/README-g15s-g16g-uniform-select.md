# Uniform conditional-select routing

Own `g15s-g16g-uniform-select.metal` compiled offline with AIR-NT32023.921
on mac-m5, targeting applegpu_g15s and applegpu_g16g with
`-fno-fast-math`. This is compiler evidence, not GPU execution on mac-m5 or
M4 hardware qualification. Both targets emit identical complete 246-byte
fragment text in the accompanying hex. The main range is [64,246).
Both complete walks re-encode every instruction with no unknown span.

FSELs at148/158/186/196 independently witness uniform comparison and
selected-value operands with masks6/9/6/10, and EQ, LT and tied NaN-aware
MAX. The portable encoder checks complete native bytes, preserves all GPR
hints, and rejects invalid widths, alignments, waits and untied MAX.
Native uniform operands carry no GPR lifetime hint. Ordinary LT/GE/EQ
condition and minifloat-alternative fields retain the existing qualified
routing; their combinations with uniform routing require hardware checks.

Raw commands, compiler identities, archives, metadata and complete walks
remain under `build/m3-uniform-fsel-native-20261004a`. The current prototype
is opt-in and has no desktop or performance qualification yet.
