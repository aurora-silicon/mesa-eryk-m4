/* SPDX-License-Identifier: MIT
 * Verify coarse derivatives at primitive boundaries through native M4 Zink.
 * Requires the J713 watchdog/thermal guard.
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
      "#version 300 es\nprecision highp float;layout(location=0) in vec4 data;"
      "out vec2 v;void main(){gl_Position=vec4(data.xy,0,1);v=data.zw;}";
   const char *fs =
      "#version 300 es\nprecision highp float;in vec2 v;out vec4 color;"
      "void main(){vec2 dx=dFdx(v),dy=dFdy(v);"
      "bool ok=all(lessThan(abs(dx-vec2(1,0)),vec2(0.02)))&&"
      "all(lessThan(abs(dy-vec2(0,1)),vec2(0.02)));"
      "color=ok?vec4(0,1,0,1):vec4(1,0,0,1);}";
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
   float vertices[8 * 8 * 6][4];
   const unsigned corners[6][2] = {{0,0},{1,0},{0,1},{1,0},{1,1},{0,1}};
   for (unsigned i = 0; i < 8 * 8 * 6; ++i) {
      unsigned cell = i / 6;
      float x = (cell % 8 + corners[i % 6][0]) * 8;
      float y = (cell / 8 + corners[i % 6][1]) * 8;
      vertices[i][0] = x / 32 - 1;
      vertices[i][1] = y / 32 - 1;
      vertices[i][2] = x + (i / 3) * 32;
      vertices[i][3] = y + (i / 3) * 32;
   }
   GLuint vbo;
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vertices[0]), NULL);
   glEnableVertexAttribArray(0);
   glDrawArrays(GL_TRIANGLES, 0, 8 * 8 * 6);
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
   unsigned bad = 0;
   for (unsigned i = 0; i < 64 * 64; ++i) {
      const unsigned char *p = &pixels[4 * i];
      if (p[0] || p[1] != 255 || p[2] || p[3] != 255) {
         if (bad < 8)
            fprintf(stderr, "pixel %u,%u: %u,%u,%u,%u\n", i % 64, i / 64,
                    p[0], p[1], p[2], p[3]);
         ++bad;
      }
   }
   fprintf(stderr, "%u incorrect pixels\n", bad);
   CHECK(bad == 0);
   puts("PASS: 4096 pixels verify derivatives across 128 discontinuous triangles");
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   return 0;
}
