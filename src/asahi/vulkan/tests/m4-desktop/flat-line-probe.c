/* SPDX-License-Identifier: MIT
 * Flat line interpolation and provoking-vertex oracle through Zink.
 * Hardware execution requires gpu-experiment.py with watchdog/thermal guards.
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
static GLuint compile(GLenum stage, const char *s) {
 GLuint shader=glCreateShader(stage);glShaderSource(shader,1,&s,NULL);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return shader;
}


int main(int argc,char **argv) {
 const int constant=argc==2&&!strcmp(argv[1],"--constant");CHECK(argc==1||constant);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint program=glCreateProgram();
 glAttachShader(program,compile(GL_VERTEX_SHADER,"#version 330 core\nflat out vec4 c;void main(){gl_Position=vec4(gl_VertexID==0?-1.:1.,.03125,0,1);c=gl_VertexID==0?vec4(1,0,0,1):vec4(0,0,1,1);}"));
 glAttachShader(program,compile(GL_FRAGMENT_SHADER,constant?"#version 330 core\nout vec4 color;void main(){color=vec4(1,0,0,1);}":"#version 330 core\nflat in vec4 c;out vec4 color;void main(){color=c;}"));
 glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 GLuint fb,rb,vao;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
 glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,32,32);
 glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,32,32);
 unsigned bad=0;unsigned char pixels[32*32*4];
 for(unsigned strip=0;strip<2;strip++)for(unsigned last=0;last<2;last++) {
  glProvokingVertex(last?GL_LAST_VERTEX_CONVENTION:GL_FIRST_VERTEX_CONVENTION);
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(strip?GL_LINE_STRIP:GL_LINES,0,2);
  glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned i=0;i<1024;i++){
   unsigned char expected[]={i/32==16&&(!last||constant)?255:0,0,i/32==16&&last&&!constant?255:0,255};
   if(memcmp(pixels+4*i,expected,4)){if(bad<4)fprintf(stderr,"strip=%u last=%u pixel=%u got=%u,%u,%u\n",strip,last,i,pixels[4*i],pixels[4*i+1],pixels[4*i+2]);bad++;}
  }
 }
 printf("%s: flat lines and strips, first/last provoking vertex (%u mismatches)\n",bad?"FAIL":"PASS",bad);
 glDeleteProgram(program);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fb);glDeleteVertexArrays(1,&vao);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return !!bad;
}
