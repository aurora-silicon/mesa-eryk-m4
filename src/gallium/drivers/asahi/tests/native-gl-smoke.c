/* SPDX-License-Identifier: MIT
 * Native-Gallium/Zink pixel checks. Use the mandatory watchdog/thermal guard.
 * Choose clear/draw/texture/depth via M3_NATIVE_PROBE_MODE; each is a separate
 * bounded run so a failure does not cause another GPU submission.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Failure at line %d: %s (GL %x)\n",__LINE__,#x,glGetError());exit(1); } } while(0)
static GLuint shader(GLenum type,const char *text) {
 GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}CHECK(ok);return s;
}
int main(void) {
 setbuf(stdout,NULL);setbuf(stderr,NULL);
 const char *mode=getenv("M3_NATIVE_PROBE_MODE");if(!mode)mode="clear";
 const int vertex_norm=!strncmp(mode,"vbo-",4);
 CHECK(!strcmp(mode,"clear")||!strcmp(mode,"draw")||!strcmp(mode,"texture")||!strcmp(mode,"depth")||
       !strcmp(mode,"vbo-unorm8")||!strcmp(mode,"vbo-snorm8")||!strcmp(mode,"vbo-unorm16")||!strcmp(mode,"vbo-snorm16"));
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL));CHECK(eglBindAPI(EGL_OPENGL_API));
 const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);CHECK(c!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 printf("Renderer: %s\nVersion: %s\nMode: %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),mode);
 const char *wanted=getenv("GALLIUM_DRIVER");CHECK(wanted!=NULL);
 if(!strcmp(wanted,"asahi")){CHECK(!strstr((const char*)glGetString(GL_RENDERER),"zink"));CHECK(strstr((const char*)glGetString(GL_RENDERER),"Apple"));}
 else CHECK(strstr((const char*)glGetString(GL_RENDERER),"zink"));
 GLuint f,r,depth;glGenFramebuffers(1,&f);glBindFramebuffer(GL_FRAMEBUFFER,f);glGenRenderbuffers(1,&r);glBindRenderbuffer(GL_RENDERBUFFER,r);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,32,32);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,r);
 glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,32,32);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
 CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,32,32);if(!strcmp(mode,"clear"))glClearColor(0,1,0,1);else glClearColor(1,0,1,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 if(strcmp(mode,"clear")) {
  const char *vs=vertex_norm?"#version 330 core\nlayout(location=0) in vec4 color;out vec4 vcolor;void main(){vec2 v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(v*2.-1.,0.,1.);vcolor=color;}":"#version 330 core\nuniform float z;void main(){vec2 v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(v*2.-1.,z,1.);}";
  GLuint p=glCreateProgram();glAttachShader(p,shader(GL_VERTEX_SHADER,vs));
  const char *fs=vertex_norm?"#version 330 core\nin vec4 vcolor;out vec4 c;void main(){c=vcolor;}":!strcmp(mode,"texture")?"#version 330 core\nuniform sampler2D tex;out vec4 c;void main(){c=texture(tex,vec2(0.5));}":"#version 330 core\nuniform vec4 color;out vec4 c;void main(){c=color;}";
  glAttachShader(p,shader(GL_FRAGMENT_SHADER,fs));glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  if(vertex_norm){
   unsigned char u8[]={0,255,0,255,0,255,0,255,0,255,0,255};
   signed char s8[]={0,127,0,127,0,127,0,127,0,127,0,127};
   unsigned short u16[]={0,65535,0,65535,0,65535,0,65535,0,65535,0,65535};
   short s16[]={0,32767,0,32767,0,32767,0,32767,0,32767,0,32767};
   const void *data;unsigned size;GLenum type;
   if(!strcmp(mode,"vbo-unorm8")){data=u8;size=sizeof(u8);type=GL_UNSIGNED_BYTE;}
   else if(!strcmp(mode,"vbo-snorm8")){data=s8;size=sizeof(s8);type=GL_BYTE;}
   else if(!strcmp(mode,"vbo-unorm16")){data=u16;size=sizeof(u16);type=GL_UNSIGNED_SHORT;}
   else{data=s16;size=sizeof(s16);type=GL_SHORT;}
   GLuint vbo;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,size,data,GL_STATIC_DRAW);glVertexAttribPointer(0,4,type,GL_TRUE,0,NULL);glEnableVertexAttribArray(0);
  }
  if(!strcmp(mode,"texture")){GLuint tex;unsigned char green[]={0,255,0,255};glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,green);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glUniform1i(glGetUniformLocation(p,"tex"),0);}
  else glUniform4f(glGetUniformLocation(p,"color"),0,1,0,1);
  glUniform1f(glGetUniformLocation(p,"z"),-.5f);
  if(!strcmp(mode,"depth")) {glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);}
  glDrawArrays(GL_TRIANGLES,0,3);
  if(!strcmp(mode,"depth")){glUniform4f(glGetUniformLocation(p,"color"),1,0,0,1);glUniform1f(glGetUniformLocation(p,"z"),.5f);glDrawArrays(GL_TRIANGLES,0,3);}
 }
 CHECK(glGetError()==GL_NO_ERROR);unsigned char px[32*32*4];memset(px,0xcc,sizeof(px));glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,px);CHECK(glGetError()==GL_NO_ERROR);unsigned bad=0;
 for(unsigned i=0;i<32*32;i++)bad+=px[4*i]!=0||px[4*i+1]!=255||px[4*i+2]!=0||px[4*i+3]!=255;
 printf("First pixel: %u %u %u %u; mismatches=%u\n",px[0],px[1],px[2],px[3],bad);CHECK(bad==0);puts("PASS: native-GL pixel oracle");
 glFinish();eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return 0;
}
