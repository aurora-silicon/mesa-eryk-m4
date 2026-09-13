/* SPDX-License-Identifier: MIT
 * Compute textureGather: float color, D32, D32S8 and clamp-to-edge.
 * All hardware runs require gpu-experiment.py watchdog/thermal guards.
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
static int clamp(int x,int n){return x<0?0:x>=n?n-1:x;}
int main(void){
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 const char *src="#version 430 core\nlayout(local_size_x=8,local_size_y=8) in;layout(std430,binding=0) writeonly buffer Out{vec4 values[];};layout(binding=0) uniform sampler2D tex;void main(){uvec2 p=gl_GlobalInvocationID.xy;vec2 uv=(vec2(p)*2.-1.+.5)/vec2(12,10);values[p.y*8u+p.x]=textureGather(tex,uv,0);}";
 int stores=getenv("M4_TEST_GATHER_STORES")!=NULL;
 if(stores)src="#version 430 core\nlayout(local_size_x=8,local_size_y=8) in;layout(r32f,binding=0) uniform writeonly image2D dst;layout(binding=0) uniform sampler2D tex;void main(){uvec2 p=gl_GlobalInvocationID.xy;vec2 uv=(vec2(p)*2.-1.+.5)/vec2(12,10);vec4 v=textureGather(tex,uv,0);ivec2 q=ivec2(p)*2;imageStore(dst,q+ivec2(0,1),vec4(v.x));imageStore(dst,q+ivec2(1,1),vec4(v.y));imageStore(dst,q+ivec2(1,0),vec4(v.z));imageStore(dst,q,vec4(v.w));}";
 char queried[2048];
 if(getenv("M4_TEST_QUERY_SIZE")){
  const char *pos=strstr(src,"vec2(12,10)");CHECK(pos);size_t prefix=pos-src;CHECK(prefix<sizeof(queried));
  memcpy(queried,src,prefix);snprintf(queried+prefix,sizeof(queried)-prefix,"vec2(textureSize(tex,0))%s",pos+strlen("vec2(12,10)"));src=queried;
 }
 GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&src,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);
 GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glBufferData(GL_SHADER_STORAGE_BUFFER,64*4*sizeof(float),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);
 GLuint target=0; if(stores){glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);glTexStorage2D(GL_TEXTURE_2D,1,GL_R32F,16,16);glBindImageTexture(0,target,0,GL_FALSE,0,GL_WRITE_ONLY,GL_R32F);}
 unsigned bad=0;
 for(unsigned mode=0;mode<3;mode++){
  float depth[120];struct{float depth;unsigned stencil;} ds[120];
  for(unsigned i=0;i<120;i++){depth[i]=(i+1)/256.f;ds[i].depth=depth[i];ds[i].stencil=(i*7)&255;}
  GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,mode==0?GL_R32F:mode==1?GL_DEPTH_COMPONENT32F:GL_DEPTH32F_STENCIL8,12,10,0,mode==0?GL_RED:mode==1?GL_DEPTH_COMPONENT:GL_DEPTH_STENCIL,mode==2?GL_FLOAT_32_UNSIGNED_INT_24_8_REV:GL_FLOAT,mode==2?(void*)ds:(void*)depth);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
  CHECK(glGetError()==GL_NO_ERROR);glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);float actual[64][4];if(!stores)glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(actual),actual);CHECK(glGetError()==GL_NO_ERROR);
  if(stores){float pixels[256];glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);glBindTexture(GL_TEXTURE_2D,target);glGetTexImage(GL_TEXTURE_2D,0,GL_RED,GL_FLOAT,pixels);const int dx[]={0,1,1,0},dy[]={1,1,0,0};for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)for(unsigned c=0;c<4;c++)actual[y*8+x][c]=pixels[(y*2+dy[c])*16+x*2+dx[c]];CHECK(glGetError()==GL_NO_ERROR);}
  unsigned errors=0;const int dx[]={0,1,1,0},dy[]={1,1,0,0};
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)for(unsigned c=0;c<4;c++){
   int px=clamp((int)x*2-1+dx[c],12),py=clamp((int)y*2-1+dy[c],10);float expected=depth[py*12+px];
   if(actual[y*8+x][c]!=expected){if(errors<5)fprintf(stderr,"mode%u (%u,%u) c%u=%g expected %g\n",mode,x,y,c,actual[y*8+x][c],expected);errors++;}
  }
  printf("%s: gather format%u,64 samples (%u mismatched words)\n",errors?"FAIL":"PASS",mode,errors);bad+=errors;glDeleteTextures(1,&tex);
 }
 if(target)glDeleteTextures(1,&target);
 glDeleteBuffers(1,&buffer);glDeleteProgram(program);glDeleteShader(shader);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return !!bad;
}
