#include <metal_stdlib>
using namespace metal;
kernel void cs(device ushort *out [[buffer(0)]], device ushort2 *in [[buffer(1)]], uint i [[thread_position_in_grid]]) {
   ushort2 x = in[i];
   out[i] = x.x - x.y;
}
