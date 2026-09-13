#include <metal_stdlib>
using namespace metal;
kernel void cs(device const uint *src [[buffer(0)]], device uint *dst [[buffer(1)]], uint id [[thread_position_in_grid]]) {
 uint x = src[id];
 dst[id*4+0] = simd_broadcast(x, 7);
 dst[id*4+1] = simd_broadcast(x, 15);
 dst[id*4+2] = simd_broadcast(x, 23);
 dst[id*4+3] = simd_broadcast(x, 31);
}
