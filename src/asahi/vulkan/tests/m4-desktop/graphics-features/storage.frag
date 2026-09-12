#version 450
layout(location=0) out vec4 color;
layout(set=0,binding=0,std430) buffer Output { uint words[]; } b;
void main(){
 uint id=uint(gl_FragCoord.x)+32u*uint(gl_FragCoord.y);
 b.words[id]=0x12340000u+id;
 uint old=atomicAdd(b.words[1024+id],17u+id);
 b.words[2048+id]=old+0x70u;
 atomicAdd(b.words[3072],1u);
 color=vec4(1,0,0,1);
}
