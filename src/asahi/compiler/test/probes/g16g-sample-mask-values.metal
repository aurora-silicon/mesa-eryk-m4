#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
struct OutM { float4 c [[color(0)]]; uint m [[sample_mask]]; };
fragment OutM m0(V v [[stage_in]]) { OutM o; o.m = 0u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
fragment OutM m1(V v [[stage_in]]) { OutM o; o.m = 1u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
fragment OutM m2(V v [[stage_in]]) { OutM o; o.m = 2u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
fragment OutM m3(V v [[stage_in]]) { OutM o; o.m = 3u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
fragment OutM m5(V v [[stage_in]]) { OutM o; o.m = 5u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
fragment OutM mf(V v [[stage_in]]) { OutM o; o.m = 0xfu; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
fragment OutM mdyn(V v [[stage_in]], constant uint &mask [[buffer(2)]]) { OutM o; o.m = mask; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
[[early_fragment_tests]] fragment OutM em2(V v [[stage_in]]) { OutM o; o.m = 2u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
[[early_fragment_tests]] fragment OutM edyn(V v [[stage_in]], constant uint &mask [[buffer(2)]]) { OutM o; o.m = mask; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
