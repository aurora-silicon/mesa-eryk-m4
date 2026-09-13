#include <metal_stdlib>
using namespace metal;
struct Resources { texture2d_array<float, access::write> dst [[id(0)]]; };
kernel void cs(constant Resources &r [[buffer(0)]],
               device const float4 *input [[buffer(1)]],
               uint3 p [[thread_position_in_grid]]) {
    r.dst.write(input[(p.z * r.dst.get_height() + p.y) * r.dst.get_width() + p.x], p.xy, p.z);
}
