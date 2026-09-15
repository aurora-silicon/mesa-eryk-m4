#version 450
#extension GL_EXT_demote_to_helper_invocation : require
layout(location = 0) in mediump vec4 a_one;
layout(location = 1) in mediump vec4 v_coords;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std430) buffer Output { uint words[]; } b;
void main(){
  uint x = uint(gl_FragCoord.x), y = uint(gl_FragCoord.y);
  uint id = x + 32u*y;
  highp vec2 dx0 = dFdx(a_one.xy + gl_FragCoord.xy);
  highp vec2 dy0 = dFdy(a_one.xy + gl_FragCoord.xy);
  b.words[id] = floatBitsToUint(dx0.y);
  b.words[1024u+id] = floatBitsToUint(dy0.x);
  b.words[2048u+id] = floatBitsToUint(a_one.y);
  ivec2 f = ivec2(gl_FragCoord.xy);
  int lsb = (f.x | f.y)&1;
  if (lsb != 0) demote;
  bool isHelper = helperInvocationEXT();
  highp vec2 dx = dFdx(a_one.xy + gl_FragCoord.xy);
  highp vec2 dy = dFdy(a_one.xy + gl_FragCoord.xy);
  highp float dh = dFdx(float(isHelper));
  uint w = (abs(dx.x-1.0) < 0.01 ? 1u:0u) | (dx.y == 0.0 ? 2u:0u) | (dy.x == 0.0 ? 4u:0u) | (abs(dy.y-1.0) < 0.01 ? 8u:0u) | (abs(dh-1.0) < 0.1 ? 16u:0u) | (!isHelper ? 32u:0u);
  if (!isHelper) b.words[3072u+id] = 0x400u | w;
  color = vec4(0,0,0,1);
}
