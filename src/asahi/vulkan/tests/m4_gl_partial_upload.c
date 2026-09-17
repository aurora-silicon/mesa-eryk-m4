/* SPDX-License-Identifier: MIT
 * Compare partial SHM-style uploads and scissored compositing against full
 * uploads/redraws. Check source bytes as well as final blended pixels. Run with
 * the mandatory J713 watchdog and thermal guards; M4_TEST_LLVMPIPE allows the
 * software reference.
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

   int W = argc > 1 ? atoi(argv[1]) : 257, H = argc > 2 ? atoi(argv[2]) : 193;
   unsigned frames = argc > 3 ? atoi(argv[3]) : 48;
   CHECK(W >= 257 && W <= 2560 && H >= 193 && H <= 1664 && frames > 0 &&
         frames <= 64);
   int BYTES = W * H * 4;
   unsigned char *pixels = malloc(BYTES), *actual = malloc(BYTES),
                 *reference = malloc(BYTES);
   CHECK(pixels && actual && reference);
   const char *vs =
      "#version 300 es\nout highp vec2 uv;void main(){vec2 p=vec2(gl_VertexID&1,(gl_VertexID>>1)&1);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
   const char *fs =
      "#version 300 es\nprecision highp float;uniform highp sampler2D image;uniform vec2 size;in highp vec2 uv;out vec4 color;void main(){color=texture(image,uv);}";
   GLuint prog = glCreateProgram();
   const char *sources[] = {vs, fs};
   for (unsigned i = 0; i < 2; i++) {
      GLuint sh = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      glShaderSource(sh, 1, &sources[i], NULL);
      glCompileShader(sh);
      GLint ok;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      CHECK(ok);
      glAttachShader(prog, sh);
      glDeleteShader(sh);
   }
   glLinkProgram(prog);
   GLint ok;
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(prog);
   glUniform1i(glGetUniformLocation(prog, "image"), 0);
   GLuint tex[5], fb;
   glGenTextures(5, tex);
   glGenFramebuffers(1, &fb);
   for (unsigned i = 0; i < 5; i++) {
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   }
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
         unsigned char *p = actual + (y * W + x) * 4;
         p[0] = (x * 7 + y * 13) & 255;
         p[1] = (x + y * 5) & 255;
         p[2] = (x * 3 + y) & 255;
         p[3] = 255;
      }
   glBindTexture(GL_TEXTURE_2D, tex[4]);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE,
                   actual);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glViewport(0, 0, W, H);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, W);
   glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
   unsigned bad_copy = 0, bad_blend = 0, max_difference = 0;
   int old_right = 0, old_bottom = 0;
   for (unsigned frame = 0; frame < frames; frame++) {
      int right = W - 127 + ((frame * 7) % 100),
          bottom = H - 123 + ((frame * 3) % 95);
      for (int y = 0; y < H; y++)
         for (int x = 0; x < W; x++) {
            unsigned char *p = pixels + (y * W + x) * 4;
            p[0] = p[1] = p[2] = 0;
            p[3] = 136;
            if (x >= 10 && x <= right && y >= 10 && y <= bottom) {
               p[3] = 0;
               int edge = x == 10 || x == right || y == 10 || y == bottom;
               int fringe =
                  x == 11 || x == right - 1 || y == 11 || y == bottom - 1;
               if (edge || fringe) {
                  p[0] = edge ? 80 : 40;
                  p[1] = edge ? 120 : 60;
                  p[2] = edge ? 180 : 90;
                  p[3] = edge ? 255 : 128;
               }
            }
         }
      int minx = (right < old_right ? right : old_right) - 2;
      int maxx = (right > old_right ? right : old_right) + 2;
      int miny = (bottom < old_bottom ? bottom : old_bottom) - 2;
      int maxy = (bottom > old_bottom ? bottom : old_bottom) + 2;
      int boxes[2][4] = {{minx, 0, maxx - minx + 1, H},
                         {0, miny, W, maxy - miny + 1}};
      glBindTexture(GL_TEXTURE_2D, tex[0]);
      if (!frame)
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixels);
      else
         for (unsigned i = 0; i < 2; i++) {
            int *b = boxes[i];
            glTexSubImage2D(GL_TEXTURE_2D, 0, b[0], b[1], b[2], b[3], GL_RGBA,
                            GL_UNSIGNED_BYTE, pixels + (b[1] * W + b[0]) * 4);
         }
      glBindTexture(GL_TEXTURE_2D, tex[1]);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE,
                      pixels);
      for (unsigned which = 0; which < 2; which++) {
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, tex[which], 0);
         CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
               GL_FRAMEBUFFER_COMPLETE);
         glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, reference);
         for (unsigned i = 0; i < BYTES; i++)
            if (reference[i] != pixels[i]) {
               if (bad_copy < 8)
                  printf("copy frame=%u path=%u byte=%u got=%u expected=%u\n",
                         frame, which, i, reference[i], pixels[i]);
               bad_copy++;
            }
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, tex[which + 2], 0);
         glBindTexture(GL_TEXTURE_2D, tex[which]);
         unsigned rects = which || !frame ? 1 : 2;
         for (unsigned i = 0; i < rects; i++) {
            if (which || !frame)
               glDisable(GL_SCISSOR_TEST);
            else {
               glEnable(GL_SCISSOR_TEST);
               int *b = boxes[i];
               glScissor(b[0], b[1], b[2], b[3]);
            }
            glDisable(GL_BLEND);
            glBindTexture(GL_TEXTURE_2D, tex[4]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glEnable(GL_BLEND);
            glBindTexture(GL_TEXTURE_2D, tex[which]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
         }
         glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE,
                      which ? reference : actual);
         CHECK(glGetError() == GL_NO_ERROR);
      }
      for (unsigned i = 0; i < BYTES; i++)
         if (actual[i] != reference[i]) {
            unsigned delta = abs((int)actual[i] - (int)reference[i]);
            if (delta > max_difference)
               max_difference = delta;
            if (bad_blend < 8)
               printf("blend frame=%u byte=%u got=%u expected=%u\n", frame, i,
                      actual[i], reference[i]);
            bad_blend++;
         }
      old_right = right;
      old_bottom = bottom;
   }
   printf(
      "partial upload: frames=%u size=%dx%d copy_bad=%u blend_bad=%u max_difference=%u\n",
      frames, W, H, bad_copy, bad_blend, max_difference);
   glDeleteFramebuffers(1, &fb);
   glDeleteTextures(5, tex);
   glDeleteProgram(prog);
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   free(pixels);
   free(actual);
   free(reference);
   return bad_copy || bad_blend;
}
