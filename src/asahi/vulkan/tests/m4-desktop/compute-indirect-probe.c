/* SPDX-License-Identifier: MIT
 * GPU-produced indirect dispatch counts, through Zink. Guard hardware runs. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError()); exit(1); } } while(0)
static GLuint program(const char *s) {
 GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&s,NULL);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);
 GLuint p=glCreateProgram();glAttachShader(p,shader);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glDeleteShader(shader);return p;
}
#define PREFIX "#version 330 core\n#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shader_storage_buffer_object: require\n#extension GL_ARB_shading_language_420pack: require\n"
int main(void) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint setup=program(PREFIX "layout(local_size_x=1) in;layout(std430,binding=0) buffer Args{uint a[];};uniform uint empty_axis;void main(){a[0]=empty_axis==1u?0u:3u;a[1]=empty_axis==2u?0u:2u;a[2]=empty_axis==3u?0u:2u;}");
 GLuint run=program(PREFIX "layout(local_size_x=4,local_size_y=2,local_size_z=2) in;layout(std430,binding=1) buffer Out{uint a[];};void main(){uvec3 p=gl_GlobalInvocationID;uint i=(p.z*4u+p.y)*12u+p.x;a[i]=1u+i+gl_NumWorkGroups.x*1000u+gl_NumWorkGroups.y*100u+gl_NumWorkGroups.z*10u;}");
 GLuint buffers[2];glGenBuffers(2,buffers);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,12,NULL,GL_DYNAMIC_COPY);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[0]);glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[0]);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);unsigned actual[192],zero[192]={0};glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(actual),zero,GL_DYNAMIC_COPY);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[1]);
 unsigned bad=0;
 for(unsigned empty=0;empty<4;empty++) {
  glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(zero),zero);
  glUseProgram(setup);glUniform1ui(glGetUniformLocation(setup,"empty_axis"),empty);glDispatchCompute(1,1,1);glMemoryBarrier(GL_COMMAND_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(run);glDispatchComputeIndirect(0);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(actual),actual);CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned i=0;i<192;i++){unsigned expected=empty?0:3221+i;if(actual[i]!=expected){if(bad<4)fprintf(stderr,"case%u word%u=%u expected%u\n",empty,i,actual[i],expected);bad++;}}
 }
 printf("%s: GPU-produced indirect XYZ counts and zero axes, 768 words, bad=%u\n",bad?"FAIL":"PASS",bad);
 glDeleteProgram(setup);glDeleteProgram(run);glDeleteBuffers(2,buffers);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return !!bad;
}
