#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]) { return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); }
fragment float4 fs(float4 p [[position]],device atomic_uint *out [[buffer(0)]]) {
 uint v=atomic_fetch_add_explicit(out+uint(p.x),1,memory_order_relaxed);
 return float4(float(v),0,0,1);
}
