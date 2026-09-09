/* SPDX-License-Identifier: MIT
 * Exercise a depth-only state update with unchanged vertex/fragment shaders. Run under the mandatory J713 watchdog and thermal guard.
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
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 16, 16);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, depth);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glClearColor(0, 0, 1, 1);
   glClearDepthf(0.5f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   GLint paint = glGetUniformLocation(prog, "paint");
   CHECK(paint >= 0);
   glUniform4f(paint, 0, 1, 0, 1);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   /* Same program, uniforms and geometry; only the depth comparison changes.
    * The native record must replace the failing LESS test with LEQUAL. */
   glDepthFunc(GL_LEQUAL);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   CHECK(glGetError() == GL_NO_ERROR);
   unsigned char pixels[16 * 16 * 4];
   glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   CHECK(glGetError() == GL_NO_ERROR);
   if (argc > 1) {
      FILE *out = fopen(argv[1], "wb");
      CHECK(out);
      CHECK(fwrite(pixels, 1, sizeof(pixels), out) == sizeof(pixels));
      CHECK(fclose(out) == 0);
   }
   for (unsigned i = 0; i < 16 * 16; ++i) {
      const unsigned char *p = pixels + 4 * i;
      if (p[0] != 0 || p[1] != 255 || p[2] != 0 || p[3] != 255) {
         fprintf(stderr, "pixel %u: %u %u %u %u, expected 0 255 0 255\n",
                 i, p[0], p[1], p[2], p[3]);
         return 1;
      }
   }
   puts("PASS: depth-only update produces 256 exact green pixels");
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   return 0;
}
