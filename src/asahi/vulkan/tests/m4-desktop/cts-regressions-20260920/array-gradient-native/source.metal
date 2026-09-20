#include <metal_stdlib>
using namespace metal;
kernel void cs(texture2d_array<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *out [[buffer(0)]], device const float4 *in [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 float4 a = in[i];
 out[i] = t.sample(s, a.xy, uint(a.z), gradient2d(float2(a.w,0),float2(0,a.w)), int2(a.xy));
}
