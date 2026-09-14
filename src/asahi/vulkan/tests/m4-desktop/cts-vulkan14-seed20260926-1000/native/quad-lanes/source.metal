#include <metal_stdlib>
using namespace metal;
kernel void probe(device uint *in [[buffer(0)]], device uint4 *out [[buffer(1)]], uint i [[thread_position_in_grid]]) { uint x=in[i]; out[i]=uint4(quad_broadcast(x,0),quad_broadcast(x,1),quad_broadcast(x,2),quad_broadcast(x,3)); }
