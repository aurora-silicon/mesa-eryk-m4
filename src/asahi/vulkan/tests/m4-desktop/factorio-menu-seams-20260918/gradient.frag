#version 450
layout(location=0) in vec4 tint;
layout(location=1) in vec2 uv;
layout(location=0) out vec4 color;
void main(){color=vec4(clamp(abs(dFdx(uv.x))*8192.*8.,0.,1.),clamp(abs(dFdy(uv.y))*3888.*8.,0.,1.),0,1)*tint;}
