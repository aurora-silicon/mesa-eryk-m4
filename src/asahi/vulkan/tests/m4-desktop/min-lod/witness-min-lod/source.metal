#include <metal_stdlib>
using namespace metal;
constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear, mip_filter::linear);
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
fragment float4 fs(V v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
 return tex.sample(s, v.c.xy, bias(v.c.z), min_lod_clamp(v.c.w));
}
