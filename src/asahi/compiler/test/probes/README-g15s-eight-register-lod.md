# Eight independent register-LOD banks

Own `g15s-eight-register-lod.metal` compiled offline with AIR-NT32023.921
on mac-m5, targeting applegpu_g15s with `-fno-fast-math`. This is compiler
evidence, not a GPU execution or M4 qualification. The complete 668-byte
fragment text is retained in the accompanying hex, SHA256 f2706b616f0003575dc1d670f126db52824c2a9ca811f04662eb6ef1c5b41fe0.
Constant entry is zero; the main code range is [64,668). Its complete walk
re-encodes every byte, with eight SMPs and no unknown span. The SMPs at
334,348,370,392,416,430,444,458 select banks 0,8,16,24,32,40,48,56.
`test_eight_native_register_lod_banks` asserts their complete 14-byte vectors,
including result slots and waits. Raw compiler commands, archive, metadata
and walk remain in `build/m3-eight-lod-native-20261004a/g15s`.

Expanding the current ordinary-fragment schedule from four to eight banks
was RGB exact on three Minecraft frames but did not improve its nine-pair
timing study. Encoding support is not evidence that more banks are faster.
