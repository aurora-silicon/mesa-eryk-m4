#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]) { return float4(float2((id<<1)&2,id&2)*2-1,0,1); }
fragment float3 fs(float3 previous [[color(0)]]) { return previous + float3(.25,.5,.75); }
