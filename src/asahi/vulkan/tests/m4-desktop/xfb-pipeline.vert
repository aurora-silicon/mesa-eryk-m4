#version 450
layout(location = 0) in vec2 position;
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16)
out vec4 captured_position;
void main()
{
   gl_Position = vec4(position, 0.0, 1.0);
   captured_position = gl_Position;
}
