#include <metal_stdlib>
using namespace metal;
kernel void cs(texture2d<uint, access::read> t [[texture(0)]], device uint4 *out [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {
    sparse_color<uint4> v = t.sparse_read(p);
    out[p.y * 128 + p.x] = uint4(v.resident());
}
