#include <metal_stdlib>
using namespace metal;
struct VSOut { float4 position [[position]]; float2 uv; };
vertex VSOut vs(uint id [[vertex_id]]) {
 float2 p=float2((id<<1)&2,id&2);
 return {float4(p*2.0f-1.0f,0.0f,1.0f),p};
}
fragment float4 fs(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]], constant float2 &levels [[buffer(0)]]) {
 float4 a=tex.sample(s,in.uv,level(in.uv.x*3.0f));
 float4 b=tex.sample(s,in.uv+float2(0.01f,0.02f),level(in.uv.y*4.0f));
 float4 c=tex.sample(s,in.uv+float2(0.03f,0.04f),level(in.uv.x*3.0f));
 float4 d=tex.sample(s,in.uv+float2(0.05f,0.06f),level(in.uv.y*4.0f));
 return (a+b+c+d)*0.25f;
}
