/* SPDX-License-Identifier: MIT
 * Real draw/readback plus timestamp and elapsed queries on M4 Zink.
 * Execute only inside the mandatory J713 watchdog/thermal guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "FAIL: line %d: %s EGL=%x GL=%x\n", __LINE__, #x, \
           eglGetError(), glGetError()); exit(1); } } while (0)

static GLuint
compile(GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   GLint ok;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[4096];
      glGetShaderInfoLog(shader, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n", log);
   }
   CHECK(ok);
   return shader;
}

int
main(void)
{
   EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(display, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(context != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLint bits;
   glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
   CHECK(bits > 0);
   GLuint program = glCreateProgram();
   GLuint vs = compile(GL_VERTEX_SHADER,
      "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}");
   GLuint fs = compile(GL_FRAGMENT_SHADER,
      "#version 330 core\nout vec4 color;void main(){color=vec4(0,1,0,1);}");
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint linked;
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   CHECK(linked);
   glUseProgram(program);
   GLuint vao, texture, fbo, queries[4];
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 128, 128);
   glGenQueries(4, queries);
   unsigned char pixels[128 * 128 * 4];
   uint64_t previous_end = 0;
   unsigned bad_pixels = 0;
   for (unsigned pass = 0; pass < 8; pass++) {
      glClearColor(1, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glQueryCounter(queries[0], GL_TIMESTAMP);
      glBeginQuery(GL_TIME_ELAPSED, queries[3]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glQueryCounter(queries[1], GL_TIMESTAMP);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEndQuery(GL_TIME_ELAPSED);
      glQueryCounter(queries[2], GL_TIMESTAMP);
      glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned pass_bad = 0;
      for (unsigned i = 0; i < 128 * 128; i++)
         pass_bad += !(pixels[4*i] == 0 && pixels[4*i+1] == 255 &&
                       pixels[4*i+2] == 0 && pixels[4*i+3] == 255);
      bad_pixels += pass_bad;
      GLuint64 values[4];
      for (unsigned i = 0; i < 4; i++) {
         glGetQueryObjectui64v(queries[i], GL_QUERY_RESULT, &values[i]);
         CHECK(glGetError() == GL_NO_ERROR);
      }
      printf("pass %u timestamps=%" PRIu64 ",%" PRIu64 ",%" PRIu64 " elapsed=%" PRIu64 "\n",
             pass, (uint64_t)values[0], (uint64_t)values[1],
             (uint64_t)values[2], (uint64_t)values[3]);
      fflush(stdout);
      printf("pass %u pixels_bad=%u/16384\n", pass, pass_bad);
      CHECK(values[0] >= previous_end && values[0] > 0);
      CHECK(values[0] <= values[1] && values[1] <= values[2]);
      CHECK(values[2] != UINT64_MAX && values[3] != UINT64_MAX);
      previous_end = values[2];
   }
   glDeleteQueries(4, queries);
   glDeleteFramebuffers(1, &fbo);
   glDeleteTextures(1, &texture);
   glDeleteVertexArrays(1, &vao);
   glDeleteProgram(program);
   glDeleteShader(vs);
   glDeleteShader(fs);
   eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(display, context);
   eglTerminate(display);
   CHECK(bad_pixels == 0);
   puts("PASS: timestamp order, elapsed queries and 131072 GPU-rendered pixels");
   return 0;
}
