/* SPDX-License-Identifier: MIT
 * Exact fragment stencil export probe, with optional simultaneous depth.
 * Run through gpu-experiment.py and run-m4-gl.py, never unguarded on hardware.
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
 GLuint p=glCreateShader(stage);glShaderSource(p,1,&s,0);glCompileShader(p);
 GLint ok;glGetShaderiv(p,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(p,sizeof(log),0,log);fputs(log,stderr);}CHECK(ok);return p;
}
int main(int argc,char **argv) {
 int depth=argc==2&&!strcmp(argv[1],"depth");CHECK(argc==1||depth);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);CHECK(eglInitialize(d,0,0)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint fb,rb[2],vao,p=glCreateProgram();glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenRenderbuffers(2,rb);
 glBindRenderbuffer(GL_RENDERBUFFER,rb[0]);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,32,32);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb[0]);
 glBindRenderbuffer(GL_RENDERBUFFER,rb[1]);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH32F_STENCIL8,32,32);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,rb[1]);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glAttachShader(p,compile(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
 char fs[1024];snprintf(fs,sizeof(fs),"#version 330 core\n#extension GL_ARB_shader_stencil_export:require\nout vec4 color;void main(){gl_FragStencilRefARB=int(gl_FragCoord.x)*7+3;color=vec4(1,0,0,1);%s}",depth?"gl_FragDepth=.25;":"");
 glAttachShader(p,compile(GL_FRAGMENT_SHADER,fs));glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,32,32);
 glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glEnable(GL_STENCIL_TEST);glStencilFunc(GL_ALWAYS,0,255);glStencilOp(GL_REPLACE,GL_REPLACE,GL_REPLACE);glClearColor(0,0,0,1);glClearDepth(1);glClearStencil(0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
 unsigned char stencil[1024],rgba[4096];float z[1024];glReadPixels(0,0,32,32,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,stencil);glReadPixels(0,0,32,32,GL_DEPTH_COMPONENT,GL_FLOAT,z);glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,rgba);CHECK(glGetError()==GL_NO_ERROR);
 const char *out=getenv("M4_PROBE_OUTPUT");if(out){char path[1024];snprintf(path,sizeof(path),"%s/stencil.bin",out);FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(stencil,sizeof(stencil),1,f)==1);CHECK(fclose(f)==0);snprintf(path,sizeof(path),"%s/depth.bin",out);f=fopen(path,"wb");CHECK(f);CHECK(fwrite(z,sizeof(z),1,f)==1);CHECK(fclose(f)==0);}
 unsigned bad=0;for(unsigned i=0;i<1024;i++)if(stencil[i]!=(i%32)*7+3||z[i]!=(depth?.25f:.5f)||rgba[4*i]!=255||rgba[4*i+1]||rgba[4*i+2]||rgba[4*i+3]!=255){if(bad<4)fprintf(stderr,"pixel%u stencil=%u depth=%g red=%u\n",i,stencil[i],z[i],rgba[4*i]);bad++;}
 printf("%s: fragment stencil exact 1024 pixels depth=%d bad=%u\n",bad?"FAIL":"PASS",depth,bad);glDeleteProgram(p);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fb);glDeleteRenderbuffers(2,rb);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return !!bad;
}
