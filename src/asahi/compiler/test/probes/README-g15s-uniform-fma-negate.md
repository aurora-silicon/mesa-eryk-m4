# G15S uniform FMA with GPR negates

Own CPU-only Apple offline compilation on macOS 26.6.2 (25G83),
AIR-NT 32023.921, target `applegpu_g15s`, strict `-fno-fast-math`.
No native GPU execution is claimed. Source and complete 302-byte fragment
text are retained beside this file; the constant program occupies [0,64),
main occupies [64,302). Every main instruction round-trips through the
AGX3b composer with no unknown span.

Native instructions at 198 and 206 are `19032e0511800202` (uniform
multiplier, negated GPR addend) and `09012e8521060208` (negated GPR
multiplier, uniform addend). These are the two complete byte oracles in
`uniform_fma_negate_vectors`. Apple pre-negates the constant inputs in its
constant program. The modifier/wait-domain combinations are encoder tests,
not additional complete Apple byte witnesses for negated uniforms.

An ordinary-fragment compiler-policy experiment removed three copies from
the retained Minecraft terrain shader (236 to 233 IR instructions), keeping
71 register halves and zero scratch. Three saved images are RGB identical
in both modes. Nine guarded replay pairs give 180.3981 versus 180.5775 FPS,
median paired change +0.1095 percent. No useful speedup was demonstrated;
the compiler option, packer integration and cache bit were removed. Only
this independently witnessed encoder extension remains. Full patch and
receipts are retained in the benchmark study `fma-uniform-negate-rejected`.
No full CTS or live-application qualification is claimed.
