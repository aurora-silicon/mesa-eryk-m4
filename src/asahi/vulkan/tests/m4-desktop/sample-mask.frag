#version 450
// Four-sample regression: retain samples 0 and 2. Over opaque black,
// full red must resolve to RGBA8 (128, 0, 0, 255).
layout(location = 0) out vec4 color;
void main()
{
   gl_SampleMask[0] = 5;
   color = vec4(1, 0, 0, 1);
}
