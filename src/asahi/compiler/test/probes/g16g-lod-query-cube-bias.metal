#include <metal_stdlib>
using namespace metal;
constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear, mip_filter::linear);
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
fragment float4 fscube(V v [[stage_in]], texturecube<float> tex [[texture(0)]]) {
 return float4(tex.calculate_unclamped_lod(s, v.c.xyz), tex.calculate_clamped_lod(s, v.c.xyz), 0, 1);
}
fragment float4 fscubearr(V v [[stage_in]], texturecube_array<float> tex [[texture(0)]]) {
 return float4(tex.calculate_unclamped_lod(s, v.c.xyz), tex.calculate_clamped_lod(s, v.c.xyz), 0, 1);
}
fragment float4 fs3d(V v [[stage_in]], texture3d<float> tex [[texture(0)]]) {
 return float4(tex.calculate_unclamped_lod(s, v.c.xyz), tex.calculate_clamped_lod(s, v.c.xyz), 0, 1);
}
fragment float4 fscubebias(V v [[stage_in]], texturecube<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, bias(v.c.w));
}
fragment float4 fscubearrbias(V v [[stage_in]], texturecube_array<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, (uint)v.pos.x, bias(v.c.w));
}
