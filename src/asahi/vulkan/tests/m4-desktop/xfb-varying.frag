#version 450
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 pixel;
void main()
{
   // Exact black/red halves require a nonconstant interpolated value. They
   // catch a missing or constant varying as well as wrong slot assignment.
   pixel = vec4(step(0.0, color.x), 0.0, 0.0, 1.0);
}
