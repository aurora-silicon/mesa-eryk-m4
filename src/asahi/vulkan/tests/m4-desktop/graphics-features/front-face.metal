#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]){return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}
fragment float4 fs(bool front [[front_facing]]) {return float4(front?1.0:0.0,0,0,1);}
