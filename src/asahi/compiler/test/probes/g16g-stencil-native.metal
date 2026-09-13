#include <metal_stdlib>
using namespace metal;
struct V {float4 pos [[position]]; float2 uv;};
vertex V vs(uint i [[vertex_id]]) {float2 p=float2((i<<1)&2,i&2);return {float4(p*2-1,0,1),p};}
struct R {float4 color [[color(0)]];uint stencil [[stencil]];};
fragment R fs(V v [[stage_in]]) {return {float4(1,0,0,1),uint(v.uv.x*127)};}
