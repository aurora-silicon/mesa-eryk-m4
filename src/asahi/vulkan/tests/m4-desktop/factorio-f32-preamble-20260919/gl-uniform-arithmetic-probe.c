/* SPDX-License-Identifier: MIT
 * CPU-checked draw-uniform F32 arithmetic, including changed uniforms.
 * Run only under gpu-experiment.py's watchdog and temperature guard.
 * cc -O2 gl-uniform-arithmetic-probe.c -lEGL -lGL -lm -o gl-uniform-arithmetic-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError()); exit(1); } } while(0)
static GLuint shader(GLenum type,const char *text) {
   GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);
   GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
   if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
   CHECK(ok);return s;
}
int main(void) {
   EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
   CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
   EGLint attr[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
   EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attr);
   CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
   printf("Renderer: %s\n",glGetString(GL_RENDERER));
   GLuint p=glCreateProgram();
   glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 330 core\nlayout(std140) uniform Values {vec4 a;vec4 b;vec4 c;vec4 d;};out vec4 color;void main(){vec4 delta=b-a;vec4 gain=c*d;color=(abs(delta)*gain+a)*(gl_FragCoord.x/64.0);}"));
   glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);
   glUseProgram(p);glUniformBlockBinding(p,glGetUniformBlockIndex(p,"Values"),0);
   GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_UNIFORM_BUFFER,buffer);
   glBufferData(GL_UNIFORM_BUFFER,64,NULL,GL_DYNAMIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,buffer);
   GLuint vao,tex,fb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,64,4,0,GL_RGBA,GL_FLOAT,NULL);
   glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,64,4);float pixels[64*4*4];unsigned bad=0;
   for(unsigned round=0;round<32;++round){
      float v[4][4];
      for(unsigned k=0;k<4;++k){
         v[0][k]=((int)(round%7)-3)*(k+1)*0.125f;
         v[1][k]=((int)(round%11)-5)*(4-k)*0.25f;
         v[2][k]=(round%5+1)*0.125f;v[3][k]=(k+1)*0.5f;
      }
      glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(v),v);glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,64,4,GL_RGBA,GL_FLOAT,pixels);CHECK(glGetError()==GL_NO_ERROR);
      for(unsigned y=0;y<4;++y)for(unsigned x=0;x<64;++x)for(unsigned k=0;k<4;++k){
         /* All operands and results are exactly representable binary fractions. */
         float expect=(fabsf(v[1][k]-v[0][k])*v[2][k]*v[3][k]+v[0][k])*((x+0.5f)/64.0f);
         float actual=pixels[(y*64+x)*4+k];
         if(actual!=expect){if(bad<8)printf("round=%u x=%u y=%u c=%u actual=%a expected=%a\n",round,x,y,k,actual,expect);bad++;}
      }
      printf("round=%u bad=%u\n",round,bad);fflush(stdout);
   }
   CHECK(!bad);puts("PASS: F32 draw-uniform arithmetic and rebinding");
   return 0;
}
