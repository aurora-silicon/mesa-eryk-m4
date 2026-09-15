#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
fragment float4 fshelper(V v [[stage_in]]) { return float4(simd_is_helper_thread() ? 1.0 : 0.0, 0.0, 0.0, 1.0); }
fragment float4 fsdfdx(V v [[stage_in]]) { return float4(dfdx(v.c.x), dfdy(v.c.y), 0.0, 1.0); }
fragment float4 fsdemotehelper(V v [[stage_in]]) { uint2 lsb = uint2(v.pos.xy) & 1u; if (lsb.x != 0u || lsb.y != 0u) discard_fragment(); float h = simd_is_helper_thread() ? 1.0 : 0.0; return float4(dfdx(h), h, 0.0, 1.0); }
fragment float4 fsquad(V v [[stage_in]]) { float x = quad_broadcast(v.c.x, 1); return float4(x, quad_broadcast(v.c.y, 3), 0.0, 1.0); }
fragment float4 fsplain(V v [[stage_in]]) { return v.c; }
