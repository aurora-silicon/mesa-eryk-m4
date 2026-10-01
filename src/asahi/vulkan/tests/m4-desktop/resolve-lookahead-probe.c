/* SPDX-License-Identifier: MIT
 * Resolve one MSAA draw into several destinations before any readback.
 * A resource-only render-pass lookahead must preserve every explicit blit.
 * Run hardware only through the watchdog/thermal guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "API failure line %d: %s GL=%x\n", __LINE__, #x, glGetError()); exit(1); } } while (0)

static GLuint shader(GLenum type, const char *source)
{
   GLuint s = glCreateShader(type);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   CHECK(ok);
   return s;
}

int main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));
   CHECK(strstr((const char *)glGetString(GL_RENDERER), "zink"));
   GLuint p = glCreateProgram();
   glAttachShader(p, shader(GL_VERTEX_SHADER,
      "#version 330 core\nvoid main(){vec2 v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(v*2.-1.,0.,1.);}"));
   glAttachShader(p, shader(GL_FRAGMENT_SHADER,
      "#version 330 core\nuniform vec4 color;out vec4 c;void main(){c=color;}"));
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(p);
   GLint loc = glGetUniformLocation(p, "color");
   CHECK(loc >= 0);
   GLuint vao;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   GLuint fb[4], rb[4];
   glGenFramebuffers(4, fb);
   glGenRenderbuffers(4, rb);
   for (unsigned i = 0; i < 4; i++) {
      glBindFramebuffer(GL_FRAMEBUFFER, fb[i]);
      glBindRenderbuffer(GL_RENDERBUFFER, rb[i]);
      if (!i)
         glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 64, 32);
      else
         glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 32);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[i]);
      CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
      glClearColor(1, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
   }
   glViewport(0, 0, 64, 32);
   unsigned bad = 0;
   for (unsigned count = 1; count <= 3; count++) {
      glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
      glUniform4f(loc, 0, 1, 0, 1);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      for (unsigned i = 1; i <= count; i++) {
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[i]);
         glBlitFramebuffer(0, 0, 64, 32, 0, 0, 64, 32, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      }
      for (unsigned i = 1; i <= count; i++) {
         unsigned char px[64 * 32 * 4];
         glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[i]);
         glReadPixels(0, 0, 64, 32, GL_RGBA, GL_UNSIGNED_BYTE, px);
         CHECK(glGetError() == GL_NO_ERROR);
         unsigned mismatches = 0;
         for (unsigned j = 0; j < 64 * 32; j++)
            mismatches += px[j*4] != 0 || px[j*4+1] != 255 || px[j*4+2] != 0 || px[j*4+3] != 255;
         printf("targets=%u destination=%u mismatches=%u\n", count, i, mismatches);
         bad += mismatches;
      }
   }
   /* Resolving depth is a use of the attachment even when the preceding
    * color draw has depth testing disabled. The previous contents survive. */
   GLuint depth[2];
   glGenRenderbuffers(2, depth);
   for (unsigned i = 0; i < 2; i++) {
      glBindFramebuffer(GL_FRAMEBUFFER, fb[i]);
      glBindRenderbuffer(GL_RENDERBUFFER, depth[i]);
      if (!i)
         glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT32F, 64, 32);
      else
         glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, 64, 32);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth[i]);
      CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
      glClearDepth(i ? 1.0 : 0.25);
      glClear(GL_DEPTH_BUFFER_BIT);
   }
   glFinish();
   for (unsigned use_depth = 0; use_depth < 2; use_depth++) {
      glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
      if (use_depth) {
         glEnable(GL_DEPTH_TEST);
         glDepthFunc(GL_ALWAYS);
      }
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1]);
      glBlitFramebuffer(0, 0, 64, 32, 0, 0, 64, 32, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1]);
      float px[64 * 32];
      glReadPixels(0, 0, 64, 32, GL_DEPTH_COMPONENT, GL_FLOAT, px);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned mismatches = 0;
      float expected = use_depth ? 0.5f : 0.25f;
      for (unsigned j = 0; j < 64 * 32; j++)
         mismatches += !(px[j] >= expected - 0.0001f && px[j] <= expected + 0.0001f);
      printf("depth_test=%u mismatches=%u first=%f expected=%f\n", use_depth, mismatches, px[0], expected);
      bad += mismatches;
   }
   glFinish();
   CHECK(glGetError() == GL_NO_ERROR);
   glDeleteRenderbuffers(4, rb);
   glDeleteRenderbuffers(2, depth);
   glDeleteFramebuffers(4, fb);
   glDeleteVertexArrays(1, &vao);
   glDeleteProgram(p);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d, c));
   CHECK(eglTerminate(d));
   puts(bad ? "FAIL: consecutive resolves" : "PASS: consecutive resolves");
   /* Expected old-driver pixel failures are observations, not device faults.
    * The host validates the PASS marker after the guard has drained/disarmed. */
   return 0;
}
