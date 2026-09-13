#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; };
vertex V vs(uint i [[vertex_id]]) { return {float4(i==1?3.0:-1.0,i==2?3.0:-1.0,0,1),float2(i&1,i>>1)}; }
fragment float4 fs(V v [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) {
return float4(tex.calculate_clamped_lod(s,v.uv),tex.calculate_unclamped_lod(s,v.uv),0,1);
}
