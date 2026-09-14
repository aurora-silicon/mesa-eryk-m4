#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; float ref; float lod; float layer; };
vertex V vs(uint i [[vertex_id]],constant float4 *p [[buffer(0)]]) {
 return {float4(p[i].xy,0,1),p[i].xy,p[i].z,p[i].w,p[i+1].x};
}
fragment float4 fs(V v [[stage_in]], depth2d_array<float> t [[texture(0)]], sampler s [[sampler(0)]]) {
 float a=t.sample_compare(s,v.uv,uint(v.layer),v.ref,level(v.lod));
 return float4(a,a,a,1);
}
