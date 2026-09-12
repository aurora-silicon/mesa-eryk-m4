#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]){return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}
fragment float4 fs(float4 pos [[position]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {return t.sample(s,pos.xy/16.0,level(0),int2(-1,2));}
