/* SPDX-License-Identifier: MIT
 * S3TC uploads, mip tails and block addressing. Analytic pixel reference, no
 * CPU decompression in the tested path. Run hardware only under the J713 guard.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <GLES2/gl2ext.h>
#define CHECK(x)                                                               \
   do {                                                                        \
      if (!(x)) {                                                              \
         fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x,           \
                 eglGetError(), glGetError());                                 \
         exit(1);                                                              \
      }                                                                        \
   } while (0)

static GLuint
shader(GLenum stage, const char *source)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[16384];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n", log);
   }
   CHECK(ok);
   return s;
}

static unsigned
size_at(unsigned n, unsigned level)
{
   return (n >> level) ? (n >> level) : 1;
}

static void
expected(unsigned format, unsigned x, unsigned y, unsigned level,
         uint8_t rgba[4])
{
   format %= 4;
   unsigned color = (x + 3 * y + level) % 4;
   rgba[0] = color == 0 || color == 3 ? 255 : 0;
   rgba[1] = color == 1 || color == 3 ? 255 : 0;
   rgba[2] = color == 2 || color == 3 ? 255 : 0;
   rgba[3] = format >= 2 ? (((x + y + level) & 1) ? 85 : 170) : 255;
   if (format == 1 && color == 3)
      memset(rgba, 0, 4);
}

static void
encode_block(uint8_t *block, unsigned fmt, unsigned x, unsigned y,
             unsigned level)
{
   uint8_t rgba[4];
   expected(fmt, x, y, level, rgba);
   if ((fmt % 4) == 2)
      memset(block, (rgba[3] / 17) * 17, 8);
   if ((fmt % 4) == 3)
      block[0] = rgba[3];
   uint8_t *color = block + ((fmt % 4) >= 2 ? 8 : 0);
   uint16_t rgb565 =
      (rgba[0] ? 0xf800 : 0) | (rgba[1] ? 0x7e0 : 0) | (rgba[2] ? 0x1f : 0);
   color[0] = rgb565 & 255;
   color[1] = rgb565 >> 8;
   if ((fmt % 4) == 1 && rgba[3] == 0)
      memset(color + 4, 255, 4);
}

int
main(int argc, char **argv)
{
   bool reference = argc == 2 && !strcmp(argv[1], "--reference");
   CHECK(argc == 1 || reference);
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_ES_API));
   const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   CHECK(renderer);
   fprintf(stderr, "renderer: %s\n", renderer);
   CHECK(reference ? strstr(renderer, "llvmpipe") != NULL
                   : (strstr(renderer, "zink") && strstr(renderer, "M4") &&
                      !strstr(renderer, "llvmpipe")));

   GLuint vao, fbo, rb;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glGenRenderbuffers(1, &rb);
   glBindRenderbuffer(GL_RENDERBUFFER, rb);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 128, 128);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, rb);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 128, 128);
   glDisable(GL_DITHER);
   GLuint p = glCreateProgram();
   GLuint v =
      shader(GL_VERTEX_SHADER,
             "#version 300 es\nprecision highp float;out vec2 uv;void main(){"
             "vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
             "uv=p;gl_Position=vec4(2.0*p-1.0,0,1);}");
   GLuint f =
      shader(GL_FRAGMENT_SHADER,
             "#version 300 es\nprecision highp float;uniform highp sampler2D t;"
             "uniform float mip;in vec2 uv;out vec4 o;"
             "void main(){o=textureLod(t,uv,mip);}");
   glAttachShader(p, v);
   glAttachShader(p, f);
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(p);
   glUniform1i(glGetUniformLocation(p, "t"), 0);
   const GLenum formats[] = {
      GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
      GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
      GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
      GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
      GL_COMPRESSED_SRGB_S3TC_DXT1_EXT,
      GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT,
      GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT,
      GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT,
   };
   unsigned failures = 0, total = 0;
   for (unsigned fmt = 0; fmt < 8; fmt++) {
      GLuint tex;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      unsigned block_B = (fmt % 4) < 2 ? 8 : 16;
      for (unsigned level = 0; level < 10; level++) {
         unsigned w = size_at(512, level), h = size_at(272, level);
         unsigned nx = (w + 3) / 4, ny = (h + 3) / 4;
         uint8_t *data = calloc(nx * ny, block_B);
         CHECK(data);
         for (unsigned y = 0; y < ny; y++)
            for (unsigned x = 0; x < nx; x++) {
               encode_block(data + (y * nx + x) * block_B, fmt, x, y, level);
            }
         glCompressedTexImage2D(GL_TEXTURE_2D, level, formats[fmt], w, h, 0,
                                nx * ny * block_B, data);
         free(data);
         CHECK(glGetError() == GL_NO_ERROR);
      }
      /* Update an interior rectangle at nonzero block coordinates. */
      uint8_t patch[4 * 3 * 16] = {0};
      for (unsigned y = 0; y < 3; y++)
         for (unsigned x = 0; x < 4; x++)
            encode_block(patch + (y * 4 + x) * block_B, fmt, x + 3, y + 5, 7);
      glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 12, 20, 16, 12, formats[fmt],
                                4 * 3 * block_B, patch);
      CHECK(glGetError() == GL_NO_ERROR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      GL_NEAREST_MIPMAP_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      for (unsigned level = 0; level < 10; level++) {
         glUniform1f(glGetUniformLocation(p, "mip"), level);
         glClearColor(.25, .25, .25, .25);
         glClear(GL_COLOR_BUFFER_BIT);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         uint8_t pixels[128 * 128 * 4];
         glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         CHECK(glGetError() == GL_NO_ERROR);
         unsigned bad = 0;
         for (unsigned y = 0; y < 128; y++)
            for (unsigned x = 0; x < 128; x++) {
               uint8_t rgba[4];
               unsigned bx = (((2 * x + 1) * size_at(512, level)) / 256) / 4;
               unsigned by = (((2 * y + 1) * size_at(272, level)) / 256) / 4;
               bool patched =
                  level == 0 && bx >= 3 && bx < 7 && by >= 5 && by < 8;
               expected(fmt, bx, by, patched ? 7 : level, rgba);
               uint8_t *pixel = pixels + (y * 128 + x) * 4;
               if (memcmp(pixel, rgba, 4)) {
                  if (bad < 3)
                     fprintf(
                        stderr,
                        "fmt%u mip%u %u,%u got %u,%u,%u,%u expected %u,%u,%u,%u\n",
                        fmt, level, x, y, pixel[0], pixel[1], pixel[2],
                        pixel[3], rgba[0], rgba[1], rgba[2], rgba[3]);
                  bad++;
               }
            }
         printf("format %u mip %u: 16384 pixels, %u failures\n", fmt, level,
                bad);
         fflush(stdout);
         failures += bad;
         total += 16384;
      }
      glDeleteTextures(1, &tex);
   }
   printf("BC uploads: %u pixels checked, %u failures\n", total, failures);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d, c) && eglTerminate(d));
   return failures ? 1 : 0;
}
