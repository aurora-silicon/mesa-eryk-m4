#version 450
#extension GL_EXT_demote_to_helper_invocation : require
#extension GL_KHR_shader_subgroup_quad : require
#extension GL_KHR_shader_subgroup_basic : require
layout(location=0) out vec4 color;
layout(set=0,binding=0,std430) buffer Output { uint words[]; } b;
void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    int lc = coord.y * 2 + coord.x;
    float alpha_value = float(lc + 1) + ((lc == 1 || lc == 2) ? 0.25 : 0.75);
    bool h = helperInvocationEXT();
    float v = h ? 8.0 : roundEven(alpha_value);
    float q0 = subgroupQuadBroadcast(v, 0u), q1 = subgroupQuadBroadcast(v, 1u), q2 = subgroupQuadBroadcast(v, 2u), q3 = subgroupQuadBroadcast(v, 3u);
    b.words[lc] = h ? 1u : 0u;
    b.words[4 + lc] = floatBitsToUint(v);
    b.words[8 + lc] = floatBitsToUint(q0); b.words[12 + lc] = floatBitsToUint(q1);
    b.words[16 + lc] = floatBitsToUint(q2); b.words[20 + lc] = floatBitsToUint(q3);
    b.words[24 + lc] = gl_SubgroupInvocationID;
    b.words[28 + lc] = gl_SubgroupSize;
    b.words[32 + lc] = uint(gl_FragCoord.x * 2.0) | (uint(gl_FragCoord.y * 2.0) << 8);
    if (fract(alpha_value) < 0.5) demote;
    float w = helperInvocationEXT() ? 8.0 : roundEven(alpha_value);
    float m1 = subgroupQuadBroadcast(w, 0u) + 10.0*subgroupQuadBroadcast(w, 1u) + 100.0*subgroupQuadBroadcast(w, 2u) + 1000.0*subgroupQuadBroadcast(w, 3u);
    b.words[36 + lc] = floatBitsToUint(m1);
    color = vec4(1.0, 0.0, 0.0, 1.0);
}
