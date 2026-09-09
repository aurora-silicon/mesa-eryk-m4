/* SPDX-License-Identifier: MIT
 * Verify native M4 translucent punch-through through Zink: preserve discarded
 * pixels and blend retained fragments. Requires the J713 watchdog/thermal guard.
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
   CHECK(strstr(r, "zink") && strstr(r, "M4"));

   const char *vs =
      "#version 300 es\nvoid main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}";
   const char *fs =
      "#version 300 es\nprecision highp float;out vec4 color;void main(){if(gl_FragCoord.x<32.0)discard;color=vec4(1,0,0,0.5);}";
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
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          tex, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 64, 64);
   glClearColor(0, 0, 1, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   CHECK(glGetError() == GL_NO_ERROR);
   unsigned char pixels[64 * 64 * 4];
   glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   CHECK(glGetError() == GL_NO_ERROR);
   if (argc > 1) {
      FILE *out = fopen(argv[1], "wb");
      CHECK(out);
      CHECK(fwrite(pixels, 1, sizeof(pixels), out) == sizeof(pixels));
      CHECK(fclose(out) == 0);
   }
   for (unsigned x = 0; x < 64; x += 8)
      fprintf(stderr, "x%u: %u,%u,%u,%u\n", x, pixels[x * 4], pixels[x * 4 + 1],
              pixels[x * 4 + 2], pixels[x * 4 + 3]);
   for (unsigned y = 0; y < 64; y++)
      for (unsigned x = 0; x < 64; x++) {
         unsigned char *p = pixels + 4 * (y * 64 + x);
         if (x < 32) {
            CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255);
         } else {
            CHECK(abs(p[0] - 128) <= 1 && p[1] == 0 && abs(p[2] - 127) <= 1 &&
                  abs(p[3] - 191) <= 1);
         }
      }
   puts("PASS: 4096 pixels verify alpha blending and discarded coverage");
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   return 0;
}
