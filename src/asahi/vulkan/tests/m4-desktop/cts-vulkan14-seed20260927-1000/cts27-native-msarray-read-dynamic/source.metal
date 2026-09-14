#include <metal_stdlib>
using namespace metal;
kernel void probe(texture2d_ms_array<uint, access::read> t [[texture(0)]], device uint4 *out [[buffer(0)]], constant uint4 &p [[buffer(1)]], uint i [[thread_position_in_grid]]) { out[i] = t.read(p.xy, (p.z+i)%8, (p.w+i)%2); }
