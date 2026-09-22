/* SPDX-License-Identifier: MIT
 * Per-sample mask/resolve diagnostic. Run through gpu-experiment.py.
 * Build: cc -O2 gl-sample-mask-probe.c -lEGL -lGL -o gl-sample-mask-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError());exit(1);}}while(0)
static GLuint shader(GLenum type,const char *text){GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return s;}
int main(void){
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,6,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 GLuint vao,tex,fb,out,resolve;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE,1,&tex);glTextureStorage2DMultisample(tex,2,GL_RGBA8,32,32,GL_TRUE);
 glCreateFramebuffers(1,&fb);glNamedFramebufferTexture(fb,GL_COLOR_ATTACHMENT0,tex,0);
 glCreateTextures(GL_TEXTURE_2D,1,&out);glTextureStorage2D(out,1,GL_RGBA8,32,32);
 glCreateFramebuffers(1,&resolve);glNamedFramebufferTexture(resolve,GL_COLOR_ATTACHMENT0,out,0);
 char vs[1024];snprintf(vs,sizeof(vs),"#version 460 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);gl_ClipDistance[0]=1;%s}",getenv("PROBE_NO_CULL")?"":"gl_CullDistance[0]=1;");
 glEnable(GL_CLIP_DISTANCE0);unsigned bad=0;
 for(unsigned mask=0;mask<4;mask++){
  char fs[2048];snprintf(fs,sizeof(fs),"#version 460 core\nout vec4 color;void main(){if(gl_SampleID==0){vec2 p=gl_SamplePosition;color=vec4(1,p.x,p.y,1);}else color=vec4(0,1,0,1);gl_SampleMask[0]=%u;}",mask);
  GLuint p=glCreateProgram(),v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);
  glBindFramebuffer(GL_FRAMEBUFFER,fb);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,32,32);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
  if(getenv("PROBE_DSA"))glBlitNamedFramebuffer(fb,resolve,0,0,32,32,0,0,32,32,GL_COLOR_BUFFER_BIT,GL_NEAREST);else{glBindFramebuffer(GL_READ_FRAMEBUFFER,fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolve);glBlitFramebuffer(0,0,32,32,0,0,32,32,GL_COLOR_BUFFER_BIT,GL_NEAREST);}glBindFramebuffer(GL_READ_FRAMEBUFFER,resolve);
  unsigned char a[4];glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,a);CHECK(glGetError()==0);
  printf("mask=%u rgba=%u,%u,%u,%u\n",mask,a[0],a[1],a[2],a[3]);
  if(mask==0&&(a[0]||a[1]||a[2]||a[3]))bad++;
  if(mask==2&&(a[0]!=0||a[1]<127||a[1]>128||a[2]!=0||a[3]<127||a[3]>128))bad++;
  if(mask==3&&(a[0]<127||a[0]>128||a[3]!=255))bad++;
  glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
 }
 printf("%s: sample mask (%u failures)\n",bad?"FAIL":"PASS",bad);return bad!=0;
}
