#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; float ref; float lod; float layer; float2 dx; float2 dy; };
vertex V vs(uint i [[vertex_id]],constant float4 *p [[buffer(0)]]) {return {float4(p[i].xy,0,1),p[i].xy,p[i].z,p[i].w,p[i+1].x,p[i+1].yz,p[i+2].xy};}
fragment float4 fs(V v [[stage_in]], depth2d_array<float> t [[texture(0)]],texture2d<float> color [[texture(1)]],sampler s [[sampler(0)]]) {
 int2 off=int2(v.uv*4);
 float a=t.sample_compare(s,v.uv,uint(v.layer),v.ref,level(v.lod),off);
 float4 b=color.sample(s,v.uv,gradient2d(v.dx,v.dy),off);
 return b+a;
}
