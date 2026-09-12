/* SPDX-License-Identifier: MIT
 * Implicit derivatives plus shader/sampler mip-bias oracle; run hardware under GPU guard.
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
int main(void) {
  EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, 0, 0);
  CHECK(eglInitialize(d,0,0) && eglBindAPI(EGL_OPENGL_API));
  const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
  EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
  CHECK(c!=EGL_NO_CONTEXT && eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
  const char *renderer=(const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s\n",renderer);fflush(stdout);
  CHECK((strstr(renderer,"zink") && strstr(renderer,"M4")) ||
    (getenv("M4_TEST_LLVMPIPE") && strstr(renderer,"llvmpipe")));
  GLuint program=glCreateProgram();
  glAttachShader(program,shader(GL_VERTEX_SHADER,
    "#version 330 core\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
    "gl_Position=vec4(p*2.-1.,0.,1.);uv=p*4.;}"));
  glAttachShader(program,shader(GL_FRAGMENT_SHADER,
    "#version 330 core\nin vec2 uv;out vec4 color;uniform sampler2D tex;uniform float bias;"
    "void main(){color=texture(tex,uv,bias+(gl_FragCoord.x<32.?-1.:1.));}"));
  glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);
  glUseProgram(program);GLint bias=glGetUniformLocation(program,"bias");CHECK(bias>=0);
  GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,6);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  for(unsigned level=0;level<=6;level++) {
    unsigned size=64>>level;unsigned char pixels[64*64*4];
    for(unsigned i=0;i<size*size;i++) {
      pixels[i*4]=16+32*level;pixels[i*4+1]=pixels[i*4+2]=0;pixels[i*4+3]=255;
    }
    glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  }
  GLuint fbo,rb;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
  glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,64,64);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glViewport(0,0,64,64);unsigned total_bad=0;
  const float biases[]={0,.25f,-.5f};
  for(unsigned sampler=0;sampler<2;sampler++) for(unsigned j=0;j<3;j++) {
    float api_bias=.5f*sampler;
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,api_bias);
    glUniform1f(bias,biases[j]);glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char pixels[64*64*4];memset(pixels,0xa5,sizeof(pixels));
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);
    unsigned bad=0;
    for(unsigned i=0;i<64*64;i++) {
      // UV covers four repeats of a 64px texture in 64px: implicit LOD=2.
      float lod=2+api_bias+biases[j]+(i%64<32?-1:1);
      int red=lroundf(16+32*lod);
      if(abs(pixels[4*i]-red)>1 || pixels[4*i+1] || pixels[4*i+2] || pixels[4*i+3]!=255) {
        if(bad<2) fprintf(stderr,"pixel%u=%u,%u,%u,%u expected=%d,0,0,255\n",i,
          pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],red);
        bad++;
      }
    }
    printf("shader_bias=%g sampler_bias=%g bad=%u/4096\n",biases[j],api_bias,bad);fflush(stdout);
    total_bad+=bad;
  }
  glFinish();glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&rb);
  glDeleteTextures(1,&tex);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);
  printf("%s: shader and sampler mip bias\n",total_bad?"FAIL":"PASS");return total_bad?1:0;
}
