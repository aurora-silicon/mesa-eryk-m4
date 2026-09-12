#include <metal_stdlib>
using namespace metal;
kernel void cs(device const half2 *input [[buffer(0)]], device half *output [[buffer(1)]], uint i [[thread_position_in_grid]]) {
    half2 v = input[i]; output[i] = v.x - v.y;
}
