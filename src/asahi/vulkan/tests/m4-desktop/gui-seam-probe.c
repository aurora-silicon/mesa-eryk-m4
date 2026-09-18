/* SPDX-License-Identifier: MIT
 * Offscreen textured-quad diagnostic. Hardware requires gpu-experiment.py.
 * cc -O2 gui-seam-probe.c -lEGL -lGL -o gui-seam-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d %s GL=%x\n", __LINE__, #x, glGetError());       \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fputs(log, stderr);
  }
  CHECK(ok);
  return s;
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  if (getenv("M4_PROBE_CHECK_ATLAS"))
    CHECK(getenv("M4_PROBE_ATLAS") && getenv("M4_PROBE_UPPER_LEFT") &&
          (getenv("M4_PROBE_EXTRA_VARYING") ||
           getenv("M4_PROBE_IMPLICIT_ONLY")) &&
          !getenv("M4_PROBE_CAPTURE_VERTICES") && !getenv("M4_PROBE_BLEND"));
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
  fflush(stdout);
  CHECK(strstr(renderer, "M4") ||
        (getenv("M4_PROBE_SOFTWARE") && strstr(renderer, "llvmpipe")));
  const int w = 2560, h = 1664, tw = getenv("M4_PROBE_ATLAS") ? 8192 : 512,
            th = getenv("M4_PROBE_ATLAS") ? 3888 : 512;
  GLuint out, fb;
  glGenTextures(1, &out);
  glBindTexture(GL_TEXTURE_2D, out);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               NULL);
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         out, 0);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  unsigned char *data = malloc(tw * th * 4);
  CHECK(data);
  for (int y = 0; y < th; y++)
    for (int x = 0; x < tw; x++) {
      int o = (y * tw + x) * 4;
      data[o] = x % 256;
      data[o + 1] = y % 256;
      data[o + 2] = ((x / 4 + y / 4) % 2) ? 200 : 80;
      data[o + 3] = 255;
    }
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               data);
  free(data);
  glGenerateMipmap(GL_TEXTURE_2D);
  if (getenv("M4_PROBE_ATLAS")) {
    int mw = tw, mh = th;
    for (int level = 0; mw || mh; level++) {
      if (!mw)
        mw = 1;
      if (!mh)
        mh = 1;
      unsigned char *m = malloc(mw * mh * 4);
      CHECK(m);
      for (int i = 0; i < mw * mh; i++) {
        m[4 * i] = level ? 255 : 142;
        m[4 * i + 1] = level ? 0 : 142;
        m[4 * i + 2] = level ? 0 : 142;
        m[4 * i + 3] = 255;
      }
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, mw, mh, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, m);
      free(m);
      if (mw == 1 && mh == 1)
        break;
      mw >>= 1;
      mh >>= 1;
    }
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  const char *vs =
      "#version 330\nlayout(location=0) in vec2 pos;layout(location=1) in vec2 "
      "uv;out vec2 coord;void "
      "main(){gl_Position=vec4(pos/vec2(1280,832)-1.,0,1);coord=uv;}";
  const char *fs =
      "#version 330\nin vec2 coord;uniform sampler2D tex;uniform int mode;out "
      "vec4 color;void main(){if(mode==0)color=vec4(.7,.5,.3,1);else "
      "if(mode==1)color=vec4(coord,0,1);else "
      "if(mode==2)color=textureLod(tex,coord,0);else "
      "if(mode==3)color=texture(tex,coord);else "
      "if(mode==4)color=vec4(dFdx(coord)*100.,0,1);else "
      "color=vec4(dFdy(coord)*100.,0,1);}";
  if (getenv("M4_PROBE_IMPLICIT_ONLY"))
    fs = "#version 330\nin vec2 coord;uniform sampler2D tex;out vec4 "
         "color;void main(){color=texture(tex,coord);}";
  if (getenv("M4_PROBE_EXTRA_VARYING")) {
    vs = "#version 330\nlayout(location=0) in vec2 pos;layout(location=1) in "
         "vec2 uv;layout(location=2) in vec4 tint;out vec4 vTint;out vec2 "
         "coord;void "
         "main(){gl_Position=vec4(pos*vec2(.00078125,.0012019231)-1.,0,1);"
         "coord=uv;vTint=tint;}";
    fs = "#version 330\nin vec4 vTint;in vec2 coord;uniform sampler2D tex;out "
         "vec4 color;void main(){color=texture(tex,coord)*vTint;}";
  }
  if (getenv("M4_PROBE_CAPTURE_VERTICES")) {
    vs = "#version 330\nlayout(location=0) in vec2 pos;layout(location=1) in "
         "vec2 uv;"
         "layout(location=2) in vec4 tint;uniform mat4 projection;out vec4 "
         "vTint;out vec2 coord;"
         "void "
         "main(){gl_Position=projection*vec4(pos,0,1);vTint=tint;coord=uv;}";
    fs = "#version 330\nin vec4 vTint;in vec2 coord;uniform sampler2D tex;out "
         "vec4 color;"
         "void main(){color=texture(tex,coord)*vTint;}";
    if (getenv("M4_PROBE_GRADIENT_OUTPUT"))
      fs = "#version 330\nin vec4 vTint;in vec2 coord;out vec4 color;"
           "void main(){color=vec4(clamp(abs(dFdx(coord.x))*8192.*8.,0.,1.),"
           "clamp(abs(dFdy(coord.y))*3888.*8.,0.,1.),0,1)*vTint;}";
  }
  GLuint pr = glCreateProgram();
  glAttachShader(pr, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(pr, shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(pr);
  GLint ok;
  glGetProgramiv(pr, GL_LINK_STATUS, &ok);
  CHECK(ok);
  glUseProgram(pr);
  glUniform1i(glGetUniformLocation(pr, "tex"), 0);
  if (getenv("M4_PROBE_CAPTURE_VERTICES")) {
    float projection[] = {.00078125, 0, 0, 0, 0,  .0012019231, 0, 0,
                          0,         0, 1, 0, -1, -1,          0, 1};
    glUniformMatrix4fv(glGetUniformLocation(pr, "projection"), 1, GL_FALSE,
                       projection);
  }
  GLuint va, bo, idx;
  glGenVertexArrays(1, &va);
  glBindVertexArray(va);
  glGenBuffers(1, &bo);
  glBindBuffer(GL_ARRAY_BUFFER, bo);
  glVertexAttribPointer(0, 2, GL_FLOAT, 0, 16, (void *)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, 0, 16, (void *)8);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttrib4f(2, 1, 1, 1, 1);
  unsigned short indices[] = {0, 1, 2, 2, 1, 3};
  glGenBuffers(1, &idx);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idx);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  if (getenv("M4_PROBE_BLEND")) {
    glEnable(GL_BLEND);
    glBlendColor(0, 0, 0, .5);
    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
  }
  if (getenv("M4_PROBE_BLEND_SRC")) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }
  if (getenv("M4_PROBE_UPPER_LEFT"))
    glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
  glViewport(0, 0, w, h);
  glClearColor(.1, .1, .1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  GLuint statistics = 0;
  if (getenv("M4_PROBE_STATS")) {
    glGenQueries(1, &statistics);
    glBeginQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB, statistics);
  }
  if (getenv("M4_PROBE_CAPTURE_VERTICES")) {
    unsigned char captured[864];
    FILE *f = fopen(getenv("M4_PROBE_CAPTURE_VERTICES"), "rb");
    CHECK(f);
    CHECK(fread(captured, 1, sizeof(captured), f) == sizeof(captured));
    fclose(f);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 24, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, 24, (void *)8);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 24, (void *)16);
    glEnableVertexAttribArray(2);
    glBufferData(GL_ARRAY_BUFFER, sizeof(captured), captured, GL_STATIC_DRAW);
    unsigned int indices[54];
    for (int q = 0; q < 9; q++)
      for (int i = 0; i < 6; i++)
        indices[q * 6 + i] = q * 4 + (unsigned int[]){0, 1, 2, 2, 1, 3}[i];
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, 54, GL_UNSIGNED_INT, 0);
  } else
    for (int mode = 0; mode < 6; mode++) {
      float x = 1080, y = 500 + mode * 85, ww = 400, hh = 60;
      float verts[] = {x,      y,      .03125, .03125, x + ww, y,
                       .09375, .03125, x,      y + hh, .03125, .09375,
                       x + ww, y + hh, .09375, .09375};
      if (getenv("M4_PROBE_ATLAS")) {
        float av[] = {1085.01, 806.01, .4189453125, .9794238,
                      1085.01, 858.01, .4189453125, .979681,
                      1475.01, 806.01, .4190673828, .9794238,
                      1475.01, 858.01, .4190673828, .979681};
        memcpy(verts, av, sizeof(verts));
        for (int j = 0; j < 4; j++)
          verts[j * 4 + 1] += (mode - 3) * 85;
      }
      if (getenv("M4_PROBE_CONSTANT_UV"))
        for (int j = 0; j < 4; j++) {
          verts[j * 4 + 2] = .078125;
          verts[j * 4 + 3] = .078125;
        }
      glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
      glUniform1i(glGetUniformLocation(pr, "mode"), mode);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    }
  if (statistics) {
    glEndQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB);
    GLuint64 count = 0;
    glGetQueryObjectui64v(statistics, GL_QUERY_RESULT, &count);
    printf("Fragment invocations: %llu\n", (unsigned long long)count);
    CHECK(count > 0);
  }
  glFinish();
  CHECK(glGetError() == GL_NO_ERROR);
  unsigned char *pixels = malloc(w * h * 4);
  CHECK(pixels);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  CHECK(glGetError() == GL_NO_ERROR);
  FILE *f = fopen(argv[1], "wb");
  CHECK(f);
  CHECK(fwrite(pixels, 4, w * h, f) == (size_t)w * h);
  CHECK(!fclose(f));
  if (getenv("M4_PROBE_CHECK_ATLAS")) {
    unsigned bad = 0, checked = 0;
    for (int q = 0; q < 6; q++)
      for (int y = 807 + (q - 3) * 85; y < 857 + (q - 3) * 85; y++)
        for (int x = 1086; x < 1474; x++) {
          /* glReadPixels rows retain the lower-left convention. */
          const unsigned char *p = pixels + 4 * ((h - 1 - y) * w + x);
          bad += memcmp(p, (unsigned char[]){142, 142, 142, 255}, 4) != 0;
          checked++;
        }
    printf("Atlas interior: %u/%u incorrect pixels\n", bad, checked);
    CHECK(bad == 0);
  }
  free(pixels);
  puts("GUI_SEAM_PROBE_COMPLETE");
  return 0;
}
