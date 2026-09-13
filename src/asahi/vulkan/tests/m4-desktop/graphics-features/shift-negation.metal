#include <metal_stdlib>
using namespace metal;
kernel void main0(device const uint2 *in [[buffer(0)]], device uint4 *out [[buffer(1)]], uint i [[thread_position_in_grid]]) { uint2 v=in[i];out[i]=uint4(v.x>>v.y,0u-v.y,7u-v.x,v.x<<v.y); }
