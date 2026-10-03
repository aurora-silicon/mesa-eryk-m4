# Independent filtered LOD banks

Own fragment source `g15s-g16g-independent-lod.metal` was compiled offline
using AIR-NT32023.921 on mac-m5, separately targeting applegpu_g15s and
applegpu_g16g. Use `native-metal-compile.py SOURCE --host root@mac-m5
--stage render --function fs --target TARGET --output NEW_DIRECTORY`.
The default target remains applegpu_g16g; success is not GPU qualification.

Both fragment text sections are byte-identical, SHA256
4a632e57c8072b9ec075c6884f0ea92b6447357642b69f3dba1b7b8502a7d09e.
The retained hex is the complete 434-byte text section. Constant entry is 0;
fragment entry is 64. Four adjacent register-LOD SMPs at 0xfc, 0x10a,
0x118 and 0x126 use coordinate banks 0, 8, 16 and 24. Their full bytes
are asserted by `test_parallel_register_lod` in the portable texture test.

The opt-in AGX_NATIVE_PARALLEL_LOD experiment selects these four banks only
for ordinary AGX3B fragment, plain 2D register-LOD samples. The existing
scoreboard reserves all eight halves until result completion, including
wraparound and overlapping texture operations. USC staging allocation
includes each selected bank. Vulkan and Gallium caches distinguish the flag.
Preambles, internal shaders, other stages and more complex texture shapes
retain the ordinary path. The option defaults to false.

Compiler and encoder checks qualify the encoding and dependency accounting;
hardware image/timing qualification is recorded separately before promotion.
Raw compiler manifests and archives remain under
build/m3-texture-bank-study-20261003/native-varying-{g15s,g16g}.

Initial M3 qualification, 2026-10-03: all 321 compiler tests and 6,448
portable texture checks pass; 32 retained shader inputs compile both ways,
three binaries change. Both three-frame Minecraft checks match the approved
reference exactly. Nine same-boot alternating pairs give 164.2933 versus
172.2898 replay FPS (+4.867%), with the final drain included. This is not
live Java FPS and the option remains disabled by default pending repeat and
broader qualification.

The first broad segment has 59 Pass, 12 NotSupported and three Fail.
All three array textureQueryLod failures reproduce with the option false and
with the preceding qualified Mesa DSO. They remain failures in the record,
not skips or new optimization regressions; broader coverage is unfinished.
Receipts: gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/parallel-lod.
