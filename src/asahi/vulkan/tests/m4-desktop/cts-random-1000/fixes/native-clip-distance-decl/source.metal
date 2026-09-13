#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float clip [[clip_distance]] [8]; };
vertex V vs(uint i [[vertex_id]], constant float *c [[buffer(0)]]) { V v; v.pos=float4(i==1?3.0:-1.0,i==2?3.0:-1.0,0,1); for(uint n=0;n<8;++n)v.clip[n]=c[n]+float(i); return v; }
fragment float4 fs() { return float4(1,0,0,1); }
