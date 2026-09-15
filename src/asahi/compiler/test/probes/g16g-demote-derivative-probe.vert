#version 450
layout(location = 0) in vec2 position;
layout(location = 0) out mediump vec4 v_one;
layout(location = 1) out mediump vec4 v_coords;
void main() {
   gl_Position = vec4(position, 0.0, 1.0);
   v_one = vec4(1.0);
   v_coords = vec4(position, 0.0, 1.0);
}
