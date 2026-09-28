// SPDX-License-Identifier: MIT
// Render at 31x31. Every pixel must be opaque green, including the last row/column.
#version 450
layout(location=0) out vec4 color;
void main() {
    float x = gl_FragCoord.x;
    float y = gl_FragCoord.y;
    float face = gl_FrontFacing ? 1.0 : -1.0;
    float value = (x * y) * face;
    bool ok = dFdxFine(value) == y * face && dFdyFine(value) == x * face;
    color = ok ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
}
