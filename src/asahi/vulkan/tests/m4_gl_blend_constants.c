/* SPDX-License-Identifier: MIT
 * Check the 0 A.D. silhouette depth/stencil sequence and constant-colour
 * blending with live texture heaps. Run with the mandatory J713 watchdog
 * and thermal guards; M4_TEST_LLVMPIPE allows the software reference.
 */
#include <stdbool.h>
#include <stdint.h>
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
         exit(1);                                                              \
      }                                                                        \
   } while (0)
int
main(int argc, char **argv)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_ES_API));
   EGLint a[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, a);
   CHECK(c != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *r = (const char *)glGetString(GL_RENDERER);
   fprintf(stderr, "Renderer: %s\n", r);
   CHECK((strstr(r, "zink") && strstr(r, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(r, "llvmpipe")));

   const char *vs =
      "#version 300 es\nuniform float z;void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,z,1);}";
   const char *fs =
      "#version 300 es\nprecision highp float;uniform vec4 paint;out vec4 color;void main(){color=paint;}";
   GLuint shaders[2] = {glCreateShader(GL_VERTEX_SHADER),
                        glCreateShader(GL_FRAGMENT_SHADER)};
   const char *sources[2] = {vs, fs};
   GLuint prog = glCreateProgram();
   for (unsigned i = 0; i < 2; i++) {
      glShaderSource(shaders[i], 1, &sources[i], NULL);
      glCompileShader(shaders[i]);
      GLint ok;
      glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok);
      CHECK(ok);
      glAttachShader(prog, shaders[i]);
   }
   glLinkProgram(prog);
   GLint ok;
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(prog);
   GLuint tex, fb;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 16, 16);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          tex, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 16, 16);
   GLuint depth;
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 16, 16);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_RENDERBUFFER, depth);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

   GLint paint = glGetUniformLocation(prog, "paint"),
         z = glGetUniformLocation(prog, "z");
   CHECK(paint >= 0 && z >= 0);
   unsigned total = 0;
   for (unsigned mode = 0; mode < 8; mode++) {
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_BLEND);
      glDisable(GL_SCISSOR_TEST);
      glColorMask(1, 1, 1, 1);
      glDepthMask(1);
      glStencilMask(255);
      glClearColor(0, 0, 0, 1);
      glClearDepthf(1);
      glClearStencil(0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
              GL_STENCIL_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LESS);
      glColorMask(0, 0, 0, 0);
      glUniform1f(z, 0);
      glEnable(GL_SCISSOR_TEST);
      glScissor(0, 0, 8, 16);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glDisable(GL_SCISSOR_TEST);
      glColorMask(1, 1, 1, 1);
      glDepthFunc(GL_GEQUAL);
      glEnable(GL_STENCIL_TEST);
      glStencilFunc(GL_NOTEQUAL, 1, 255);
      glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
      glEnable(GL_BLEND);
      glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
      glBlendColor(0, 0, 0, 0.75f);
      if (mode == 3 || mode == 6)
         glDisable(GL_STENCIL_TEST);
      if (mode == 4 || mode == 6)
         glDisable(GL_BLEND);
      if (mode == 5)
         glDepthFunc(GL_ALWAYS);
      if (mode == 7)
         glDepthFunc(GL_LESS);
      /* Front geometry fails GEQUAL and must not mark the stencil. */
      if (mode == 1 || mode == 2) {
         glUniform4f(paint, 1, 0, 0, 1);
         glUniform1f(z, -0.5f);
         glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      glUniform4f(paint, 0, 0, 1, 1);
      glUniform1f(z, mode == 7 ? -0.5f : 0.5f);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      /* Already drawn pixels fail NOTEQUAL and must not blend a second time. */
      if (mode == 2) {
         glUniform4f(paint, 0, 1, 0, 1);
         glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      unsigned char pixels[16 * 16 * 4];
      glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned bad = 0;
      for (unsigned i = 0; i < 256; i++) {
         const unsigned char *p = pixels + i * 4;
         unsigned want = (i % 16 < 8 || mode == 5 || mode == 7)
                            ? ((mode == 4 || mode == 6) ? 255 : 191)
                            : 0;
         if (p[0] > 1 || p[1] > 1 || abs((int)p[2] - (int)want) > 1 ||
             p[3] != 255) {
            if (bad < 3)
               printf("mode %u pixel %u: %u %u %u %u expected blue %u\n", mode,
                      i, p[0], p[1], p[2], p[3], want);
            bad++;
         }
      }
      printf("mode=%u bad=%u/256\n", mode, bad);
      total += bad;
   }

   /* Both blend factors and texture heaps must survive each other's uploads.
    * All channels differ, and constants change without changing the shaders.
    */
   const char *texture_fs =
      "#version 300 es\nprecision highp float;uniform sampler2D tex;out vec4 color;void main(){color=texture(tex,gl_FragCoord.xy/16.0);}";
   GLuint sh = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(sh, 1, &texture_fs, NULL);
   glCompileShader(sh);
   glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
   CHECK(ok);
   glDetachShader(prog, shaders[1]);
   glAttachShader(prog, sh);
   glLinkProgram(prog);
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(prog);
   glUniform1f(glGetUniformLocation(prog, "z"), 0);
   glUniform1i(glGetUniformLocation(prog, "tex"), 0);
   GLuint source;
   glGenTextures(1, &source);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, source);
   const unsigned char src[] = {64, 128, 192, 128}, dst[] = {16, 32, 48, 192};
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                   src);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_STENCIL_TEST);
   glDisable(GL_SCISSOR_TEST);
   glColorMask(1, 1, 1, 1);
   const float constants[][4] = {{.25f, .5f, .75f, .625f},
                                 {.875f, .125f, .375f, .25f},
                                 {0, 0, 0, 0},
                                 {1, 1, 1, 1}};
   for (unsigned alpha = 0; alpha < 2; alpha++)
      for (unsigned c = 0; c < 4; c++) {
         glDisable(GL_BLEND);
         glClearColor(dst[0] / 255.0f, dst[1] / 255.0f, dst[2] / 255.0f,
                      dst[3] / 255.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         glEnable(GL_BLEND);
         glBlendColor(constants[c][0], constants[c][1], constants[c][2],
                      constants[c][3]);
         glBlendFuncSeparate(
            alpha ? GL_CONSTANT_ALPHA : GL_CONSTANT_COLOR,
            alpha ? GL_ONE_MINUS_CONSTANT_ALPHA : GL_ONE_MINUS_CONSTANT_COLOR,
            GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         unsigned char pixels[16 * 16 * 4];
         glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         CHECK(glGetError() == GL_NO_ERROR);
         unsigned bad = 0;
         for (unsigned i = 0; i < 256; i++) {
            bool wrong = false;
            for (unsigned k = 0; k < 4; k++) {
               float f = constants[c][alpha ? 3 : k];
               int want = (int)(src[k] * f + dst[k] * (1 - f) + .5f);
               wrong |= abs((int)pixels[4 * i + k] - want) > 2;
            }
            if (wrong) {
               if (bad < 2)
                  printf(
                     "textured alpha=%u constants=%u pixel=%u got=%u,%u,%u,%u\n",
                     alpha, c, i, pixels[4 * i], pixels[4 * i + 1],
                     pixels[4 * i + 2], pixels[4 * i + 3]);
               bad++;
            }
         }
         printf("textured alpha=%u constants=%u bad=%u/256\n", alpha, c, bad);
         total += bad;
      }
   printf("%s: constant blending, %u incorrect pixels\n",
          total ? "FAIL" : "PASS", total);
   return total ? 1 : 0;
}
