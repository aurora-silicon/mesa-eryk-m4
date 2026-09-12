/* SPDX-License-Identifier: MIT
 * Half-float and packed HDR attachment clear, draw, blend and reload oracle.
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
  const char *fs = "#version 330 core\nprecision highp float;layout(location=0)out vec4 a;layout(location=1)out vec4 b;void main(){a=vec4(.5,1.,2.,0.);b=vec4(1.,4.,8.,0.);}";
  GLuint program=glCreateProgram();glAttachShader(program,shader(GL_VERTEX_SHADER,vs));glAttachShader(program,shader(GL_FRAGMENT_SHADER,fs));glLinkProgram(program);
  GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
  GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  const GLenum formats[]={GL_R16F,GL_RGBA16F,GL_R11F_G11F_B10F,GL_R11F_G11F_B10F,GL_RGBA8};
  const int components[]={1,4,3,3,4};
  const float clear[2][4]={{.25,2.,64.,1.},{2.,4.,8.,1.}};
  const float draw[2][4]={{.5,1.,2.,0.},{1.,4.,8.,0.}};
  unsigned failures=0;
  for(unsigned test=0;test<5;++test){
    unsigned count=test>=3?2:1;
    GLuint fb,tex[2];glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(count,tex);
    GLenum attachments[2]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
    for(unsigned rt=0;rt<count;++rt){glBindTexture(GL_TEXTURE_2D,tex[rt]);glTexStorage2D(GL_TEXTURE_2D,1,formats[test],128,64);glFramebufferTexture2D(GL_FRAMEBUFFER,attachments[rt],GL_TEXTURE_2D,tex[rt],0);}
    glDrawBuffers(count,attachments);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,128,64);
    for(unsigned phase=0;phase<3;++phase){
      printf("HDR case=%u targets=%u phase=%u\n",test,count,phase);fflush(stdout);
      if(phase==0){for(unsigned rt=0;rt<count;++rt)glClearBufferfv(GL_COLOR,rt,clear[rt]);}
      else{if(phase==1)glDisable(GL_BLEND);else{glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);}glDrawArrays(GL_TRIANGLES,0,3);}
      CHECK(glGetError()==GL_NO_ERROR);
      for(unsigned rt=0;rt<count;++rt){
        uint16_t pixels[128*64*4];memset(pixels,0xa5,sizeof(pixels));glReadBuffer(attachments[rt]);glReadPixels(0,0,128,64,GL_RGBA,GL_HALF_FLOAT,pixels);CHECK(glGetError()==GL_NO_ERROR);
        unsigned bad=0;
        for(unsigned px=0;px<128*64;++px)for(unsigned c=0;c<4;++c){
          float expected=c>=(unsigned)components[test]?(c==3?1.:0.):phase==0?clear[rt][c]:draw[rt][c]*(phase==2?2.:1.);
          if (test == 4) expected = roundf(fminf(fmaxf(expected,0),1)*255)/255;
          uint16_t h=pixels[px*4+c];
          float actual=(h&0x8000?-1.f:1.f)*ldexpf((h&0x7c00)?1.f+(h&1023)/1024.f:(h&1023)/1024.f,((h>>10)&31)?((h>>10)&31)-15:-14);
          if((h&0x7c00)==0x7c00)actual=NAN;
          if(!isfinite(actual)||fabsf(actual-expected)>.001){if(bad<3)fprintf(stderr,"mismatch case=%u phase=%u rt=%u px=%u c=%u got=%g expected=%g\n",test,phase,rt,px,c,actual,expected);++bad;}
        }
        printf("case=%u phase=%u rt=%u bad=%u\n",test,phase,rt,bad);fflush(stdout);failures+=bad;
      }
    }
    glDisable(GL_BLEND);glDeleteFramebuffers(1,&fb);glDeleteTextures(count,tex);
  }
  glFinish();printf("%s: HDR attachments clear, draw, additive blend and reload\n",failures?"FAIL":"PASS");
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);
  return failures?1:0;
}
