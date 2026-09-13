#include <metal_stdlib>
using namespace metal;
kernel void cs(device const uint *src [[buffer(0)]], device uint *dst [[buffer(1)]], uint id [[thread_position_in_grid]]) {
 uint x = src[id];
 dst[id*3+0] = simd_prefix_exclusive_sum(x);
 dst[id*3+1] = simd_prefix_inclusive_sum(x);
 dst[id*3+2] = x;
}
