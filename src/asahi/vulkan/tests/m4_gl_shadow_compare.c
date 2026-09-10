/* SPDX-License-Identifier: MIT
 * Analytic 2D depth comparison/PCF regression. No game assets.
 * Live J713 execution requires watchdog and thermal guards.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#define CHECK(x)                                                               \
   do {                                                                        \
      if (!(x)) {                                                              \
         fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x,           \
                 eglGetError(), glGetError());                                 \
         return 1;                                                             \
      }                                                                        \
   } while (0)
static float
compare(unsigned c, float r, float d)
{
   switch (c) {
   case 0:
      return 0;
   case 1:
      return r < d;
   case 2:
      return r == d;
   case 3:
      return r <= d;
   case 4:
      return r > d;
   case 5:
      return r != d;
   case 6:
      return r >= d;
   default:
      return 1;
   }
}
static int
edge(int i)
{
   return i < 0 ? 0 : i > 3 ? 3 : i;
}
int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_ES_API));
   EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
   EGLContext ctx =
      eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(ctx != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   fflush(stdout);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   const char *sources[] = {
      "#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}",
      "#version 300 es\nprecision highp float;precision highp sampler2DShadow;uniform sampler2DShadow depthTex;uniform float reference;out vec4 color;void main(){vec2 uv=gl_FragCoord.xy/16.*1.5-.25;color=vec4(texture(depthTex,vec3(uv,reference)));}"};
   GLuint prog = glCreateProgram();
   GLenum kinds[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
   for (unsigned i = 0; i < 2; i++) {
      GLuint sh = glCreateShader(kinds[i]);
      glShaderSource(sh, 1, &sources[i], NULL);
      glCompileShader(sh);
      GLint ok;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (!ok) {
         char msg[4096];
         glGetShaderInfoLog(sh, sizeof(msg), NULL, msg);
         fputs(msg, stderr);
      }
      CHECK(ok);
      glAttachShader(prog, sh);
      glDeleteShader(sh);
   }
   glLinkProgram(prog);
   GLint ok;
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(prog);
   GLuint target, fb, depth;
   glGenTextures(1, &target);
   glBindTexture(GL_TEXTURE_2D, target);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 16, 16);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          target, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   float depths[16];
   for (unsigned y = 0; y < 4; y++)
      for (unsigned x = 0; x < 4; x++)
         depths[y * 4 + x] = ((x + 2 * y) % 5) * .25f;
   glGenTextures(1, &depth);
   glBindTexture(GL_TEXTURE_2D, depth);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 4, 4, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, depths);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                   GL_COMPARE_REF_TO_TEXTURE);
   glUniform1i(glGetUniformLocation(prog, "depthTex"), 0);
   GLint ref = glGetUniformLocation(prog, "reference");
   CHECK(ref >= 0);
   glViewport(0, 0, 16, 16);
   const GLenum funcs[] = {GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                           GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
   const float refs[] = {.125, .5, .875};
   unsigned bad = 0, total = 0, max_error = 0;
   for (unsigned filter = 0; filter < 2; filter++)
      for (unsigned c = 0; c < 8; c++)
         for (unsigned r = 0; r < 3; r++) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            filter ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            filter ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, funcs[c]);
            glUniform1f(ref, refs[r]);
            glClearColor(1, 0, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            unsigned char pixels[16 * 16 * 4];
            glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            CHECK(glGetError() == GL_NO_ERROR);
            unsigned wrong = 0;
            for (unsigned y = 0; y < 16; y++)
               for (unsigned x = 0; x < 16; x++) {
                  float u = ((x + .5f) / 16 * 1.5f - .25f) * 4,
                        v = ((y + .5f) / 16 * 1.5f - .25f) * 4, e;
                  if (!filter)
                     e = compare(c, refs[r],
                                 depths[edge(floorf(v)) * 4 + edge(floorf(u))]);
                  else {
                     u -= .5f;
                     v -= .5f;
                     int ix = floorf(u), iy = floorf(v);
                     float fx = u - ix, fy = v - iy;
                     e = 0;
                     for (unsigned dy = 0; dy < 2; dy++)
                        for (unsigned dx = 0; dx < 2; dx++)
                           e += compare(
                                   c, refs[r],
                                   depths[edge(iy + dy) * 4 + edge(ix + dx)]) *
                                (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                  }
                  unsigned err = 0;
                  for (unsigned k = 0; k < 4; k++) {
                     unsigned q = abs((int)pixels[(y * 16 + x) * 4 + k] -
                                      (int)lroundf(e * 255));
                     if (q > err)
                        err = q;
                  }
                  if (err > max_error)
                     max_error = err;
                  wrong += err > 2;
               }
            printf("filter=%u compare=%u ref=%g wrong=%u\n", filter, c, refs[r],
                   wrong);
            fflush(stdout);
            bad += wrong;
            total += 256;
         }
   printf("pixels=%u incorrect=%u max_error=%u\n", total, bad, max_error);
   return bad ? 1 : 0;
}
