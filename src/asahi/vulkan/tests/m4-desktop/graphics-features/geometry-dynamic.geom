#version 450
layout(triangles) in;
layout(triangle_strip,max_vertices=6) out;
layout(location=0) out vec4 shade;
void main(){
 int count=gl_in[0].gl_Position.x<0?2:1;
 for(int side=0;side<count;side++){
  for(int i=0;i<3;i++){
   vec4 p=gl_in[i].gl_Position;
   p.x=p.x*0.5+(side==0?-0.5:0.5);
   gl_Position=p;
   shade=side==0?vec4(1,0,0,1):vec4(0,1,0,1);
   EmitVertex();
  }
  EndPrimitive();
 }
}
