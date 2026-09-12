/* SPDX-License-Identifier: MIT
 * Exact signed/unsigned integer texture sampling through Zink.
 * Hardware execution requires gpu-experiment.py with watchdog/thermal guards.
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
 GLuint shader=glCreateShader(stage);glShaderSource(shader,1,&s,NULL);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return shader;
}

int main(int argc, char **argv) {
 const int offset = argc==2 && !strcmp(argv[1],"--offset");
 CHECK(argc==1 || offset);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 unsigned bad=0;
 for(unsigned sign=0;sign<2;sign++) {
  unsigned pixels[16*16*4], actual[16*16*4];
  for(unsigned i=0;i<16*16*4;i++)pixels[i]=0x71000301u+i*0x10001u+(sign?0x80000000u:0u);
  GLuint textures[2],fb,vao;glGenTextures(2,textures);
  GLenum type=sign?GL_INT:GL_UNSIGNED_INT,format=sign?GL_RGBA32I:GL_RGBA32UI;
  for(unsigned i=0;i<2;i++){
   glBindTexture(GL_TEXTURE_2D,textures[i]);
   glTexImage2D(GL_TEXTURE_2D,0,format,16,16,0,GL_RGBA_INTEGER,type,i?NULL:pixels);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  }
  glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  GLuint program=glCreateProgram();
  glAttachShader(program,compile(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
  char src[512];snprintf(src,sizeof(src),"#version 330 core\nuniform %csampler2D tex;out %cvec4 color;void main(){color=%s;}",sign?'i':'u',sign?'i':'u',offset?"textureLodOffset(tex,gl_FragCoord.xy/16.,0.,ivec2(-1,2))":"texture(tex,gl_FragCoord.xy/16.)");
  glAttachShader(program,compile(GL_FRAGMENT_SHADER,src));glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
  glBindTexture(GL_TEXTURE_2D,textures[0]);glUniform1i(glGetUniformLocation(program,"tex"),0);
  glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,16,16);glDrawArrays(GL_TRIANGLES,0,3);
  glReadPixels(0,0,16,16,GL_RGBA_INTEGER,type,actual);CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned i=0;i<16*16*4;i++){unsigned x=(i/4)%16,y=i/64,c=i%4;unsigned j=offset?((((y+2)%16)*16+(x+15)%16)*4+c):i;if(actual[i]!=pixels[j]){if(bad<4)fprintf(stderr,"%s word %u: %08x expected %08x\n",sign?"signed":"unsigned",i,actual[i],pixels[j]);bad++;}}
  glDeleteProgram(program);glDeleteTextures(2,textures);glDeleteFramebuffers(1,&fb);glDeleteVertexArrays(1,&vao);
 }
 printf("%s: signed/unsigned normalized-coordinate nearest texture samples offset=%d (%u mismatches)\n",bad?"FAIL":"PASS",offset,bad);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return !!bad;
}
