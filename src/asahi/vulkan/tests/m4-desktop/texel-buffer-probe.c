/* SPDX-License-Identifier: MIT
 * Texel-buffer size, R8/R32/RGB32 fetch and OOB oracle. Guard hardware runs.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL: line %d %s GL=%x\n",__LINE__,#x,glGetError());exit(1);} }while(0)
static GLuint shader(GLenum type,const char *text){GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}CHECK(ok);return s;}
int main(void){
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);CHECK(eglInitialize(d,NULL,NULL));CHECK(eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);CHECK(c!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint p=glCreateProgram();glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
 glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 330 core\nuniform samplerBuffer tex;uniform int phase;out vec4 color;void main(){int i=int(gl_FragCoord.x)+64*int(gl_FragCoord.y);if(phase==0)color=vec4(.25,0,0,1);else if(phase==1)color=textureSize(tex)==1500?vec4(0,1,0,1):vec4(1,0,0,1);else color=vec4(texelFetch(tex,i).rgb,1);}"));
 glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);
 GLuint vao,fb,rt,tex,buf;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(1,&rt);glBindTexture(GL_TEXTURE_2D,rt);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,64,32);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,rt,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,64,32);
 glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_BUFFER,tex);glGenBuffers(1,&buf);glBindBuffer(GL_TEXTURE_BUFFER,buf);
 const GLenum formats[]={GL_R8,GL_R32F,GL_RGB32F};unsigned failures=0;
 for(unsigned test=0;test<3;++test){unsigned char bytes[1500];float values[1500*3];unsigned channels=test==2?3:1;
  for(unsigned i=0;i<1500;++i){bytes[i]=1+(i*37)%254;for(unsigned k=0;k<channels;++k)values[i*channels+k]=(1+(i*37+k*53)%254)/255.f;}
  glBufferData(GL_TEXTURE_BUFFER,test==0?sizeof(bytes):1500*channels*sizeof(float),test==0?(void*)bytes:(void*)values,GL_STATIC_DRAW);glTexBuffer(GL_TEXTURE_BUFFER,formats[test],buf);CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned phase=0;phase<3;++phase){glUniform1i(glGetUniformLocation(p,"phase"),phase);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);unsigned char pixels[2048*4];glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);unsigned bad=0;
   for(unsigned i=0;i<2048;++i)for(unsigned k=0;k<4;++k){int expected=k==3?255:phase==0?(k==0?64:0):phase==1?(k==1?255:0):(i<1500&&k<channels?1+(i*37+k*53)%254:0);if(abs((int)pixels[4*i+k]-expected)>1){if(bad<3)fprintf(stderr,"case=%u phase=%u pixel=%u c=%u got=%u expected=%d\n",test,phase,i,k,pixels[4*i+k],expected);++bad;}}
   printf("case=%u phase=%u bad=%u\n",test,phase,bad);fflush(stdout);failures+=bad;
  }
 }
 printf("%s: texel buffer size/fetch oracle (%u mismatches)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
