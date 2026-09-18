/* SPDX-License-Identifier: MIT
 * CPU-checked fragment statistics and timing; use the hardware experiment guard.
 * cc -O2 gl-fragment-statistics-bench.c -lEGL -lGL -o gl-fragment-statistics-bench
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
int main(void) {
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));
   GLuint p = glCreateProgram();
   glAttachShader(p, shader(GL_VERTEX_SHADER, "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   glAttachShader(p, shader(GL_FRAGMENT_SHADER, "#version 330 core\nout vec4 color;void main(){color=vec4(1,0,1,1);}"));
   glLinkProgram(p); GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); CHECK(ok); glUseProgram(p);
   GLuint vao, tex, fb, stats, timer;
   glGenVertexArrays(1, &vao); glBindVertexArray(vao);
   glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2560, 1664, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glGenQueries(1, &stats); glGenQueries(1, &timer);
   const int sizes[][2] = {{2560,1664},{2557,1661},{17,13},{1,1}};
   for (unsigned k = 0; k < 4; k++) for (unsigned enabled = 0; enabled < 2; enabled++) {
      int w = sizes[k][0], h = sizes[k][1];
      glViewport(0, 0, w, h); glEnable(GL_SCISSOR_TEST); glScissor(0, 0, w, h);
      for (unsigned round = 0; round < 5; round++) {
         glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
         double start = now();
         if (enabled) glBeginQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB, stats);
         glBeginQuery(GL_TIME_ELAPSED, timer); glDrawArrays(GL_TRIANGLES, 0, 3);
         unsigned draws = 1;
         if (enabled && round == 4) {
            /* Keep one GL statistics query alive across two Vulkan batches. */
            glFlush();
            glDrawArrays(GL_TRIANGLES, 0, 3);
            ++draws;
         }
         glEndQuery(GL_TIME_ELAPSED);
         if (enabled) glEndQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB);
         GLuint64 count = 0, ns = 0;
         glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &ns);
         if (enabled && round == 3) {
            GLuint qbo;
            glGenBuffers(1, &qbo); glBindBuffer(GL_QUERY_BUFFER, qbo);
            glBufferData(GL_QUERY_BUFFER, sizeof(count), NULL, GL_STREAM_READ);
            glGetQueryObjectui64v(stats, GL_QUERY_RESULT, 0);
            glGetBufferSubData(GL_QUERY_BUFFER, 0, sizeof(count), &count);
            glBindBuffer(GL_QUERY_BUFFER, 0); glDeleteBuffers(1, &qbo);
         } else if (enabled) glGetQueryObjectui64v(stats, GL_QUERY_RESULT, &count);
         double elapsed = (now() - start) * 1000;
         CHECK(glGetError() == GL_NO_ERROR);
         /* Hardware may count helpers; this implementation counts covered samples.
          * Require the exact retained semantics, including partial edge quads. */
         if (enabled) CHECK(count == (GLuint64)w * h * draws);
         unsigned char pixel[4];
         glReadPixels(w-1,h-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
         CHECK(pixel[0]==255 && pixel[1]==0 && pixel[2]==255 && pixel[3]==255);
         printf("{\"width\":%d,\"height\":%d,\"stats\":%u,\"round\":%u,\"count\":%llu,\"gpu_ms\":%.6f,\"wall_ms\":%.6f}\n",w,h,enabled,round,(unsigned long long)count,ns/1e6,elapsed);
         fflush(stdout);
      }
   }
   puts("PASS: fragment statistics exact counts and edge pixels");
   return 0;
}
