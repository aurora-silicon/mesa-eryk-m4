#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]) { return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); }
fragment float4 fs() {
 return float4(simd_is_helper_thread()?1.0:0.0,0,0,1);
}
