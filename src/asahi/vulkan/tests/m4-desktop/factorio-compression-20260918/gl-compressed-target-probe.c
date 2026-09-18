/* SPDX-License-Identifier: MIT
 * CPU-checked ordinary/custom-border sampler executable rebinds; use the hardware experiment guard.
 * cc -O2 gl-plain-sampler-probe.c -lEGL -lGL -o gl-plain-sampler-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); exit(1); } } while (0)
static GLuint shader(GLenum type, const char *text) {
   GLuint s = glCreateShader(type); glShaderSource(s, 1, &text, NULL);
   glCompileShader(s); GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log); fputs(log, stderr); }
   CHECK(ok); return s;
}
int main(void) {
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));

   GLuint p = glCreateProgram();
   glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 330 core\nlayout(std140) uniform Values {uvec4 value;};uniform sampler2D image;out vec4 color;void main(){color=texture(image,vec2(-0.5))*vec4(value)/255.0;}"));
   glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);
   glUseProgram(p);glUniformBlockBinding(p,glGetUniformBlockIndex(p,"Values"),0);
   GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_UNIFORM_BUFFER,buffer);
   glBufferData(GL_UNIFORM_BUFFER,16,NULL,GL_DYNAMIC_DRAW);
   glBindBufferBase(GL_UNIFORM_BUFFER,0,buffer);
   GLuint input,samplers[2];glGenTextures(1,&input);glBindTexture(GL_TEXTURE_2D,input);
   unsigned char texel[4]={64,128,192,255};
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texel);
   glGenSamplers(2,samplers);
   for(unsigned i=0;i<2;++i){
      glSamplerParameteri(samplers[i],GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glSamplerParameteri(samplers[i],GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_S,i?GL_CLAMP_TO_BORDER:GL_CLAMP_TO_EDGE);
      glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_T,i?GL_CLAMP_TO_BORDER:GL_CLAMP_TO_EDGE);
      float border[4]={1,0.25,0,1};glSamplerParameterfv(samplers[i],GL_TEXTURE_BORDER_COLOR,border);
   }
   GLuint vao,tex,fb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
   glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,256,256);
   glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,256,256);
   glBindTexture(GL_TEXTURE_2D,input);
   unsigned char pixels[256*256*4];unsigned bad=0;
   for(unsigned round=0;round<4;++round){
      unsigned variant=(round/2)&1;
      unsigned value[4]={255,255,255,255};
      glBindBuffer(GL_UNIFORM_BUFFER,buffer);
      glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(value),value);
      glBindSampler(0,samplers[variant]);
      glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      CHECK(glGetError()==GL_NO_ERROR);
      unsigned char expected[2][4]={{64,128,192,255},{255,64,0,255}};
      for(unsigned j=0;j<256*256;++j)for(unsigned c=0;c<4;++c)
         bad+=abs((int)pixels[j*4+c]-expected[variant][c])>1;
      printf("round=%u custom_border=%u bad=%u\n",round,variant,bad);fflush(stdout);
   }
   CHECK(!bad);puts("PASS: compressed render target writes and CPU readback");
   return 0;
}
