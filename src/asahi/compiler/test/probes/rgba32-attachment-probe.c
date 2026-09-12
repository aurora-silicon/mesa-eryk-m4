/* SPDX-License-Identifier: MIT
 * RGBA32 float/int/uint attachment clear, draw and reload oracle.
 * Hardware invocation requires tools/gpu-experiment.py watchdog/thermal guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL: line %d: %s GL=%x\n", __LINE__, #x,               \
              glGetError());                                                   \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum type, const char *s) {
  GLuint p = glCreateShader(type);
  glShaderSource(p, 1, &s, 0);
  glCompileShader(p);
  GLint ok;
  glGetShaderiv(p, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(p, sizeof(log), 0, log);
    fprintf(stderr, "%s\n", log);
  }
  CHECK(ok);
  return p;
}

int main(void) {
  EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, 0, 0);
  CHECK(eglInitialize(d, 0, 0)); CHECK(eglBindAPI(EGL_OPENGL_API));
  EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLContext ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  CHECK(ctx != EGL_NO_CONTEXT);
  CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s\n",renderer); fflush(stdout);
  CHECK((strstr(renderer,"zink") && strstr(renderer,"M4")) ||
        (getenv("M4_TEST_LLVMPIPE") && strstr(renderer,"llvmpipe")));
  const char *vs = "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}";

  const GLenum formats[]={GL_RGBA32F,GL_RGBA32I,GL_RGBA32UI};
  const char *types[]={"vec4","ivec4","uvec4"};
  const char *values[]={"vec4(1.0001220703125,-3.75,100000.,.5)","ivec4(16777217,-123456789,2147483000,-1)","uvec4(16777217u,0xfedcba98u,0x80000000u,0xffffffffu)"};
  const float floats[]={1.0001220703125f,-3.75f,100000.f,.5f};
  const int32_t ints[]={16777217,-123456789,2147483000,-1};
  const uint32_t uints[]={16777217u,0xfedcba98u,0x80000000u,0xffffffffu};
  const void *expected[]={floats,ints,uints};
  GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  unsigned failures=0;
  for(unsigned test=0;test<3;++test){
    char fs[512];snprintf(fs,sizeof(fs),"#version 330 core\nlayout(location=0)out %s color;void main(){color=%s;}",types[test],values[test]);
    GLuint program=glCreateProgram();glAttachShader(program,shader(GL_VERTEX_SHADER,vs));glAttachShader(program,shader(GL_FRAGMENT_SHADER,fs));glLinkProgram(program);
    GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
    GLuint fb,tex;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);glTexStorage2D(GL_TEXTURE_2D,1,formats[test],64,64);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,64,64);
    for(unsigned phase=0;phase<3;++phase){
      printf("RGBA32 case=%u phase=%u\n",test,phase);fflush(stdout);
      if(phase==0){if(test==0)glClearBufferfv(GL_COLOR,0,floats);else if(test==1)glClearBufferiv(GL_COLOR,0,ints);else glClearBufferuiv(GL_COLOR,0,uints);}
      else{if(phase==2){glEnable(GL_SCISSOR_TEST);glScissor(16,16,32,32);}glDrawArrays(GL_TRIANGLES,0,3);}
      CHECK(glGetError()==GL_NO_ERROR);
      uint32_t pixels[64*64*4];memset(pixels,0xa5,sizeof(pixels));
      glReadPixels(0,0,64,64,test==0?GL_RGBA:GL_RGBA_INTEGER,test==0?GL_FLOAT:test==1?GL_INT:GL_UNSIGNED_INT,pixels);CHECK(glGetError()==GL_NO_ERROR);
      unsigned bad=0;for(unsigned px=0;px<64*64;++px)if(memcmp(pixels+px*4,expected[test],16)){if(bad<2)fprintf(stderr,"pixel %u: %08x %08x %08x %08x\n",px,pixels[px*4],pixels[px*4+1],pixels[px*4+2],pixels[px*4+3]);++bad;}
      printf("case=%u phase=%u bad=%u/4096\n",test,phase,bad);fflush(stdout);failures+=bad;
    }
    glDisable(GL_SCISSOR_TEST);glDeleteFramebuffers(1,&fb);glDeleteTextures(1,&tex);glDeleteProgram(program);
  }
  glFinish();printf("%s: RGBA32 float/int/uint clear draw reload, exact 32-bit channels\n",failures?"FAIL":"PASS");
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return failures?1:0;
}
