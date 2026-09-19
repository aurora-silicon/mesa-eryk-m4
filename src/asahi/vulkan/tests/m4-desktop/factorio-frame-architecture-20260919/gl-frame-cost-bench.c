/* SPDX-License-Identifier: MIT
 * Basic solid/fetch/sample scaling with CPU-checked output; use the hardware guard.
 * cc -O2 gl-frame-cost-bench.c -lEGL -lGL -o gl-frame-cost-bench
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
static double now(void) {
   struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv) {
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));
   GLuint p = glCreateProgram();
   glAttachShader(p, shader(GL_VERTEX_SHADER, "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   unsigned mode = argc > 1 ? atoi(argv[1]) : 0;
   unsigned discard = argc > 2 ? atoi(argv[2]) : 0;
   CHECK(mode <= 2);
   CHECK(discard <= 1);
   const char *fs[] = {
      "#version 330 core\nout vec4 color;void main(){color=vec4(1,0,1,1);}",
      "#version 330 core\nuniform sampler2D source;out vec4 color;void main(){color=texelFetch(source,ivec2(gl_FragCoord.xy),0);}",
      "#version 330 core\nuniform sampler2D source;out vec4 color;void main(){color=texture(source,gl_FragCoord.xy/vec2(textureSize(source,0)));}"
   };
   glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs[mode]));
   glLinkProgram(p); GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); CHECK(ok); glUseProgram(p);
   GLuint vao, tex, fb, source, source_fb;
   glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenFramebuffers(1,&fb);glGenFramebuffers(1,&source_fb);
   glGenTextures(1,&tex);glGenTextures(1,&source);
   const unsigned sizes[][2]={{512,512},{1920,1080},{2560,1664},{3840,2160}};
   for(unsigned k=0;k<4;k++) {
      unsigned w=sizes[k][0],h=sizes[k][1];
      glBindTexture(GL_TEXTURE_2D,source);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER,source_fb);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
      CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
      glViewport(0,0,w,h);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
      glBindTexture(GL_TEXTURE_2D,tex);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
      glBindFramebuffer(GL_FRAMEBUFFER,fb);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
      CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
      glBindTexture(GL_TEXTURE_2D,source);
      for(unsigned round=0;round<6;round++) {
         glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
         if (discard) {
            const GLenum attachment = GL_COLOR_ATTACHMENT0;
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
         }
         double start=now();
         /* No query instrumentation that could alter the rendering workload.
          * Kernel pass timestamps supply GPU timings; this is CPU wall time. */
         glDrawArrays(GL_TRIANGLES,0,3);glFinish();
         double elapsed=(now()-start)*1000;
         unsigned char pixel[4];glReadPixels(w-1,h-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
         CHECK(glGetError()==GL_NO_ERROR);
         CHECK(pixel[0]==255&&pixel[1]==0&&pixel[2]==255&&pixel[3]==255);
         printf("FRAME_COST mode=%u discard=%u width=%u height=%u round=%u wall_ms=%.6f\n",mode,discard,w,h,round,elapsed);
         fflush(stdout);
      }
   }
   puts("PASS: basic frame cost and edge readback");
   eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
   eglDestroyContext(d,c);eglTerminate(d);
   return 0;
}
