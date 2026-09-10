/* SPDX-License-Identifier: MIT
 * Exercise the 32-scalar native output boundary with real GPU interpolation.
 * Independent scalar vertex attributes prevent folding the output interface.
 * Run M4 only under the mandatory watchdog and thermal guards.
 */
#define GL_GLEXT_PROTOTYPES
#include <math.h>
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
         fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x,           \
                 eglGetError(), glGetError());                                 \
         exit(1);                                                              \
      }                                                                        \
   } while (0)
#define W 128
static GLuint
shader(GLenum stage, const char *source)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[8192];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n%s\n", source, log);
   }
   CHECK(ok);
   return s;
}
int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_API));
   EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION,
                     3,
                     EGL_CONTEXT_MINOR_VERSION,
                     3,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK,
                     EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
                     EGL_NONE};
   EGLContext ctx =
      eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(ctx != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
   const char *renderer = (void *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   fflush(stdout);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLuint fb, target, vbo;
   glGenTextures(1, &target);
   glBindTexture(GL_TEXTURE_2D, target);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, W);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          target, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   /* Each vec4 attribute supplies four separately interpolated components. */
   float vertices[6][36] = {0};
   const float pos[6][2] = {{-1, -1}, {1, -1}, {-1, 1},
                            {1, -1},  {1, 1},  {-1, 1}};
   for (unsigned v = 0; v < 6; v++) {
      vertices[v][0] = pos[v][0];
      vertices[v][1] = pos[v][1];
      for (unsigned i = 0; i < 32; i++)
         vertices[v][4 + i] = (i + 1) / 64.0f +
                              pos[v][0] * (i % 3 + 1) / 128.0f +
                              pos[v][1] * (i % 5 + 1) / 256.0f;
   }
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   for (unsigned i = 0; i < 9; i++) {
      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(vertices[0]),
                            (void *)(size_t)(i * 4 * sizeof(float)));
      glEnableVertexAttribArray(i);
   }
   glViewport(0, 0, W, W);
   unsigned total_bad = 0;
   const unsigned counts[] = {28, 29, 32};
   for (unsigned test = 0; test < 3; test++) {
      unsigned n = counts[test];
      char vs[16384] = "#version 330\nlayout(location=0) in vec4 pos;\n",
           fs[16384] = "#version 330\nout vec4 color;\n";
      size_t vo = strlen(vs), fo = strlen(fs);
      for (unsigned i = 0; i < 8; i++)
         vo += snprintf(vs + vo, sizeof(vs) - vo,
                        "layout(location=%u) in vec4 a%u;\n", i + 1, i);
      for (unsigned i = 0; i < n; i++) {
         vo += snprintf(vs + vo, sizeof(vs) - vo, "out float v%u;\n", i);
         fo += snprintf(fs + fo, sizeof(fs) - fo, "in float v%u;\n", i);
      }
      vo += snprintf(vs + vo, sizeof(vs) - vo,
                     "void main(){gl_Position=vec4(pos.xy,0,1);\n");
      fo += snprintf(fs + fo, sizeof(fs) - fo, "void main(){float s=0;\n");
      for (unsigned i = 0; i < n; i++) {
         vo += snprintf(vs + vo, sizeof(vs) - vo, "v%u=a%u.%c;\n", i, i / 4,
                        "xyzw"[i % 4]);
         fo += snprintf(fs + fo, sizeof(fs) - fo, "s+=v%u*%.9f;\n", i,
                        (double)(i % 7 + 1) / (n * 4));
      }
      snprintf(vs + vo, sizeof(vs) - vo, "}\n");
      snprintf(fs + fo, sizeof(fs) - fo, "color=vec4(s,s*0.5,1.0-s,1);}\n");
      GLuint p = glCreateProgram(), v = shader(GL_VERTEX_SHADER, vs),
             f = shader(GL_FRAGMENT_SHADER, fs);
      glAttachShader(p, v);
      glAttachShader(p, f);
      glLinkProgram(p);
      GLint ok;
      glGetProgramiv(p, GL_LINK_STATUS, &ok);
      CHECK(ok);
      glDeleteShader(v);
      glDeleteShader(f);
      glUseProgram(p);
      printf("varyings=%u native_position_plus_varyings=%u\n", n, n + 4);
      fflush(stdout);
      /* Multiple retirements expose stale coefficient/output state as well as
       * a single draw's values. Readback verifies every iteration. */
      unsigned bad = 0, max_error = 0;
      for (unsigned repeat = 0; repeat < 24; repeat++) {
         glClearColor(1, 0, 1, 1);
         glClear(GL_COLOR_BUFFER_BIT);
         glDrawArrays(GL_TRIANGLES, 0, 6);
         unsigned before_bad = bad;
         unsigned char pixels[W * W * 4];
         memset(pixels, 0x5a, sizeof(pixels));
         glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         CHECK(glGetError() == GL_NO_ERROR);
         for (unsigned y = 0; y < W; y++)
            for (unsigned x = 0; x < W; x++) {
               double px = (x + .5) * 2 / W - 1, py = (y + .5) * 2 / W - 1,
                      s = 0;
               for (unsigned i = 0; i < n; i++)
                  s += ((i + 1) / 64.0 + px * (i % 3 + 1) / 128.0 +
                        py * (i % 5 + 1) / 256.0) *
                       (i % 7 + 1) / (n * 4.0);
               int expected[4] = {(int)lround(s * 255), (int)lround(s * 127.5),
                                  (int)lround((1 - s) * 255), 255};
               unsigned e = 0;
               for (unsigned c = 0; c < 4; c++) {
                  unsigned q =
                     abs((int)pixels[(y * W + x) * 4 + c] - expected[c]);
                  if (q > e)
                     e = q;
               }
               if (e > max_error)
                  max_error = e;
               if (e > 1)
                  bad++;
            }
         if (bad != before_bad)
            printf("repeat=%u bad=%u first_rgba=%u,%u,%u,%u\n", repeat,
                   bad - before_bad, pixels[0], pixels[1], pixels[2],
                   pixels[3]);
      }
      printf("varyings=%u bad=%u max_error=%u pixels=%u\n", n, bad, max_error,
             24 * W * W);
      fflush(stdout);
      total_bad += bad;
      glDeleteProgram(p);
   }
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, ctx);
   eglTerminate(d);
   printf("%s: wide varyings bad=%u\n", total_bad ? "FAIL" : "PASS", total_bad);
   return total_bad ? 1 : 0;
}
