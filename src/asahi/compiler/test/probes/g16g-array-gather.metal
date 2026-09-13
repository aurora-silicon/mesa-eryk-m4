#include <metal_stdlib>
using namespace metal;
struct Resources { texture2d_array<int> tex [[id(0)]]; sampler samp [[id(1)]]; };
vertex float4 vs(uint id [[vertex_id]]) {
   return float4(float2((id << 1) & 2, id & 2) * 2 - 1, 0, 1);
}
fragment int4 fs(float4 p [[position]], constant Resources *r [[buffer(0)]]) {
   uint i = uint(p.x) & 1;
   return r[i].tex.gather(r[i].samp, p.xy * 0.01f, uint(p.y), int2(-8, -8), component::z);
}
