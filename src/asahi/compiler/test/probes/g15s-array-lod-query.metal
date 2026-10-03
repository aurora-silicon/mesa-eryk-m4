#include <metal_stdlib>
using namespace metal;
struct Varying { float4 position [[position]]; float2 uv; };
vertex Varying vs(uint vertex_id [[vertex_id]]) {
 float2 p=float2((vertex_id<<1)&2,vertex_id&2);
 return {float4(p*2-1,0,1),p};
}
fragment float4 fs(Varying in [[stage_in]], texture2d_array<float> image [[texture(0)]], sampler s [[sampler(0)]]) {
 return float4(image.calculate_clamped_lod(s,in.uv),image.calculate_unclamped_lod(s,in.uv),0,1);
}
