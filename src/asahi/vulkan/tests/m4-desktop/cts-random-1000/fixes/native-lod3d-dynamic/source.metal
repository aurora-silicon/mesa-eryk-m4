#include <metal_stdlib>
using namespace metal;
struct V {float4 p [[position]]; float3 uv;};
vertex V vs(uint id [[vertex_id]]) {return {float4(float2((id<<1)&2,id&2)*2-1,0,1),float3(float(id),.5,.25)};}
fragment float4 fs(V v [[stage_in]], texture3d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {return t.sample(s,v.uv,level(v.uv.x));}
