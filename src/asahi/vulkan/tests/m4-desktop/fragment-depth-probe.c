/* SPDX-License-Identifier: MIT
 * Fragment depth writeout oracle; run hardware under GPU guard.
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
  bool constant=false;unsigned samples=1;
  for(int i=1;i<argc;i++) {
    if(!strcmp(argv[i],"constant"))constant=true;
    else {samples=atoi(argv[i]);CHECK(samples==1 || samples==2 || samples==4);}
  }
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);
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
    "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}"));
  const char *fs=constant ? "#version 330 core\nuniform float level;out vec4 color;void main(){gl_FragDepth=level;color=vec4(level,0,0,1);}" : "#version 330 core\n"
    "uniform float level;out vec4 color;void main(){"
    "gl_FragDepth=gl_FragCoord.x<32?level:1.-level;color=vec4(1,0,0,1);}";
  glAttachShader(program,shader(GL_FRAGMENT_SHADER,fs));glLinkProgram(program);
  GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
  GLint loc=glGetUniformLocation(program,"level");CHECK(loc>=0);
  GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  unsigned char pixels[64*64*4];float depth[64*64];
  GLuint fb,rb;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
  glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples==1?0:samples,GL_RGBA8,64,64);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  GLuint dr;glGenRenderbuffers(1,&dr);glBindRenderbuffer(GL_RENDERBUFFER,dr);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples==1?0:samples,GL_DEPTH_COMPONENT32F,64,64);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,dr);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  GLuint resolve_fb=0,resolve_rb[2]={0};
  if(samples>1) {
    glGenFramebuffers(1,&resolve_fb);glBindFramebuffer(GL_FRAMEBUFFER,resolve_fb);
    glGenRenderbuffers(2,resolve_rb);
    glBindRenderbuffer(GL_RENDERBUFFER,resolve_rb[0]);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,64,64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,resolve_rb[0]);
    glBindRenderbuffer(GL_RENDERBUFFER,resolve_rb[1]);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT32F,64,64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,resolve_rb[1]);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  }
  printf("samples=%u\n",samples);
  glViewport(0,0,64,64);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
  unsigned bad=0;
  for(unsigned step=0;step<3;step++) {
    glBindFramebuffer(GL_FRAMEBUFFER,fb);
    float level=(step+1)*.125f;glUniform1f(loc,level);
    glClearColor(0,0,0,1);glClearDepth(.6);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES,0,3);glFinish();CHECK(glGetError()==GL_NO_ERROR);
    if(samples>1) {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolve_fb);
      glBlitFramebuffer(0,0,64,64,0,0,64,64,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
      CHECK(glGetError()==GL_NO_ERROR);glBindFramebuffer(GL_READ_FRAMEBUFFER,resolve_fb);
    }
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glReadPixels(0,0,64,64,GL_DEPTH_COMPONENT,GL_FLOAT,depth);CHECK(glGetError()==GL_NO_ERROR);
    for(unsigned q=0;q<4;q++) {
      unsigned i=(unsigned[]){0,32,2048,4095}[q];
      printf("sample%u rgba=%u,%u,%u,%u depth=%a\n",i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],depth[i]);
    }
    const char *out=getenv("M4_PROBE_OUTPUT");
    if(out) {
      char path[1024];snprintf(path,sizeof(path),"%s/depth-step%u.bin",out,step);
      FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(depth,sizeof(depth),1,f)==1);CHECK(fclose(f)==0);
      snprintf(path,sizeof(path),"%s/color-step%u.rgba",out,step);
      f=fopen(path,"wb");CHECK(f);CHECK(fwrite(pixels,sizeof(pixels),1,f)==1);CHECK(fclose(f)==0);
    }
    unsigned n=0,color_bad=0,depth_bad=0;
    for(unsigned i=0;i<4096;i++) {
      bool left=constant || i%64<32;
      unsigned red=left?(constant?(unsigned)roundf(level*255):255):0;
      color_bad += abs(pixels[4*i]-(int)red)>1 || pixels[4*i+1] || pixels[4*i+2] || pixels[4*i+3]!=255;
      depth_bad += (!isfinite(depth[i]) || fabsf(depth[i]-(left?level:.6f))>1e-6);
      if(abs(pixels[4*i]-(int)red)>1 || pixels[4*i+1] || pixels[4*i+2] || pixels[4*i+3]!=255 || (!isfinite(depth[i]) || fabsf(depth[i]-(left?level:.6f))>1e-6)) {
        if(n<2)fprintf(stderr,"pixel%u rgba=%u,%u,%u,%u depth=%a expected=%f\n",i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],depth[i],left?level:.6f);
        n++;
      }
    }
    printf("step%u bad=%u/4096 color=%u depth=%u\n",step,n,color_bad,depth_bad);bad+=n;
  }
  glDeleteFramebuffers(1,&resolve_fb);glDeleteRenderbuffers(2,resolve_rb);
  glDeleteFramebuffers(1,&fb);glDeleteRenderbuffers(1,&rb);glDeleteRenderbuffers(1,&dr);
  glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);
  printf("%s: fragment depth writes\n",bad?"FAIL":"PASS");return bad?1:0;
}
