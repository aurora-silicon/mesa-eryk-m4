/* SPDX-License-Identifier: MIT
 * Mixed float, sRGB and integer attachment clear, draw and reload oracle.
 * Hardware invocation requires tools/gpu-experiment.py watchdog/thermal guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL: line %d: %s GL=%x\n", __LINE__, #x,               \
              glGetError());                                                   \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum type, const char *s) {
  GLuint p = glCreateShader(type);
  glShaderSource(p, 1, &s, 0);
  glCompileShader(p);
  GLint ok;
  glGetShaderiv(p, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(p, sizeof(log), 0, log);
    fprintf(stderr, "%s\n", log);
  }
  CHECK(ok);
  return p;
}

int main(int argc, char **argv) {
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
  EGLContext ctx =
      eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  CHECK(ctx != EGL_NO_CONTEXT);
  CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s\n", renderer);
  fflush(stdout);
  CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
        (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
  const char *vs = "#version 330 core\nvoid main(){vec2 "
                   "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4("
                   "p*2.-1.,0.,1.);}";

  unsigned test = argc > 1 ? strtoul(argv[1], 0, 10) : 0;
  CHECK(test < 6);
  const int sparse = argc > 2 && !strcmp(argv[2], "sparse");
  const int mixed = argc > 2 && !strcmp(argv[2], "mixed");
  CHECK((!sparse && !mixed) || (test == 3 || test == 4));
  const GLenum formats[6][5] = {{GL_R16UI, 0, 0},
                                {GL_SRGB8_ALPHA8, GL_RGBA8, 0},
                                {GL_RGBA16F, GL_R16UI, 0},
                                {GL_RGBA16F, GL_RG16F, GL_R16UI},
                                {GL_RGBA16F, GL_R32UI, GL_RG16, GL_RGB10_A2, GL_RGB10_A2},
                                {GL_RG16}};
  const unsigned count = test == 4 ? 5 : (test == 0 || test == 5) ? 1 : test == 3 ? 3 : 2;
  char fs[2048] = "#version 330 core\n";
  for (unsigned rt = 0; rt < count; rt++) {
    char line[256];
    snprintf(line, sizeof(line), "layout(location=%u)out %s out%u;\n", rt,
             (formats[test][rt] == GL_R16UI || formats[test][rt] == GL_R32UI) ? "uint" : "vec4", rt);
    strcat(fs, line);
  }
  strcat(fs, "void main(){");
  for (unsigned rt = 0; rt < count; rt++) {
    char line[256];
    snprintf(line, sizeof(line), "out%u=%s;", rt,
             (formats[test][rt] == GL_R16UI || formats[test][rt] == GL_R32UI) ? "23456u" : "vec4(.25,.5,.75,1.)");
    strcat(fs, line);
  }
  strcat(fs, "}");
  GLuint p = glCreateProgram();
  glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(p);
  GLint ok;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  CHECK(ok);
  glUseProgram(p);
  GLuint vao, fb, tex[5];
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glGenTextures(count, tex);
  GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                          GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
  for (unsigned rt = 0; rt < count; rt++) {
    glBindTexture(GL_TEXTURE_2D, tex[rt]);
    glTexStorage2D(GL_TEXTURE_2D, 1, formats[test][rt], 64, 32);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[rt], GL_TEXTURE_2D,
                           tex[rt], 0);
  }
  glDrawBuffers(count, attachments);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  glViewport(0, 0, 64, 32);
  glEnable(GL_FRAMEBUFFER_SRGB);
  const GLfloat clear[] = {.5, .25, .125, 1};
  const GLuint iclear[] = {54321, 0, 0, 0};
  unsigned failures = 0;
  for (unsigned phase = 0; phase < 3; phase++) {
    if (!phase)
      for (unsigned rt = 0; rt < count; rt++) {
        if ((formats[test][rt] == GL_R16UI || formats[test][rt] == GL_R32UI))
          glClearBufferuiv(GL_COLOR, rt, iclear);
        else
          glClearBufferfv(GL_COLOR, rt, clear);
      }
    else {
      if (mixed) {
        glDisable(GL_SCISSOR_TEST);
        glClearBufferfv(GL_COLOR, 1, clear);
      }
      /* Discard the first two targets while preserving RT2 across passes. */
      if (sparse)
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
      glEnable(GL_SCISSOR_TEST);
      glScissor(phase == 1 ? 0 : 32, 0, 32, 32);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);
    for (unsigned rt = 0; rt < count; rt++) {
      if (sparse && phase && rt < 2)
        continue;
      unsigned bad = 0;
      glReadBuffer(attachments[rt]);
      uint32_t ui[64 * 32];
      float rgba[64 * 32 * 4];
      if ((formats[test][rt] == GL_R16UI || formats[test][rt] == GL_R32UI))
        glReadPixels(0, 0, 64, 32, GL_RED_INTEGER, GL_UNSIGNED_INT, ui);
      else if (formats[test][rt] == GL_RG16) {
        uint16_t rg[64 * 32 * 2];
        glReadPixels(0, 0, 64, 32, GL_RG, GL_UNSIGNED_SHORT, rg);
        for (unsigned px = 0; px < 64 * 32; ++px) {
          rgba[4*px] = rg[2*px] / 65535.f;
          rgba[4*px+1] = rg[2*px+1] / 65535.f;
          rgba[4*px+2] = 0; rgba[4*px+3] = 1;
        }
      } else
        glReadPixels(0, 0, 64, 32, GL_RGBA, GL_FLOAT, rgba);
      CHECK(glGetError() == GL_NO_ERROR);
      for (unsigned px = 0; px < 64 * 32; px++) {
        int drawn = phase == 2 || (phase == 1 && px % 64 < 32);
        if (mixed && rt == 1 && phase == 2)
          drawn = px % 64 >= 32;
        if ((formats[test][rt] == GL_R16UI || formats[test][rt] == GL_R32UI)) {
          unsigned expected = drawn ? 23456 : 54321;
          if (ui[px] != expected) {
            if (bad < 3)
              fprintf(stderr, "px%u got%u expected%u\n", px, ui[px], expected);
            bad++;
          }
        } else
          for (unsigned c = 0; c < 4; c++) {
            const float draw[] = {.25, .5, .75, 1};
            float expected = drawn ? draw[c] : clear[c];
            if ((formats[test][rt] == GL_RG16F || formats[test][rt] == GL_RG16) && c >= 2)
              expected = c == 3 ? 1 : 0;
            if (formats[test][rt] == GL_SRGB8_ALPHA8 && c < 3)
              expected = expected <= .0031308
                             ? expected * 12.92
                             : 1.055 * powf(expected, 1 / 2.4f) - .055;
            if (formats[test][rt] == GL_RGBA8 ||
                formats[test][rt] == GL_SRGB8_ALPHA8)
              expected = roundf(expected * 255) / 255;
            float got = rgba[px * 4 + c];
            if (!isfinite(got) || fabsf(got - expected) > .004) {
              if (bad < 3)
                fprintf(stderr, "px%u c%u got%g expected%g\n", px, c, got,
                        expected);
              bad++;
            }
          }
      }
      printf("%s: MRT case=%u phase=%u rt=%u bad=%u\n", bad ? "FAIL" : "PASS",
             test, phase, rt, bad);
      fflush(stdout);
      failures += bad;
    }
  }
  glDeleteFramebuffers(1, &fb);
  glDeleteTextures(count, tex);
  glDeleteProgram(p);
  glDeleteVertexArrays(1, &vao);
  eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(d, ctx);
  eglTerminate(d);
  return failures ? 1 : 0;
}
