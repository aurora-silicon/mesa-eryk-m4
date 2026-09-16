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

fragment float4 fsarraygrad(V v [[stage_in]], texture2d_array<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, uint(v.pos.x), gradient2d(float2(v.c.z,0),float2(0,v.c.z)), min_lod_clamp(v.c.w));
}
fragment float fsshadow(V v [[stage_in]], depth2d<float> tex [[texture(0)]]) {
 return tex.sample_compare(s, v.c.xy, v.c.z, min_lod_clamp(v.c.w));
}
fragment float4 fscube(V v [[stage_in]], texturecube<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, min_lod_clamp(v.c.w));
}

fragment float4 fscubearray(V v [[stage_in]], texturecube_array<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, uint(v.pos.x), bias(v.pos.y), min_lod_clamp(v.c.w));
}
fragment float4 fs3d(V v [[stage_in]], texture3d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, bias(v.pos.y), min_lod_clamp(v.c.w));
}
fragment float4 fsoffset(V v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, bias(v.c.z), min_lod_clamp(v.c.w), int2(1,-1));
}

fragment float4 fscubebias(V v [[stage_in]], texturecube<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, bias(v.pos.y), min_lod_clamp(v.c.w));
}
fragment float4 fsgradoffset(V v [[stage_in]], texture3d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xyz, gradient3d(float3(v.c.w,0,0),float3(0,v.c.w,0)), min_lod_clamp(v.pos.x), int3(1,-1,1));
}
