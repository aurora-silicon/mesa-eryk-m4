#include <metal_stdlib>
using namespace metal;
struct VSOut { float4 position [[position]]; float2 uv; };
vertex VSOut vs(uint id [[vertex_id]]) {
 float2 p=float2((id<<1)&2,id&2);
 return {float4(p*2.0f-1.0f,0.0f,1.0f),p};
}
fragment float4 fs(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
 uint2 p=uint2(in.position.xy);
 float4 a=tex.read(p);
 float4 b=tex.read(p+uint2(1,2));
 float4 c=tex.read(p+uint2(3,4));
 float4 d=tex.read(p+uint2(5,6));
 return (a+b+c+d)*0.25f;
}
