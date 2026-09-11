/* SPDX-License-Identifier: MIT
 * Exact visible/scissored occlusion counts and draw readback on M4 Zink.
 * Execute only inside the mandatory J713 watchdog/thermal guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#define CHECK(x)                                                               \
   do {                                                                        \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL: line %d: %s EGL=%x GL=%x\n", __LINE__, #x,     \
                 eglGetError(), glGetError());                                 \
         exit(1);                                                              \
      }                                                                        \
   } while (0)

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
   EGLDisplay display =
      eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(display, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION,
                           3,
                           EGL_CONTEXT_MINOR_VERSION,
                           3,
                           EGL_CONTEXT_OPENGL_PROFILE_MASK,
                           EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                           EGL_NONE};
   EGLContext context =
      eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(context != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLint bits;
   glGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &bits);
   CHECK(bits > 0);
   GLuint program = glCreateProgram();
   GLuint vs = compile(
      GL_VERTEX_SHADER,
      "#version 330 core\nuniform float z;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,z,1);}");
   GLuint fs = compile(
      GL_FRAGMENT_SHADER,
      "#version 330 core\nuniform vec4 draw_color;out vec4 color;void main(){color=draw_color;}");
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint linked;
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   CHECK(linked);
   glUseProgram(program);
   GLint color_loc = glGetUniformLocation(program, "draw_color");
   CHECK(color_loc >= 0);
   glUniform4f(color_loc, 0, 1, 0, 1);
   const int depth_test = getenv("M4_TEST_DEPTH_MASK") != NULL;
   GLint z_loc = glGetUniformLocation(program, "z");
   CHECK(z_loc >= 0);
   GLuint vao, texture, fbo, queries[4];
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          texture, 0);
   if (depth_test) {
      GLuint depth;
      glGenRenderbuffers(1, &depth);
      glBindRenderbuffer(GL_RENDERBUFFER, depth);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, 128, 128);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, depth);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LEQUAL);
   }
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 128, 128);
   glGenQueries(4, queries);
   unsigned char pixels[128 * 128 * 4];
   unsigned bad_pixels = 0;
   for (unsigned pass = 0; pass < 8; pass++) {
      glClearColor(1, 0, 0, 1);
      glDepthMask(GL_TRUE);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glUniform1f(z_loc, 0);
      glUniform4f(color_loc, 0, depth_test ? 0 : 1, depth_test ? 1 : 0, 1);
      glBeginQuery(GL_SAMPLES_PASSED, queries[0]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEndQuery(GL_SAMPLES_PASSED);
      glEnable(GL_SCISSOR_TEST);
      glScissor(17, 23, 11, 7);
      glBeginQuery(GL_SAMPLES_PASSED, queries[1]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEndQuery(GL_SAMPLES_PASSED);
      glScissor(0, 0, 0, 0);
      glBeginQuery(GL_SAMPLES_PASSED, queries[2]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEndQuery(GL_SAMPLES_PASSED);
      glDisable(GL_SCISSOR_TEST);
      glBeginQuery(GL_ANY_SAMPLES_PASSED, queries[3]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEndQuery(GL_ANY_SAMPLES_PASSED);
      /* Visibility bounding boxes must count samples without painting over
       * previously rendered color. Exercise the exact game state transition. */
      glUniform4f(color_loc, 1, 0, 0, 1);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glDepthMask(GL_FALSE);
      glUniform1f(z_loc, -.5f);
      glBeginQuery(GL_SAMPLES_PASSED, queries[0]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glEndQuery(GL_SAMPLES_PASSED);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glUniform4f(color_loc, 0, 1, 0, 1);
      glDepthMask(GL_TRUE);
      if (depth_test) {
         glUniform1f(z_loc, -.25f);
         glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned pass_bad = 0;
      for (unsigned i = 0; i < 128 * 128; i++)
         pass_bad += !(pixels[4 * i] == 0 && pixels[4 * i + 1] == 255 &&
                       pixels[4 * i + 2] == 0 && pixels[4 * i + 3] == 255);
      bad_pixels += pass_bad;
      GLuint64 values[4];
      for (unsigned i = 0; i < 4; i++) {
         glGetQueryObjectui64v(queries[i], GL_QUERY_RESULT, &values[i]);
         CHECK(glGetError() == GL_NO_ERROR);
      }
      printf("pass%u samples=%" PRIu64 ",%" PRIu64 ",%" PRIu64 " any=%" PRIu64
             " pixels_bad=%u\n",
             pass, (uint64_t)values[0], (uint64_t)values[1],
             (uint64_t)values[2], (uint64_t)values[3], pass_bad);
      fflush(stdout);
      CHECK(values[0] == 16384 && values[1] == 77 && values[2] == 0 &&
            values[3] == 1);
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
   puts("PASS: masked occlusion queries preserve 131072 GPU-rendered pixels");
   return 0;
}
