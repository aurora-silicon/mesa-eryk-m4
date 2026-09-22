/* SPDX-License-Identifier: MIT
 * Eight-target colour broadcast/remapping oracle. Run under the M3 GPU guard.
 * cc -O2 gl-broadcast-probe.c -lEGL -lGL -o gl-broadcast-probe
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
static GLuint shader(GLenum type,const char *source) {
 GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return s;
}
int main(void) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,a);
 CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 printf("Renderer: %s\n",glGetString(GL_RENDERER));fflush(stdout);
 GLint max;glGetIntegerv(GL_MAX_DRAW_BUFFERS,&max);CHECK(max>=8);
 GLuint vao,fb,tex[8];glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(8,tex);
 GLenum attachments[8];
 for(unsigned t=0;t<8;t++){
  attachments[t]=GL_COLOR_ATTACHMENT0+t;glBindTexture(GL_TEXTURE_2D,tex[t]);
  glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,64,32);
  glFramebufferTexture2D(GL_FRAMEBUFFER,attachments[t],GL_TEXTURE_2D,tex[t],0);
 }
 glViewport(0,0,64,32);glDisable(GL_DITHER);
 for(unsigned variant=0;variant<3;variant++){
  char fs[4096]="#version 330 core\n";size_t used=strlen(fs);
  for(unsigned t=0;t<8;t++)used+=snprintf(fs+used,sizeof(fs)-used,"layout(location=%u) out vec4 c%u;\n",t,t);
  used+=snprintf(fs+used,sizeof(fs)-used,"void main(){vec4 v=vec4(float((int(gl_FragCoord.x)*3+17)%%256),64,128,255)/255.;");
  for(unsigned t=0;t<8;t++)used+=snprintf(fs+used,sizeof(fs)-used,"c%u=v%s;",t,variant==1&&t==4?".bgra":"");
  if(variant==2)used+=snprintf(fs+used,sizeof(fs)-used,"if(int(gl_FragCoord.y)>=16)discard;");
  snprintf(fs+used,sizeof(fs)-used,"}");
  GLuint p=glCreateProgram();
  glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(v*2.-1.,0,1);}"));
  glAttachShader(p,shader(GL_FRAGMENT_SHADER,fs));glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);
  for(unsigned mapping=0;mapping<3;mapping++){
   GLenum draw[8];
   glDrawBuffers(8,attachments);
   const float clear[]={0,0,0,0};for(unsigned t=0;t<8;t++)glClearBufferfv(GL_COLOR,t,clear);
   for(unsigned t=0;t<8;t++)draw[t]=mapping==0?attachments[t]:mapping==1?attachments[7-t]:t==7?attachments[3]:GL_NONE;
   glDrawBuffers(8,draw);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glDrawArrays(GL_TRIANGLES,0,3);glFinish();CHECK(glGetError()==GL_NO_ERROR);
   unsigned bad=0;
   for(unsigned t=0;t<8;t++){
    unsigned char pixels[64*32*4];glReadBuffer(attachments[t]);glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<64;x++){
     unsigned loc=mapping==0?t:mapping==1?7-t:7;
     unsigned char want[4]={(x*3+17)%256,64,128,255};
     if(variant==1&&loc==4){unsigned char c=want[0];want[0]=want[2];want[2]=c;}
     if((mapping==2&&t!=3)||(variant==2&&y>=16))memset(want,0,4);
     for(unsigned c=0;c<4;c++)bad+=abs((int)pixels[(y*64+x)*4+c]-want[c])>1;
    }
   }
   CHECK(glGetError()==GL_NO_ERROR);printf("variant=%u mapping=%u bad=%u\n",variant,mapping,bad);fflush(stdout);CHECK(!bad);
  }
  glDeleteProgram(p);
 }
 puts("PASS: eight colour targets, distinct-output rejection, reversed/sparse mapping and discard");
 return 0;
}
