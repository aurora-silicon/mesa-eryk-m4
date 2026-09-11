/* SPDX-License-Identifier: MIT
 * Depth prepass/reload equality over nonconstant depth and clip W.
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

int
main(void)
{
   EGLDisplay display =
      eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(display, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION,
                           3,
                           EGL_CONTEXT_MINOR_VERSION,
                           3,
                           EGL_CONTEXT_OPENGL_PROFILE_MASK,
                           EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                           EGL_NONE};
   EGLContext context =
      eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(context != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLuint program = glCreateProgram();
   GLuint vs = compile(
      GL_VERTEX_SHADER,
      "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);float w=1.+p.x*.25+p.y*.5;gl_Position=vec4((p*2.-1.)*w,(p.x*.373+p.y*.157-.731)*w,w);}");
   GLuint fs = compile(
      GL_FRAGMENT_SHADER,
      "#version 330 core\nout vec4 color;void main(){color=vec4(0,1,0,1);}");
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint linked;
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   CHECK(linked);
   glUseProgram(program);
   GLuint vao, texture, fbo;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          texture, 0);
   GLuint depth, other, other_tex;
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, 128, 128);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, depth);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glGenTextures(1, &other_tex);
   glBindTexture(GL_TEXTURE_2D, other_tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &other);
   glBindFramebuffer(GL_FRAMEBUFFER, other);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          other_tex, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glEnable(GL_DEPTH_TEST);

   glViewport(0, 0, 128, 128);
   unsigned char pixels[128 * 128 * 4];
   unsigned bad_pixels = 0;
   for (unsigned pass = 0; pass < 8; pass++) {
      glDepthMask(GL_TRUE);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(1, 0, 0, 1);
      glClearDepth(1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glDepthFunc(GL_LESS);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFlush();
      glBindFramebuffer(GL_FRAMEBUFFER, other);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(0, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glDepthFunc(GL_LEQUAL);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned pass_bad = 0;
      for (unsigned i = 0; i < 128 * 128; i++)
         pass_bad += !(pixels[4 * i] == 0 && pixels[4 * i + 1] == 255 &&
                       pixels[4 * i + 2] == 0 && pixels[4 * i + 3] == 255);
      bad_pixels += pass_bad;
      printf("pass%u prepass equality pixels_bad=%u/16384\n", pass, pass_bad);
      fflush(stdout);
   }

   glDeleteFramebuffers(1, &fbo);
   glDeleteTextures(1, &texture);
   glDeleteVertexArrays(1, &vao);
   glDeleteProgram(program);
   glDeleteShader(vs);
   glDeleteShader(fs);
   eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(display, context);
   eglTerminate(display);
   CHECK(bad_pixels == 0);
   puts("PASS: sloped depth prepass preserves 131072 GPU-rendered pixels");
   return 0;
}
