// SPDX-License-Identifier: MIT
// Pair with g16-frontface-derivative.frag at 63x63. Draw 3 vertices and
// 98 instances with culling disabled. Adjacent triangles have opposite facing;
// every covered fragment must still retain its own primitive's helper values.
#version 450
void main()
{
    int cell = gl_InstanceIndex / 2;
    int tri = gl_InstanceIndex % 2;
    vec2 first[3] = vec2[3](vec2(0, 0), vec2(1, 0), vec2(0, 1));
    vec2 second[3] = vec2[3](vec2(1, 1), vec2(1, 0), vec2(0, 1));
    vec2 corner = tri == 0 ? first[gl_VertexIndex] : second[gl_VertexIndex];
    vec2 pos = (vec2(cell % 7, cell / 7) + corner) * (2.0 / 7.0) - 1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
