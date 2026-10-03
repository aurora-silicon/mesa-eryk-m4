#include <metal_stdlib>
using namespace metal;
struct VSOut {float4 position [[position]];float2 uv;};
vertex VSOut vs(uint id [[vertex_id]]) {float2 p=float2((id<<1)&2,id&2);return {float4(p*2.0f-1.0f,0.0f,1.0f),p};}
fragment float4 fs(VSOut in [[stage_in]],texture2d<float> tex [[texture(0)]],sampler s [[sampler(0)]]) {
 float4 v0=tex.sample(s,in.uv+float2(0.00f,0.00f),level(in.uv.x*3.0f));
 float4 v1=tex.sample(s,in.uv+float2(0.03f,0.02f),level(in.uv.y*4.0f));
 float4 v2=tex.sample(s,in.uv+float2(0.06f,0.04f),level(in.uv.x*3.0f));
 float4 v3=tex.sample(s,in.uv+float2(0.09f,0.06f),level(in.uv.y*4.0f));
 float4 v4=tex.sample(s,in.uv+float2(0.12f,0.08f),level(in.uv.x*3.0f));
 float4 v5=tex.sample(s,in.uv+float2(0.15f,0.10f),level(in.uv.y*4.0f));
 float4 v6=tex.sample(s,in.uv+float2(0.18f,0.12f),level(in.uv.x*3.0f));
 float4 v7=tex.sample(s,in.uv+float2(0.21f,0.14f),level(in.uv.y*4.0f));
 return (v0+v1+v2+v3+v4+v5+v6+v7)*0.125f;
}
