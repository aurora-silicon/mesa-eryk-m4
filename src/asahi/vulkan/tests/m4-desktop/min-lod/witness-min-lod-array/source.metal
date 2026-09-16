#include <metal_stdlib>
using namespace metal;
constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear, mip_filter::linear);
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
fragment float4 fs(V v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, bias(v.c.z), min_lod_clamp(v.c.w));
}

fragment float4 fsmin(V v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, min_lod_clamp(v.c.w));
}
fragment float4 fsarray(V v [[stage_in]], texture2d_array<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, uint(v.pos.x), bias(v.c.z), min_lod_clamp(v.c.w));
}
fragment float4 fsgrad(V v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, gradient2d(float2(v.c.z,0),float2(0,v.c.z)), min_lod_clamp(v.c.w));
}
