#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; float ref; float lod; };
vertex V vs(uint i [[vertex_id]],constant float4 *p [[buffer(0)]]) {
 return {float4(p[i].xy,0,1),p[i].xy,p[i].z,p[i].w};
}
fragment float4 fs(V v [[stage_in]], depth2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {
 float a=t.sample_compare(s,v.uv,v.ref,level(v.lod));
 float b=t.sample_compare(s,v.uv,v.ref,bias(v.lod),int2(v.uv*4));
 return float4(a,b,a+b,1);
}
