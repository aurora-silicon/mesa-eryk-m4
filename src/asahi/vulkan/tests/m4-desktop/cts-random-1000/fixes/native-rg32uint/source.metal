#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]) { return float4(float2((id<<1)&2,id&2)*2-1,0,1); }
fragment uint2 fs(float4 p [[position]]) { return uint2(p.xy) + uint2(0x12345678,0xaabbccdd); }
