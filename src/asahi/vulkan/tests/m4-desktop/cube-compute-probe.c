/* SPDX-License-Identifier: MIT
 * Exact cube projection + explicit LOD in compute through Zink.
 * Execute hardware only through gpu-experiment.py watchdog/thermal guards.
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
int main(void) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_CUBE_MAP,tex);
 glTexStorage2D(GL_TEXTURE_CUBE_MAP,2,GL_RGBA32F,16,16);
 for(unsigned level=0;level<2;level++)for(unsigned face=0;face<6;face++){
  unsigned size=16>>level;float data[size*size*4];
  for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++)for(unsigned c=0;c<4;c++)data[(y*size+x)*4+c]=face*1024+level*512+y*16+x+c*.125f;
  glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,0,0,size,size,GL_RGBA,GL_FLOAT,data);
 }
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAX_LEVEL,1);
 float directions[96][4],actual[96][4];
 for(unsigned face=0;face<6;face++)for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){
  float s=(x+.5f)*.5f-1,t=(y+.5f)*.5f-1;
  const float v[6][4]={{1,-t,-s,0},{-1,-t,s,0},{s,1,t,0},{s,-1,-t,0},{s,-t,1,0},{-s,-t,-1,0}};
  memcpy(directions[face*16+y*4+x],v[face],sizeof(v[0]));
 }
 GLuint buffers[2];glGenBuffers(2,buffers);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(directions),directions,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[0]);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(actual),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[1]);
 unsigned bad=0;
 for(unsigned level=0;level<2;level++){
  char source[1024];snprintf(source,sizeof(source),"#version 430 core\nlayout(local_size_x=32) in; layout(std430,binding=0) readonly buffer D{vec4 dirs[];};layout(std430,binding=1) writeonly buffer O{vec4 result[];};layout(binding=0) uniform samplerCube tex; void main(){uint i=gl_GlobalInvocationID.x;result[i]=textureLod(tex,dirs[i].xyz,%u.0);}",level);
  GLuint shader=glCreateShader(GL_COMPUTE_SHADER);const char *text=source;glShaderSource(shader,1,&text,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[4096];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);
  GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
  glDispatchCompute(3,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(actual),actual);CHECK(glGetError()==GL_NO_ERROR);
  unsigned size=16>>level;
  for(unsigned face=0;face<6;face++)for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)for(unsigned c=0;c<4;c++){
   unsigned px=(2*x+1)*size/8,py=(2*y+1)*size/8,i=face*16+y*4+x;
   float expected=face*1024+level*512+py*16+px+c*.125f;
   if(actual[i][c]!=expected){if(bad<6)fprintf(stderr,"lod%u face%u (%u,%u) c%u=%g expected %g\n",level,face,x,y,c,actual[i][c],expected);bad++;}
  }
  glDeleteProgram(program);glDeleteShader(shader);
 }
 printf("%s: compute cube six faces/two LODs, 192 samples (%u mismatched words)\n",bad?"FAIL":"PASS",bad);
 glDeleteBuffers(2,buffers);glDeleteTextures(1,&tex);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return !!bad;
}
