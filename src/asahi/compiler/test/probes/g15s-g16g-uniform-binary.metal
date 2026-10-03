#include <metal_stdlib>
using namespace metal;
struct Out { float4 position [[position]]; float2 uv; };
vertex Out vs(uint id [[vertex_id]]) {
 float2 p=float2((id<<1)&2,id&2);
 return {float4(p*2.0f-1.0f,0.0f,1.0f),p};
}
fragment float4 fs(Out in [[stage_in]], constant float4 &data [[buffer(0)]]) {
 return float4(in.uv.x * data.x, in.uv.y + data.y,
               in.uv.x - data.z, in.uv.y * data.w);
}
