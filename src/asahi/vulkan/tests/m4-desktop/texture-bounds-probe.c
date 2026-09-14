/* SPDX-License-Identifier: MIT
 * Diagnose sampled-image coordinates and gradients with exact SSBO readback.
 * Hardware runs must use gpu-experiment.py and run-m4-gl.py.
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
 EGLContext context=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(context!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 float data[8*8*4];for(unsigned i=0;i<8*8*4;i++)data[i]=i+1;
 GLuint tex,buf;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_3D,tex);
 glTexImage3D(GL_TEXTURE_3D,0,GL_R32F,8,8,4,0,GL_RED,GL_FLOAT,data);
 glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,0);
 for(unsigned i=0;i<2;i++)glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S+i,GL_CLAMP_TO_BORDER);
 glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_BORDER);
 CHECK(glGetError()==GL_NO_ERROR);
 const unsigned count=3*12;float output[count*4*4];memset(output,0,sizeof(output));
 glGenBuffers(1,&buf);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);
 glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(output),output,GL_DYNAMIC_COPY);
 glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
 const char *src="#version 330 core\n#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shader_storage_buffer_object: require\n#extension GL_ARB_shading_language_420pack: require\nlayout(local_size_x=1) in;layout(std430,binding=0) buffer Data { vec4 result[]; };layout(binding=0) uniform sampler3D tex;void main(){uint i=gl_GlobalInvocationID.x;int axis=int(i/12u),c=int(i%12u)-2;ivec3 p=ivec3(0);p[axis]=c;vec3 q=(vec3(p)+.25)/vec3(8,8,4);vec3 dd=vec3(0);dd[axis]=1.;result[i*4u]=vec4(textureSize(tex,0),1);result[i*4u+1u]=texture(tex,q);result[i*4u+2u]=textureLod(tex,q,0);result[i*4u+3u]=textureGrad(tex,q,dd,dd);}";
 if(getenv("M4_TEST_GRADIENT_ONLY"))src="#version 330 core\n#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shader_storage_buffer_object: require\n#extension GL_ARB_shading_language_420pack: require\nlayout(local_size_x=1) in;layout(std430,binding=0) buffer Data { vec4 result[]; };layout(binding=0) uniform sampler3D tex;void main(){uint i=gl_GlobalInvocationID.x;int axis=int(i/12u),c=int(i%12u)-2;ivec3 p=ivec3(0);p[axis]=c;vec3 q=(vec3(p)+.25)/vec3(8,8,4);vec3 dd=vec3(0);dd[axis]=1.;vec4 v=textureGrad(tex,q,dd,dd);result[i*4u]=vec4(8,8,4,1);result[i*4u+1u]=v;result[i*4u+2u]=v;result[i*4u+3u]=v;}";
 char adjusted[4096];
 if(getenv("M4_TEST_SMALL_GRADIENT")) {
  CHECK(strlen(src)<sizeof(adjusted));strcpy(adjusted,src);
  char *at=strstr(adjusted,"dd[axis]=1.;");CHECK(at);
  memcpy(at,"dd[axis]=.1;",11);src=adjusted;
 }
 GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&src,NULL);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);
 GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 glDispatchCompute(count,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
 glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(output),output);CHECK(glGetError()==GL_NO_ERROR);
 unsigned bad=0;
 for(unsigned i=0;i<count;i++)for(unsigned op=0;op<4;op++) {
  int axis=i/12,c=(int)(i%12)-2,inside=c>=0&&c<(axis==2?4:8);
  float expected[4]={inside?data[c*(axis==0?1:axis==1?8:64)]:0,0,0,inside?1:0};
  if(!op){expected[0]=8;expected[1]=8;expected[2]=4;expected[3]=1;}
  float *v=output+16*i+4*op;
  if(!inside && op && (v[3]==0 || v[3]==1)) expected[3]=v[3];
  if(memcmp(v,expected,sizeof(expected))) {
   if(bad<24)printf("axis%d coord%d op%u: (%g,%g,%g,%g) expected (%g,%g,%g,%g)\n",axis,c,op,v[0],v[1],v[2],v[3],expected[0],expected[1],expected[2],expected[3]);bad++;
  }
 }
 printf("%s: texture bounds and explicit gradients (%u mismatches)\n",bad?"FAIL":"PASS",bad);
 return bad?1:0;
}
