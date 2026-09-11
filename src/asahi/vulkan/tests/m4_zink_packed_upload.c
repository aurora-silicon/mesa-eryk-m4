/* SPDX-License-Identifier: MIT
 * ARGB1555 uploads, mip migration, interior patches and sampled RGBA readback.
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

static unsigned short
word(unsigned x, unsigned y, unsigned level)
{
   return (((x + y + level) & 1) << 15) | (((x + level) & 31) << 10) |
          (((y + 3 * level) & 31) << 5) | ((x + 5 * y + level) & 31);
}

int
main(void)
{
   EGLDisplay d =
      eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION,
                     3,
                     EGL_CONTEXT_MINOR_VERSION,
                     3,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK,
                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                     EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT &&
         eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLuint p = glCreateProgram();
   GLuint vs = compile(
      GL_VERTEX_SHADER,
      "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}");
   GLuint fs = compile(
      GL_FRAGMENT_SHADER,
      "#version 330 core\nuniform sampler2D t;uniform int mip;out vec4 color;void main(){color=texelFetch(t,ivec2(gl_FragCoord.xy),mip);}");
   glAttachShader(p, vs);
   glAttachShader(p, fs);
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(p);
   GLuint vao, fbo, target, tex;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenTextures(1, &target);
   glBindTexture(GL_TEXTURE_2D, target);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 32, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          target, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
   for (unsigned level = 0; level < 7; level++) {
      unsigned w = 64 >> level, h = (32 >> level) ? (32 >> level) : 1;
      unsigned short data[64 * 32];
      for (unsigned y = 0; y < h; y++)
         for (unsigned x = 0; x < w; x++)
            data[y * w + x] = word(x, y, level);
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGB5_A1, w, h, 0, GL_BGRA,
                   GL_UNSIGNED_SHORT_1_5_5_5_REV, data);
      CHECK(glGetError() == GL_NO_ERROR);
   }
   unsigned short patch[12 * 7];
   memset(patch, 0xa5, sizeof(patch));
   for (unsigned y = 0; y < 5; y++)
      for (unsigned x = 0; x < 8; x++)
         patch[(y + 1) * 12 + x + 2] = word(x + 7, y + 3, 11);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 12);
   glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
   glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 7, 3, 8, 5, GL_BGRA,
                   GL_UNSIGNED_SHORT_1_5_5_5_REV, patch);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
   glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
   glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   unsigned total = 0, bad = 0;
   for (unsigned pass = 0; pass < 2; pass++)
      for (unsigned level = 0; level < 7; level++) {
         unsigned w = 64 >> level, h = (32 >> level) ? (32 >> level) : 1;
         glViewport(0, 0, w, h);
         glUniform1i(glGetUniformLocation(p, "mip"), level);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         unsigned char pixels[64 * 32 * 4];
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         CHECK(glGetError() == GL_NO_ERROR);
         unsigned errors = 0;
         for (unsigned y = 0; y < h; y++)
            for (unsigned x = 0; x < w; x++) {
               unsigned v = word(
                  x, y,
                  level == 0 && x >= 7 && x < 15 && y >= 3 && y < 8 ? 11
                                                                    : level);
               unsigned expect[] = {(((v >> 10) & 31) * 255 + 15) / 31,
                                    (((v >> 5) & 31) * 255 + 15) / 31,
                                    ((v & 31) * 255 + 15) / 31,
                                    (v >> 15) * 255};
               unsigned char *got = pixels + (y * w + x) * 4;
               for (unsigned i = 0; i < 4; i++)
                  if (abs((int)got[i] - (int)expect[i]) > 1) {
                     if (errors < 3)
                        fprintf(
                           stderr,
                           "pass%u mip%u %u,%u channel%u got%u expected%u\n",
                           pass, level, x, y, i, got[i], expect[i]);
                     errors++;
                     break;
                  }
            }
         printf("pass%u mip%u: %u pixels, %u failures\n", pass, level, w * h,
                errors);
         fflush(stdout);
         total += w * h;
         bad += errors;
      }
   printf("Packed uploads: %u pixels checked, %u failures\n", total, bad);
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   return bad ? 1 : 0;
}
