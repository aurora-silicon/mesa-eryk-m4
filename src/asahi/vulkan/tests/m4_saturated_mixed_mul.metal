#include <metal_stdlib>
using namespace metal;
kernel void cs(device const float *src [[buffer(0)]], device const half *alpha [[buffer(1)]], device float *dst [[buffer(2)]], uint i [[thread_position_in_grid]]) {
 dst[i] = saturate(src[i] * float(alpha[i]));
}
