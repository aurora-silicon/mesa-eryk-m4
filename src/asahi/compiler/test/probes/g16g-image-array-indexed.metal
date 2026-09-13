#include <metal_stdlib>
using namespace metal;
struct Resources { array<texture2d_array<float,access::write>,4> dst [[id(0)]]; };
kernel void cs(constant Resources &r [[buffer(0)]], device float4 *data [[buffer(1)]], uint3 id [[thread_position_in_grid]]) { r.dst[id.x & 3].write(data[id.x], id.xy, id.z); }
