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
   bool reference = false, explicit_only = false, array = false,
        immutable = false;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--reference"))
         reference = true;
      else if (!strcmp(argv[i], "--explicit-only"))
         explicit_only = true;
      else if (!strcmp(argv[i], "--array"))
         array = true;
      else if (!strcmp(argv[i], "--immutable"))
         immutable = true;
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
   const GLenum target = array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
   const unsigned layers = array ? 3 : 1;
   glBindTexture(target, tex);
   if (immutable) {
      if (array)
         glTexStorage3D(target, 9, GL_RGBA8, 256, 256, layers);
      else
         glTexStorage2D(target, 9, GL_RGBA8, 256, 256);
   }
   for (unsigned level = 0; level < 9; level++) {
      unsigned n = 256 >> level;
      uint8_t *pixels = malloc(n * n * 4 * layers);
      CHECK(pixels);
      for (unsigned i = 0; i < n * n * layers; i++) {
         pixels[i * 4] = level * 25;
         pixels[i * 4 + 1] = 255 - level * 25;
         pixels[i * 4 + 2] = level * 10 + (i / (n * n)) * 40;
         pixels[i * 4 + 3] = 255;
      }
      if (immutable) {
         if (array)
            glTexSubImage3D(target, level, 0, 0, 0, n, n, layers, GL_RGBA,
                            GL_UNSIGNED_BYTE, pixels);
         else
            glTexSubImage2D(target, level, 0, 0, n, n, GL_RGBA,
                            GL_UNSIGNED_BYTE, pixels);
      } else if (array)
         glTexImage3D(target, level, GL_RGBA8, n, n, layers, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, pixels);
      else
         glTexImage2D(target, level, GL_RGBA8, n, n, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, pixels);
      free(pixels);
   }
   glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
   for (unsigned layer = 0; layer < layers; layer++)
      for (unsigned mode = 0; mode < (explicit_only ? 9u : 15u); mode++) {
         char fs[1024], expr[256];
         bool small = mode == 10 || mode == 12,
              derivative = mode == 11 || mode == 12;
         const char *coord = array ? "vec3(uv,layer)" : "uv";
         if (mode < 9)
            snprintf(expr, sizeof(expr), "textureLod(t,%s,%u.0)", coord, mode);
         else if (mode == 14)
            snprintf(expr, sizeof(expr), "texelFetch(t,%s,0)",
                     array ? "ivec3(ivec2(gl_FragCoord.xy),int(layer))"
                           : "ivec2(gl_FragCoord.xy)");
         else if (!derivative)
            snprintf(expr, sizeof(expr), "texture(t,%s)", coord);
         else
            snprintf(
               expr, sizeof(expr),
               "vec4(abs(dFdx(uv.x))*8.0,abs(dFdy(uv.y))*8.0,abs(dFdy(uv.x))*8.0,1)");
         snprintf(
            fs, sizeof(fs),
            "#version 300 es\nprecision highp float;precision highp %s;uniform %s t;uniform float layer;in vec2 uv;out vec4 o;void main(){o=%s;}",
            array ? "sampler2DArray" : "sampler2D",
            array ? "sampler2DArray" : "sampler2D", expr);
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
         glUniform1f(glGetUniformLocation(p, "layer"), layer);
         glUniform1f(glGetUniformLocation(p, "scale"), small ? .25f : 1.f);
         glTexParameterf(target, GL_TEXTURE_MAX_LOD, mode >= 13 ? 0 : 1000);
         glClearColor(0, 0, 0, 0);
         glClear(GL_COLOR_BUFFER_BIT);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         uint8_t pixels[64 * 64 * 4];
         glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         CHECK(glGetError() == GL_NO_ERROR);
         unsigned checked = 0, bad = 0;
         unsigned level = mode < 9 ? mode : mode >= 13 ? 0 : small ? 4 : 2;
         int expected[3] = {level * 25, 255 - level * 25,
                            level * 10 + layer * 40};
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
                  fprintf(
                     stderr,
                     "mode %u pixel %u,%u got %u,%u,%u expected %d,%d,%d\n",
                     mode, i % 64, i / 64, pixels[4 * i], pixels[4 * i + 1],
                     pixels[4 * i + 2], expected[0], expected[1], expected[2]);
               bad++;
            }
         }
         /* Pixel centers on the diagonal belong to the top-left primitive edge.
          */
         if (checked != (small ? 496u : 4096u)) {
            fprintf(stderr, "mode %u unexpected coverage %u\n", mode, checked);
            bad++;
         }
         printf("layer %u mode %u: %u pixels, %u failures\n", layer, mode,
                checked, bad);
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
