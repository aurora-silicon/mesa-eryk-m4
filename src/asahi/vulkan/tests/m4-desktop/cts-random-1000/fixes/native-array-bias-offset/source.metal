#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; float layer; float lod; };
vertex V vs(uint i [[vertex_id]]) { return {float4(i==1?3.0:-1.0,i==2?3.0:-1.0,0,1),float2(i&1,i>>1),float(i),float(i)}; }
fragment float4 fs(V v [[stage_in]], texture2d_array<float> tex [[texture(0)]], sampler s [[sampler(0)]]) { return tex.sample(s,v.uv,uint(v.layer),bias(v.lod),int2(-1,2)); }
