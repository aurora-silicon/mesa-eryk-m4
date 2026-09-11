#include <metal_stdlib>
using namespace metal;
kernel void cs(device const float2 *src [[buffer(0)]], device float *dst [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 dst[i] = saturate(fma(-8.0f, src[i].x, src[i].y));
}
