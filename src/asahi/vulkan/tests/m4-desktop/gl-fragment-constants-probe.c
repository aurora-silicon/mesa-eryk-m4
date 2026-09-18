/* SPDX-License-Identifier: MIT
 * CPU-checked fragment constant arrays and shader rebinds; use the hardware experiment guard.
 * cc -O2 gl-fragment-constants-probe.c -lEGL -lGL -o gl-fragment-constants-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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

   GLuint programs[2];
   for (unsigned variant = 0; variant < 2; ++variant) {
      char source[20000]; size_t used = snprintf(source,sizeof(source),
         "#version 330 core\nuniform int shift;out vec4 color;const uvec4 lut[256]=uvec4[256](");
      for (unsigned i=0;i<256;++i)
         used += snprintf(source+used,sizeof(source)-used,"%suvec4(%u,%u,%u,255)",
            i ? "," : "",(i*73+19+variant*17)&255,(i*41+7+variant*31)&255,(i*137+23+variant*47)&255);
      snprintf(source+used,sizeof(source)-used,
         ");void main(){int i=(int(gl_FragCoord.x)+shift)&255;color=vec4(lut[i])/255.0;}");
      GLuint p = programs[variant] = glCreateProgram();
      glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
      glAttachShader(p,shader(GL_FRAGMENT_SHADER,source));
      glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);
   }
   GLuint vao,tex,fb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
   glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,256,4);
   unsigned char pixels[256*4*4];unsigned bad=0;
   for(unsigned round=0;round<8;++round){
      unsigned variant=(round/2)&1,shift=round*43;
      glUseProgram(programs[variant]);
      glUniform1i(glGetUniformLocation(programs[variant],"shift"),shift);
      glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,256,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      CHECK(glGetError()==GL_NO_ERROR);
      for(unsigned y=0;y<4;++y)for(unsigned x=0;x<256;++x){
         unsigned i=(x+shift)&255;
         unsigned char expected[4]={(i*73+19+variant*17)&255,(i*41+7+variant*31)&255,(i*137+23+variant*47)&255,255};
         for(unsigned c=0;c<4;++c)bad+=pixels[(y*256+x)*4+c]!=expected[c];
      }
      printf("round=%u variant=%u bad=%u\n",round,variant,bad);fflush(stdout);
   }
   CHECK(!bad);puts("PASS: fragment constant arrays, shader rebinds and dynamic indexing");
   return 0;
}
