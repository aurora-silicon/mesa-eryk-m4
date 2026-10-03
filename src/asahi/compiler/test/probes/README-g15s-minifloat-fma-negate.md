# Native G15S minifloat FMA with negated GPR addend

Own CPU-only Apple offline compilation on macOS 26.6.2 (25G83), AIR-NT
32023.921, `applegpu_g15s`, strict `-fno-fast-math`. No native GPU
execution claim. Full 240-byte fragment text and source are retained.
Constant program [0,64); main [64,240), all main instructions round-trip.

Three complete native bytes at offsets 156/180/188 are
`098526c111860202`, `190b2ec191020202`, `29072ea191040202`.
They combine a minifloat multiplier, a negated GPR addend, independent
source lifetime hints and a cached destination. They carry no wait on
slot 5. The older helper accidentally forced f102=1 for modified FMA,
although that field belongs to the caller's requested dependency mask.
The old four witnesses carry an actual slot-5 dependency; their test
requests now include it explicitly instead of relying on a template bit.

The ordinary-fragment policy `AGX_NATIVE_MINIFLOAT_FMA_NEGATE` (default
false) preserves only this qualified full-F32 scalar source shape during
legalization. Abs, modified multiplier, uniform, half, vector, preamble,
internal and secondary variants keep the existing path. It preserves fused
rounding, all actual dependency waits, lifetime hints and source order.

The selected Minecraft fragment goes from 236 to 227 compiler IR instructions,
removing eight XORs and their sign-mask load. Main text shrinks 1934 to 1862
bytes, linked body 2334 to 2206; 71 register halves and zero scratch are
unchanged. All 332 compiler tests and 71 portable ALU checks pass, including
three new complete native G15S vectors. Three of 32 retained fragment
binaries change with identical optimized NIR. Both modes match three RGB
reference frames exactly. Two nine-pair same-boot measurements have median
paired gains +0.8463/+1.1045 percent, combined +1.0504 percent (16/18 faster),
separate combined medians 180.1560/182.0923 replay FPS. These are 30 completed
3024x1837 gameplay replay frames with final drain retained, not Java FPS.
The pinned 3675-case texture/arithmetic/FMA qualification is still running.
No full Vulkan CTS, M4 hardware or normal desktop promotion is claimed.
