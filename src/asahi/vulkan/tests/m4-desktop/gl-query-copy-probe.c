/* Exercise immediate query readback through Zink, including empty queries.
 * Build: cc -O2 gl-query-copy-probe.c -lEGL -lGL -o gl-query-copy-probe
 * Hardware execution requires gpu-experiment.py. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "FAIL line %d GL=%x\n", __LINE__, glGetError());         \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum t, const char *s) {
  GLuint x = glCreateShader(t);
  glShaderSource(x, 1, &s, NULL);
  glCompileShader(x);
  return x;
}
int main(int argc, char **argv) {
  setbuf(stdout, NULL);
  EGLDisplay d =
      eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
  CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
  const EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION,
                         4,
                         EGL_CONTEXT_MINOR_VERSION,
                         5,
                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                         EGL_NONE};
  EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
  CHECK(c != EGL_NO_CONTEXT &&
        eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
  printf("Renderer: %s\n", glGetString(GL_RENDERER));
  int mode = argc > 1 ? atoi(argv[1]) : 0;
  int rounds = argc > 2 ? atoi(argv[2]) : 16;
  CHECK(mode >= 0 && mode <= 5 && rounds > 0 && rounds <= 2000);
  GLuint tex = 0, fb = 0;
  if (mode == 4) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  }
  if (mode == 4) {
    GLuint p = glCreateProgram();
    glAttachShader(
        p, shader(GL_VERTEX_SHADER, "#version 450\nvoid main(){vec2 "
                                    "v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                                    "gl_Position=vec4(v*2.-1.,0.,1.);}"));
    glAttachShader(
        p, shader(GL_FRAGMENT_SHADER, "#version 450\nlayout(location=0) out "
                                      "vec4 c;void main(){c=vec4(1,0,0,1);}"));
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);
    glUseProgram(p);
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, 64, 64);
  }
  GLuint q[2], b;
  glGenQueries(2, q);
  glGenBuffers(1, &b);
  glBindBuffer(GL_QUERY_BUFFER, b);
  glBufferData(GL_QUERY_BUFFER, 32, NULL, GL_STREAM_READ);
  glBindBuffer(GL_QUERY_BUFFER, 0);
  for (int i = 0; i < rounds; i++) {
    printf("QUERY mode=%d round=%d\n", mode, i);
    if (mode == 4) {
      glClearColor(1, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glQueryCounter(q[0], GL_TIMESTAMP);
      glClearColor(0, 1, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glQueryCounter(q[1], GL_TIMESTAMP);
    } else if (mode == 0 || mode == 3) {
      glQueryCounter(q[0], GL_TIMESTAMP);
      glQueryCounter(q[1], GL_TIMESTAMP);
    } else {
      GLenum type = (mode == 1 || mode == 5) ? GL_TIME_ELAPSED : GL_SAMPLES_PASSED;
      glBeginQuery(type, q[0]);
      if (mode == 5) glFlush();
      glEndQuery(type);
      glBeginQuery(type, q[1]);
      glEndQuery(type);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    GLuint64 v[2] = {0};
    if (mode == 3 || mode == 4) {
      glBindBuffer(GL_QUERY_BUFFER, b);
      glGetQueryObjectui64v(q[0], GL_QUERY_RESULT, (void *)0);
      glGetQueryObjectui64v(q[1], GL_QUERY_RESULT, (void *)8);
      glGetBufferSubData(GL_QUERY_BUFFER, 0, 16, v);
      glBindBuffer(GL_QUERY_BUFFER, 0);
    } else {
      glGetQueryObjectui64v(q[0], GL_QUERY_RESULT, &v[0]);
      glGetQueryObjectui64v(q[1], GL_QUERY_RESULT, &v[1]);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    if (mode == 0 || mode == 3 || mode == 4)
      CHECK(v[0] != 0 && v[1] >= v[0]);
    if (mode == 2)
      CHECK(v[0] == 0 && v[1] == 0);
    printf("VALUES %llu %llu\n", (unsigned long long)v[0],
           (unsigned long long)v[1]);
  }
  glDeleteQueries(2, q);
  glDeleteBuffers(1, &b);
  eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(d, c);
  eglTerminate(d);
  puts("QUERY_COPY_PASS");
  return 0;
}
