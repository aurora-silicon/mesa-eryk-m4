/* SPDX-License-Identifier: MIT
 * Per-sample coverage, resolve and reload oracle; run hardware under GPU guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum type, const char *text) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &text, 0);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(s, sizeof(log), 0, log);
    fputs(log, stderr);
  }
  CHECK(ok);
  return s;
}
int main(int argc, char **argv) {
  unsigned samples = argc > 1 ? atoi(argv[1]) : 4;
  CHECK(samples == 1 || samples == 2 || samples == 4);
  EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, 0, 0);
  CHECK(eglInitialize(d, 0, 0));
  CHECK(eglBindAPI(EGL_OPENGL_API));
  EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION,
                    3,
                    EGL_CONTEXT_MINOR_VERSION,
                    3,
                    EGL_CONTEXT_OPENGL_PROFILE_MASK,
                    EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                    EGL_NONE};
  EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  CHECK(c != EGL_NO_CONTEXT);
  CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s samples=%u\n", renderer, samples);
  fflush(stdout);
  CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
        (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
  const char *vs = "#version 330 core\nlayout(location=0)in vec2 p;void "
                   "main(){gl_Position=vec4(p,0.,1.);}";
  const char *fs = "#version 330 core\nlayout(location=0)out vec4 c;void "
                   "main(){c=vec4(1.,0.,0.,1.);}";
  GLuint program = glCreateProgram();
  glAttachShader(program, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(program, shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(program);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  CHECK(ok);
  glUseProgram(program);
  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  const float vertices[] = {-1, -1, 3, -1, -1, 3};
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glViewport(0, 0, 64, 64);
  GLuint fb[2], rb[2];
  glGenFramebuffers(2, fb);
  glGenRenderbuffers(2, rb);
  for (unsigned i = 0; i < 2; ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb[i]);
    glBindRenderbuffer(GL_RENDERBUFFER, rb[i]);
    if (i || samples == 1)
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);
    else
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, 64,
                                       64);
    GLint actual;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES,
                                 &actual);
    CHECK(actual == (i || samples == 1 ? 0 : (int)samples));
    CHECK(glGetError() == GL_NO_ERROR);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, rb[i]);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_MULTISAMPLE);
  if (!getenv("M4_MSAA_ALL_SAMPLES"))
    glEnable(GL_SAMPLE_MASK);
  unsigned total_bad = 0;
  for (unsigned step = 0; step <= samples; ++step) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
    if (step) {
      glSampleMaski(0, 1u << (step - 1));
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1]);
    glBlitFramebuffer(0, 0, 64, 64, 0, 0, 64, 64, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    CHECK(glGetError() == GL_NO_ERROR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1]);
    unsigned char px[64 * 64 * 4];
    memset(px, 0xa5, sizeof(px));
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(glGetError() == GL_NO_ERROR);
    unsigned bad = 0;
    int red = getenv("M4_MSAA_ALL_SAMPLES") ? (step ? 255 : 0)
                                            : lroundf(255.f * step / samples);
    for (unsigned i = 0; i < 64 * 64; ++i) {
      if (abs(px[4 * i] - red) > 1 || px[4 * i + 1] || px[4 * i + 2] ||
          px[4 * i + 3] != 255) {
        if (bad < 2)
          fprintf(stderr, "pixel%u=%u,%u,%u,%u expected=%d,0,0,255\n", i,
                  px[4 * i], px[4 * i + 1], px[4 * i + 2], px[4 * i + 3], red);
        bad++;
      }
    }
    printf("samples=%u filled=%u bad=%u/4096\n", samples, step, bad);
    fflush(stdout);
    total_bad += bad;
  }
  glFinish();
  glDeleteFramebuffers(2, fb);
  glDeleteRenderbuffers(2, rb);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(d, c);
  eglTerminate(d);
  printf("%s: multisample coverage resolve and reload\n",
         total_bad ? "FAIL" : "PASS");
  return total_bad ? 1 : 0;
}
