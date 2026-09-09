/* SPDX-License-Identifier: MIT
 * Mip selection and derivatives across triangle boundaries. Run under the
 * J713 watchdog/thermal guard; --reference requires host llvmpipe.
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
static GLuint
shader(GLenum stage, const char *src)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &src, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[4096];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n", log);
   }
   CHECK(ok);
   return s;
}
int
main(int argc, char **argv)
{
   bool reference = false, explicit_only = false;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--reference"))
         reference = true;
      else if (!strcmp(argv[i], "--explicit-only"))
         explicit_only = true;
      else
         CHECK(false);
   }
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_ES_API));
   const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *r = (const char *)glGetString(GL_RENDERER);
   CHECK(r);
   fprintf(stderr, "renderer: %s\n", r);
   CHECK(reference
            ? strstr(r, "llvmpipe") != NULL
            : (strstr(r, "zink") && strstr(r, "M4") && !strstr(r, "llvmpipe")));
   GLuint vao;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   GLuint tex;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   for (unsigned level = 0; level < 9; level++) {
      unsigned n = 256 >> level;
      uint8_t *pixels = malloc(n * n * 4);
      CHECK(pixels);
      for (unsigned i = 0; i < n * n; i++) {
         pixels[i * 4] = level * 25;
         pixels[i * 4 + 1] = 255 - level * 25;
         pixels[i * 4 + 2] = level * 10;
         pixels[i * 4 + 3] = 255;
      }
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, n, n, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, pixels);
      free(pixels);
   }
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                   GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   GLuint fbo, rb;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glGenRenderbuffers(1, &rb);
   glBindRenderbuffer(GL_RENDERBUFFER, rb);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, rb);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 64, 64);
   glDisable(GL_DITHER);
   const char *vs =
      "#version 300 es\nprecision highp float;uniform float scale;out vec2 uv;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4((2.0*p-1.0)*scale,0,1);}";
   unsigned failures = 0, total = 0;
   for (unsigned mode = 0; mode < (explicit_only ? 9u : 13u); mode++) {
      char fs[1024], expr[256];
      bool small = mode == 10 || mode == 12, derivative = mode >= 11;
      if (mode < 9)
         snprintf(expr, sizeof(expr), "textureLod(t,uv,%u.0)", mode);
      else if (!derivative)
         snprintf(expr, sizeof(expr), "texture(t,uv)");
      else
         snprintf(
            expr, sizeof(expr),
            "vec4(abs(dFdx(uv.x))*8.0,abs(dFdy(uv.y))*8.0,abs(dFdy(uv.x))*8.0,1)");
      snprintf(
         fs, sizeof(fs),
         "#version 300 es\nprecision highp float;precision highp sampler2D;uniform sampler2D t;in vec2 uv;out vec4 o;void main(){o=%s;}",
         expr);
      GLuint p = glCreateProgram(), v = shader(GL_VERTEX_SHADER, vs),
             f = shader(GL_FRAGMENT_SHADER, fs);
      glAttachShader(p, v);
      glAttachShader(p, f);
      glLinkProgram(p);
      GLint ok;
      glGetProgramiv(p, GL_LINK_STATUS, &ok);
      CHECK(ok);
      glUseProgram(p);
      glUniform1i(glGetUniformLocation(p, "t"), 0);
      glUniform1f(glGetUniformLocation(p, "scale"), small ? .25f : 1.f);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      uint8_t pixels[64 * 64 * 4];
      glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned checked = 0, bad = 0;
      unsigned level = mode < 9 ? mode : small ? 4 : 2;
      int expected[3] = {level * 25, 255 - level * 25, level * 10};
      if (derivative) {
         expected[0] = expected[1] = small ? 128 : 32;
         expected[2] = 0;
      }
      for (unsigned i = 0; i < 64 * 64; i++) {
         if (!pixels[4 * i + 3])
            continue;
         checked++;
         bool wrong = false;
         for (unsigned k = 0; k < 3; k++)
            wrong |= abs((int)pixels[4 * i + k] - expected[k]) > 2;
         if (wrong) {
            if (bad < 3)
               fprintf(stderr,
                       "mode %u pixel %u,%u got %u,%u,%u expected %d,%d,%d\n",
                       mode, i % 64, i / 64, pixels[4 * i], pixels[4 * i + 1],
                       pixels[4 * i + 2], expected[0], expected[1],
                       expected[2]);
            bad++;
         }
      }
      /* Pixel centers on the diagonal belong to the top-left primitive edge. */
      if (checked != (small ? 496u : 4096u)) {
         fprintf(stderr, "mode %u unexpected coverage %u\n", mode, checked);
         bad++;
      }
      printf("mode %u: %u pixels, %u failures\n", mode, checked, bad);
      failures += bad;
      total += checked;
      glDeleteProgram(p);
      glDeleteShader(v);
      glDeleteShader(f);
   }
   printf("mipmaps: %u pixels checked, %u failures\n", total, failures);
   glDeleteRenderbuffers(1, &rb);
   glDeleteFramebuffers(1, &fbo);
   glDeleteTextures(1, &tex);
   glDeleteVertexArrays(1, &vao);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d, c) && eglTerminate(d));
   return failures ? 1 : 0;
}
