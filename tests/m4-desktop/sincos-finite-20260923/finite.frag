#version 450
// SPDX-License-Identifier: MIT
layout(location=0) out vec4 color;
void main() {
    float x = gl_FragCoord.x * 0.125 - 2.0;
    float s = sin(x), c = cos(x);
    bool ok = !isnan(s) && !isnan(c) && abs(s*s + c*c - 1.0) < 0.0001;
    color = ok ? vec4(0,1,0,1) : vec4(1,0,0,1);
}
