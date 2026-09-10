/* SPDX-License-Identifier: MIT
 * Exercise fragment loop retirement and loop-carried texture values on M4.
 * Live J713 runs require the watchdog and thermal guards.
 */
#include <math.h>
#include <stdbool.h>
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
#define W 64
#define H 16
static GLuint
program(unsigned mode)
{
   const char *vs =
      "#version 300 es\nprecision highp float;layout(location=0) in vec2 pos;void main(){gl_Position=vec4(pos,0,1);}";
   const char *body =
      "precision highp float;precision highp int;uniform int limit;uniform float float_limit;uniform sampler2D tex;out vec4 color;\n"
      "void main(){\n"
      "#if MODE == 0 || MODE == 3\n"
      "int steps=min(int(gl_FragCoord.x)-8,limit);\n"
      "#elif FLOAT_BOUND == 2\n"
      "int steps=int(uint(float_limit+gl_FragCoord.x*0.0001));\n"
      "#elif FLOAT_BOUND == 1\n"
      "int steps=int(float_limit+gl_FragCoord.x*0.0001);\n"
      "#else\n"
      "int steps=limit;\n"
      "#endif\n"
      "float h=0.5019607843137255;\n#if MODE != 0\nh=textureLod(tex,vec2(0.253,0.5),0.0).a;\n#endif\n"
      "float height=1.0;float s=1.0/float(steps);float t=s;vec2 coord=vec2(0.253,0.5);vec2 move=vec2(0.16,0)/float(steps);int count=0;\n"
      "if(steps>0){for(int i=0;i<steps;++i){height-=t;t=(h<height)?s:0.0;coord+=(h<height)?move:vec2(0);\n"
      "#if MODE == 1 || MODE == 3\n"
      "h=textureLod(tex,coord,0.0).a;\n"
      "#elif MODE == 2\n"
      "h=texture(tex,coord).a;\n"
      "#endif\n"
      "count++;}\n"
      "float hp=h;\n"
      "#if MODE != 0\n"
      "hp=textureLod(tex,coord-move,0.0).a;\n"
      "#endif\n"
      "coord-=move*((h-height)/(s+h-hp));}\n"
      "color=vec4(float(count)/32.0,height,coord.x,h);}";
   char fs[4096];
   /* Reusing the float after f2i exposes a destructive source hint in the
    * native cast encoder. The tiny fractional part preserves the same
    * analytical loop count while preventing an integer-only expression. */
   snprintf(fs, sizeof(fs),
            "#version 300 es\n#define MODE %u\n#define FLOAT_BOUND %u\n%s",
            mode, getenv("M4_TEST_UNSIGNED_BOUND") ? 2 :
                     (getenv("M4_TEST_FLOAT_BOUND") ? 1 : 0), body);
   const char *sources[] = {vs, fs};
   GLenum types[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
   GLuint p = glCreateProgram();
   for (unsigned i = 0; i < 2; i++) {
      GLuint sh = glCreateShader(types[i]);
      glShaderSource(sh, 1, &sources[i], NULL);
      glCompileShader(sh);
      GLint ok;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (!ok) {
         char log[8192];
         glGetShaderInfoLog(sh, sizeof(log), NULL, log);
         fprintf(stderr, "%s\n%s\n", sources[i], log);
      }
      CHECK(ok);
      glAttachShader(p, sh);
      glDeleteShader(sh);
   }
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   return p;
}
static unsigned
channel(float f)
{
   return (unsigned)lroundf(fmaxf(0, fminf(1, f)) * 255);
}
int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_ES_API));
   EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *r = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", r);
   fflush(stdout);
   CHECK((strstr(r, "zink") && strstr(r, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(r, "llvmpipe")));
   GLuint fb, output, input, vbo;
   glGenTextures(1, &output);
   glBindTexture(GL_TEXTURE_2D, output);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          output, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glGenTextures(1, &input);
   glBindTexture(GL_TEXTURE_2D, input);
   unsigned char sample[64 * 4] = {0};
   for (unsigned i = 0; i < 64; i++)
      sample[i * 4 + 3] = i * 4;
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                sample);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   const float vertices[] = {-1, -1, 3, -1, -1, 3};
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glViewport(0, 0, W, H);
   const int limits[] = {0, 1, 2, 4, 8, 16, 19};
   unsigned total_bad = 0;
   for (unsigned mode = 0; mode < 4; mode++) {
      GLuint p = program(mode);
      glUseProgram(p);
      glUniform1i(glGetUniformLocation(p, "tex"), 0);
      for (unsigned j = 0; j < sizeof(limits) / sizeof(limits[0]); j++) {
         int limit = limits[j];
         glUniform1i(glGetUniformLocation(p, "limit"), limit);
         glUniform1f(glGetUniformLocation(p, "float_limit"), limit);
         glClearColor(1, 0, 1, 1);
         glClear(GL_COLOR_BUFFER_BIT);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         unsigned char pixels[W * H * 4];
         memset(pixels, 0xa5, sizeof(pixels));
         glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         CHECK(glGetError() == GL_NO_ERROR);
         unsigned bad = 0;
         for (unsigned y = 0; y < H; y++)
            for (unsigned x = 0; x < W; x++) {
               int steps = (mode == 0 || mode == 3) && (int)x - 8 < limit
                              ? (int)x - 8
                              : limit;
               float h = mode == 0 ? 128.0f / 255 : 64.0f / 255, height = 1,
                     coord = .253f;
               if (steps > 0) {
                  float s = 1.0f / steps, t = s, move = .16f / steps;
                  for (int k = 0; k < steps; k++) {
                     height -= t;
                     t = h < height ? s : 0;
                     if (h < height)
                        coord += move;
                     if (mode != 0)
                        h = (floorf(coord * 64) * 4) / 255;
                  }
                  float hp =
                     mode == 0 ? h : (floorf((coord - move) * 64) * 4) / 255;
                  coord -= move * ((h - height) / (s + h - hp));
               }
               unsigned expect[] = {channel((steps > 0 ? steps : 0) / 32.0f),
                                    channel(height), channel(coord),
                                    channel(h)};
               const unsigned char *q = &pixels[(y * W + x) * 4];
               bool wrong = false;
               for (unsigned k = 0; k < 4; k++)
                  wrong |= abs((int)q[k] - (int)expect[k]) > 2;
               if (wrong) {
                  if (bad < 3)
                     fprintf(
                        stderr,
                        "mode=%u limit=%d pixel=%u,%u got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                        mode, limit, x, y, q[0], q[1], q[2], q[3], expect[0],
                        expect[1], expect[2], expect[3]);
                  bad++;
               }
            }
         printf("mode=%u limit=%d bad=%u/%u\n", mode, limit, bad, W * H);
         fflush(stdout);
         total_bad += bad;
      }
      glDeleteProgram(p);
   }
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   printf("%s: fragment loops, 28672 pixels, %u incorrect\n",
          total_bad ? "FAIL" : "PASS", total_bad);
   return total_bad ? 1 : 0;
}
