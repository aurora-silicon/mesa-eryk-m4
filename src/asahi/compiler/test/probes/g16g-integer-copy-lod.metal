#include <metal_stdlib>
using namespace metal;
kernel void cs(texture2d<int, access::read> src [[texture(0)]],
               texture2d<int, access::write> dst [[texture(1)]],
               constant uint &level [[buffer(0)]],
               uint2 xy [[thread_position_in_grid]]) {
   dst.write(src.read(xy, level), xy);
}
