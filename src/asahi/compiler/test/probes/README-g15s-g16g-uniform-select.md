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
is opt-in (`AGX_NATIVE_SELECT_UNIFORMS`, default false), restricted to ordinary
AGX3B fragment shaders. It reuses already-pushed float32 uniforms while keeping
literals in the ordinary register/immediate path to avoid an extra constant
upload. Both constant-use counting and replacement follow this rule.

On M3, 335 compiler tests, the portable native-byte checks and all three
Minecraft replay reference images pass. The frozen 3,675-case texture/arithmetic
selection gives 2,763 Pass, 912 NotSupported, no failures or device loss. Two
nine-pair completed-frame timing studies give median paired gains of 0.570%
and 0.234% (17 of 18 positive); combined 0.404%. This is a small opt-in replay
gain, not macOS parity, live Java FPS, full conformance or M4 hardware validation.
Raw qualification remains in the separate gpu-replay-bench diagnostics under
`m3-macos-driver-gap-20261003/uniform-select-no-literal-upload`.
