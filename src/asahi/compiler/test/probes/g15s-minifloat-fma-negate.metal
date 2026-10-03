#include <metal_stdlib>
using namespace metal;
struct Out { float4 position [[position]]; float4 uv; };
vertex Out vs(uint id [[vertex_id]]) {
   float2 p = float2((id << 1) & 2, id & 2);
   return {float4(p * 2.0f - 1.0f, 0.0f, 1.0f), float4(p, p.yx)};
}
fragment float4 fs(Out in [[stage_in]]) {
   return float4(fma(2.0f, in.uv.x, -in.uv.y),
                 fma(in.uv.z, 1.0f, -in.uv.w),
                 fma(2.0f, in.uv.w, -in.uv.z),
                 fma(0.5f, in.uv.y, -in.uv.x));
}
