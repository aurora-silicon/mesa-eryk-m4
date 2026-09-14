#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float3 coord; float bias; };
vertex V vs(uint i [[vertex_id]]) { float2 p=float2((i<<1)&2,i&2); return {float4(p*2-1,0,1),float3(p,.3f),1.f}; }
fragment float4 fs(V v [[stage_in]], texture3d<float> t [[texture(0)]], sampler s [[sampler(0)]]) { return t.sample(s,v.coord,bias(v.bias),int3(-8,7,3)); }
