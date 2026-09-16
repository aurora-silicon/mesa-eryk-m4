/* SPDX-License-Identifier: MIT
 * Independent VS/TES layer and viewport routing with fragment builtin checks.
 * Run only under gpu-experiment.py's watchdog and thermal guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError()); exit(1); } } while(0)
static GLuint shader(GLenum stage, const char *text) {
 GLuint s=glCreateShader(stage);glShaderSource(s,1,&text,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return s;
}
int main(int argc,char **argv) {
 CHECK(argc==3);
 const int tess=!strcmp(argv[1],"tess"),layer=strcmp(argv[2],"viewport")!=0,viewport=strcmp(argv[2],"layer")!=0;
 CHECK(tess||!strcmp(argv[1],"vertex"));CHECK(!strcmp(argv[2],"layer")||!strcmp(argv[2],"viewport")||!strcmp(argv[2],"both"));
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 const unsigned width=viewport?16:8,layers=layer?4:1;
 GLuint tex,fb,vao;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D_ARRAY,tex);
 glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA32F,width,width,layers,0,GL_RGBA,GL_FLOAT,NULL);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex,0);
 CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 const char *header="#version 450 core\n#extension GL_ARB_shader_viewport_layer_array : require\n";
 char vs[2048],tes[2048];GLuint p=glCreateProgram();
 if(tess) {
  snprintf(vs,sizeof(vs),"%sflat out int which;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);which=gl_InstanceID;}",header);
  glAttachShader(p,shader(GL_TESS_CONTROL_SHADER,"#version 450 core\nlayout(vertices=3)out;flat in int which[];patch out int target;void main(){gl_out[gl_InvocationID].gl_Position=gl_in[gl_InvocationID].gl_Position;if(gl_InvocationID==0){target=which[0];gl_TessLevelOuter[0]=1.;gl_TessLevelOuter[1]=1.;gl_TessLevelOuter[2]=1.;gl_TessLevelInner[0]=1.;}}"));
  snprintf(tes,sizeof(tes),"%slayout(triangles,equal_spacing,cw)in;patch in int target;void main(){gl_Position=gl_TessCoord.x*gl_in[0].gl_Position+gl_TessCoord.y*gl_in[1].gl_Position+gl_TessCoord.z*gl_in[2].gl_Position;gl_Layer=%s;gl_ViewportIndex=%s;}",header,layer?"target":"0",viewport?"target":"0");
  glAttachShader(p,shader(GL_TESS_EVALUATION_SHADER,tes));
 } else snprintf(vs,sizeof(vs),"%svoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);gl_Layer=%s;gl_ViewportIndex=%s;}",header,layer?"gl_InstanceID":"0",viewport?"gl_InstanceID":"0");
 glAttachShader(p,shader(GL_VERTEX_SHADER,vs));
 glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 450 core\nlayout(location=0)out vec4 color;void main(){color=vec4(float(gl_Layer+1)*.25,float(gl_ViewportIndex+1)*.25,.75,1);}"));
 glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[8192];glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);
 glUseProgram(p);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 const GLfloat viewports[]={0,0,8,8,8,0,8,8,0,8,8,8,8,8,8,8};
 glViewportArrayv(0,viewport?4:1,viewports);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
 if(tess)glPatchParameteri(GL_PATCH_VERTICES,3);
 glDrawArraysInstanced(tess?GL_PATCHES:GL_TRIANGLES,0,3,4);
 float actual[4*16*16*4];glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_RGBA,GL_FLOAT,actual);CHECK(glGetError()==GL_NO_ERROR);
 unsigned bad=0;
 for(unsigned l=0;l<layers;l++)for(unsigned y=0;y<width;y++)for(unsigned x=0;x<width;x++)for(unsigned c=0;c<4;c++) {
  unsigned v=viewport?(x/8+2*(y/8)):0;int covered=!(layer&&viewport)||l==v;
  float expected=covered?(c==0?(l+1)*.25f:c==1?(v+1)*.25f:c==2?.75f:1.f):0.f;
  unsigned i=((l*width+y)*width+x)*4+c;
  if(actual[i]!=expected){if(bad<8)fprintf(stderr,"layer%u (%u,%u) c%u=%g expected%g\n",l,x,y,c,actual[i],expected);bad++;}
 }
 printf("%s: %s %s exact %u pixels, bad=%u\n",bad?"FAIL":"PASS",argv[1],argv[2],layers*width*width,bad);
 glDeleteProgram(p);glDeleteTextures(1,&tex);glDeleteFramebuffers(1,&fb);glDeleteVertexArrays(1,&vao);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return !!bad;
}
