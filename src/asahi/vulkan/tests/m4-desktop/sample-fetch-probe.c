/* SPDX-License-Identifier: MIT
 * Individual multisample texture read oracle; run hardware under GPU guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum type, const char *text) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &text, 0);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(s, sizeof(log), 0, log);
    fputs(log, stderr);
  }
  CHECK(ok);
  return s;
}
int main(int argc, char **argv) {
  unsigned samples=argc>1?atoi(argv[1]):4; CHECK(samples==2 || samples==4);
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);
  CHECK(eglInitialize(d,0,0) && eglBindAPI(EGL_OPENGL_API));
  const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
  EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
  CHECK(c!=EGL_NO_CONTEXT && eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
  const char *renderer=(const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s samples=%u\n",renderer,samples);fflush(stdout);
  CHECK((strstr(renderer,"zink") && strstr(renderer,"M4")) ||
    (getenv("M4_TEST_LLVMPIPE") && strstr(renderer,"llvmpipe")));
  GLuint vs=shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}");
  const char *fs[]={"#version 330 core\nuniform vec4 value;out vec4 color;void main(){color=value;}",
    "#version 330 core\nuniform sampler2DMS source;uniform int sample_id;out vec4 color;void main(){color=texelFetch(source,ivec2(gl_FragCoord.xy),sample_id);}"};
  GLuint prog[2];
  for(unsigned i=0;i<2;i++) {
    prog[i]=glCreateProgram();glAttachShader(prog[i],vs);GLuint f=shader(GL_FRAGMENT_SHADER,fs[i]);
    glAttachShader(prog[i],f);glLinkProgram(prog[i]);GLint ok;glGetProgramiv(prog[i],GL_LINK_STATUS,&ok);CHECK(ok);glDeleteShader(f);
  }
  GLuint vao,fb[2],tex[2];glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(2,fb);glGenTextures(2,tex);
  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,tex[0]);
  glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,samples,GL_RGBA8,64,64,GL_TRUE);CHECK(glGetError()==GL_NO_ERROR);
  glBindFramebuffer(GL_FRAMEBUFFER,fb[0]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D_MULTISAMPLE,tex[0],0);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glBindTexture(GL_TEXTURE_2D,tex[1]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glBindFramebuffer(GL_FRAMEBUFFER,fb[1]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[1],0);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glViewport(0,0,64,64);glBindFramebuffer(GL_FRAMEBUFFER,fb[0]);glUseProgram(prog[0]);
  static const unsigned char colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{64,128,191,255}};
  GLint value=glGetUniformLocation(prog[0],"value");CHECK(value>=0);
  glEnable(GL_SAMPLE_MASK);
  for(unsigned i=0;i<samples;i++) {
    glSampleMaski(0,1u<<i);glUniform4f(value,colors[i][0]/255.f,colors[i][1]/255.f,colors[i][2]/255.f,1);glDrawArrays(GL_TRIANGLES,0,3);
  }
  glDisable(GL_SAMPLE_MASK);glBindFramebuffer(GL_FRAMEBUFFER,fb[1]);glUseProgram(prog[1]);
  glUniform1i(glGetUniformLocation(prog[1],"source"),0);
  GLint sample_id=glGetUniformLocation(prog[1],"sample_id");CHECK(sample_id>=0);
  unsigned total_bad=0;unsigned char pixels[64*64*4];
  for(unsigned i=0;i<samples;i++) {
    glUniform1i(sample_id,i);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);
    unsigned bad=0;
    for(unsigned px=0;px<4096;px++)for(unsigned ch=0;ch<4;ch++) if(abs(pixels[4*px+ch]-colors[i][ch])>1) {bad++;break;}
    printf("sample=%u bad=%u/4096 first=%u,%u,%u,%u\n",i,bad,pixels[0],pixels[1],pixels[2],pixels[3]);total_bad+=bad;
  }
  glFinish();glDeleteTextures(2,tex);glDeleteFramebuffers(2,fb);glDeleteProgram(prog[0]);glDeleteProgram(prog[1]);glDeleteShader(vs);glDeleteVertexArrays(1,&vao);
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);
  printf("%s: individual multisample texture reads\n",total_bad?"FAIL":"PASS");return total_bad?1:0;
}
