/* SPDX-License-Identifier: MIT
 * Exact array-layer + varying mip-level texture sampling through Zink.
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

int main(void) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext context=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(context!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint tex,output,fb,vao;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D_ARRAY,tex);
 for(unsigned level=0;level<4;level++){
  unsigned size=8>>level;float data[size*size*4*4];
  for(unsigned layer=0;layer<4;layer++)for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++)for(unsigned c=0;c<4;c++)
   data[((layer*size+y)*size+x)*4+c]=level*512+layer*128+y*8+x+c*.125f;
  glTexImage3D(GL_TEXTURE_2D_ARRAY,level,GL_RGBA32F,size,size,4,0,GL_RGBA,GL_FLOAT,data);
 }
 glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
 glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAX_LEVEL,3);
 glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,128,1,0,GL_RGBA,GL_FLOAT,NULL);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
 CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 GLuint program=glCreateProgram();
 glAttachShader(program,compile(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
 glAttachShader(program,compile(GL_FRAGMENT_SHADER,"#version 330 core\nuniform sampler2DArray tex;out vec4 color;void main(){float i=floor(gl_FragCoord.x);float lod=mod(i,4.);float layer=mod(floor(i/4.),4.);float u=(floor(i/16.)+.5)/8.;color=textureLod(tex,vec3(u,1.-u,layer),lod);}"));
 glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 glUniform1i(glGetUniformLocation(program,"tex"),0);
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,128,1);glDrawArrays(GL_TRIANGLES,0,3);
 float actual[128*4];glReadPixels(0,0,128,1,GL_RGBA,GL_FLOAT,actual);CHECK(glGetError()==GL_NO_ERROR);
 unsigned bad=0;
 for(unsigned i=0;i<128;i++)for(unsigned c=0;c<4;c++){
  unsigned level=i%4,layer=(i/4)%4,size=8>>level;
  unsigned x=((2*(i/16)+1)*size)/16,y=((15-2*(i/16))*size)/16;
  float expected=level*512+layer*128+y*8+x+c*.125f;
  if(actual[4*i+c]!=expected){if(bad<4)fprintf(stderr,"pixel%u channel%u: %g expected %g\n",i,c,actual[4*i+c],expected);bad++;}
 }
 printf("%s: array varying LOD exact 128 pixels (%u mismatches)\n",bad?"FAIL":"PASS",bad);
 glDeleteProgram(program);glDeleteTextures(1,&tex);glDeleteTextures(1,&output);glDeleteFramebuffers(1,&fb);glDeleteVertexArrays(1,&vao);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,context);eglTerminate(d);return !!bad;
}
