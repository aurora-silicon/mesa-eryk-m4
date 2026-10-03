#include <metal_stdlib>
using namespace metal;

struct Out {
   float4 position [[position]];
   float2 uv;
};

vertex Out vs(uint id [[vertex_id]]) {
   float2 p = float2((id << 1) & 2, id & 2);
   return {float4(p * 2.0f - 1.0f, 0.0f, 1.0f), p};
}

struct Data { float4 value; uint iterations; };

fragment float4 fs(Out in [[stage_in]], constant Data &data [[buffer(0)]]) {
   float4 x = float4(in.uv, in.uv.yx);
   for (uint i = 0; i < data.iterations; ++i) {
      x = float4(fma(data.value.x, x.x, -x.y),
                 fma(-x.z, x.y, data.value.y),
                 fma(-data.value.z, x.z, x.w),
                 fma(x.x, x.w, -data.value.w));
   }
   return x;
}
