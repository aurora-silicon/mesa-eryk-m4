#version 450
layout(location=0) in vec2 position;
layout(set=0,binding=0,std430) buffer Output { uint words[]; } b;
void main() {
 uint id=uint(gl_VertexIndex);
 b.words[id]=0x12340000u+id;
 uint old=atomicAdd(b.words[3+id],17u+id);
 b.words[6+id]=old+0x70u;
 atomicAdd(b.words[9],1u);
 gl_Position=vec4(position,0,1);
}
