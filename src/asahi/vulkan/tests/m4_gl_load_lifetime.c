/* SPDX-License-Identifier: MIT
 * Exercise asynchronous uniform loads and reused physical address registers.
 * A matrix transform, tangent frame and fog plane keep scalar loads live.
 * Run M4 only under the mandatory watchdog and thermal guards.
 */
#define GL_GLEXT_PROTOTYPES
#include <math.h>
#include <stdbool.h>
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
         fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x,           \
                 eglGetError(), glGetError());                                 \
         exit(1);                                                              \
      }                                                                        \
   } while (0)
#define W 128
static GLuint
shader(GLenum stage, const char *source)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[8192];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n%s\n", source, log);
   }
   CHECK(ok);
   return s;
}
int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_API));
   EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION,
                     3,
                     EGL_CONTEXT_MINOR_VERSION,
                     3,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK,
                     EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
                     EGL_NONE};
   EGLContext ctx =
      eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(ctx != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
   const char *renderer = (void *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   fflush(stdout);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLuint fb, target, vbo;
   glGenTextures(1, &target);
   glBindTexture(GL_TEXTURE_2D, target);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, W);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          target, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

   const float positions[6][2] = {{-1, -1}, {1, -1}, {-1, 1},
                                  {1, -1},  {1, 1},  {-1, 1}};
   float vertices[6][16] = {0};
   for (unsigned v = 0; v < 6; v++) {
      const float x = positions[v][0], y = positions[v][1];
      const float w = 16 + (x + 1) * 120 + (y + 1) * 248;
      float *a = vertices[v];
      a[0] = x * w;
      a[1] = y * w;
      a[2] = 0;
      a[3] = w;
      a[4] = .2f + x * .05f;
      a[5] = .3f + y * .07f;
      a[6] = 8192 + x * 1024;
      a[7] = 4096 + y * 512;
      a[8] = 0;
      a[9] = 0;
      a[10] = 1;
      a[12] = 1;
      a[13] = 0;
      a[14] = 0;
      a[15] = 1;
   }
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   const unsigned sizes[] = {4, 2, 2, 3, 4}, offsets[] = {0, 4, 6, 8, 12};
   for (unsigned i = 0; i < 5; i++) {
      glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE,
                            sizeof(vertices[0]),
                            (void *)(size_t)(offsets[i] * sizeof(float)));
      glEnableVertexAttribArray(i);
   }
   const char *vs =
      "#version 330\n"
      "layout(location=0)in vec4 p;layout(location=1)in vec2 uv;"
      "layout(location=2)in vec2 uv2;layout(location=3)in vec3 n;"
      "layout(location=4)in vec4 t;uniform mat4 matrix;uniform vec2 scroll;"
      "uniform vec3 camera;uniform vec4 plane;out vec2 a,b;out vec3 c;"
      "out float fog;void main(){gl_Position=matrix*p;"
      "a=uv+scroll;b=uv2*(1.0/32768.0);vec3 q=camera-p.xyz;"
      "c=vec3(dot(q,t.xyz),dot(q,cross(n,t.xyz)*t.w),dot(q,n));"
      "fog=dot(plane,gl_Position);}";
   const char *fs =
      "#version 330\nin vec2 a,b;in vec3 c;in float fog;uniform vec2 fp;"
      "out vec4 color;void main(){color=vec4(clamp(fog*fp.x+fp.y,0.,1.),"
      "a.x+a.y*.37+b.x*.25+b.y*.125,"
      "dot(normalize(c),vec3(.25,.125,.0625))+.5,1);}";
   GLuint p = glCreateProgram(), v = shader(GL_VERTEX_SHADER, vs),
          f = shader(GL_FRAGMENT_SHADER, fs);
   glAttachShader(p, v);
   glAttachShader(p, f);
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(p);
   const float matrix[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
   glUniformMatrix4fv(glGetUniformLocation(p, "matrix"), 1, GL_FALSE, matrix);
   glUniform2f(glGetUniformLocation(p, "scroll"), 0, 0);
   glUniform3f(glGetUniformLocation(p, "camera"), 4, 8, 16);
   glUniform4f(glGetUniformLocation(p, "plane"), 0, 0, 0, -1);
   glViewport(0, 0, W, W);
   unsigned total_bad = 0, max_error = 0;
   const unsigned repeats = 24;
   for (unsigned repeat = 0; repeat < repeats; repeat++) {
      /* Alternate real uniform data; no warmup/readback result is discarded. */
      float scale = repeat & 1 ? .0013f : .001f;
      float bias = repeat & 1 ? .9f : 1.f;
      glUniform2f(glGetUniformLocation(p, "fp"), scale, bias);
      glClearColor(1, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      unsigned char pixels[W * W * 4];
      memset(pixels, 0x5a, sizeof(pixels));
      glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned bad = 0;
      for (unsigned y = 0; y < W; y++)
         for (unsigned x = 0; x < W; x++) {
            const double sx = (x + .5) / W, sy = (y + .5) / W;
            unsigned base = sx + sy <= 1 ? 0 : 3;
            double weights[3];
            if (!base) {
               weights[0] = 1 - sx - sy;
               weights[1] = sx;
               weights[2] = sy;
            } else {
               weights[0] = 1 - sy;
               weights[1] = sx + sy - 1;
               weights[2] = 1 - sx;
            }
            double den = 0, data[8] = {0};
            for (unsigned i = 0; i < 3; i++) {
               const float *a = vertices[base + i];
               const double q = weights[i] / a[3];
               den += q;
               for (unsigned j = 0; j < 8; j++)
                  data[j] += q * a[j];
            }
            for (unsigned j = 0; j < 8; j++)
               data[j] /= den;
            double cx = 4 - data[0], cy = 8 - data[1], cz = 16 - data[2];
            double length = sqrt(cx * cx + cy * cy + cz * cz);
            double expected[] = {
               fmax(0, fmin(1, -data[3] * scale + bias)),
               data[4] + data[5] * .37 + data[6] / 32768 * .25 +
                  data[7] / 32768 * .125,
               .5 + (cx * .25 + cy * .125 + cz * .0625) / length, 1};
            bool wrong = false;
            for (unsigned c = 0; c < 4; c++) {
               int want = lround(expected[c] * 255),
                   got = pixels[(y * W + x) * 4 + c];
               unsigned error = abs(got - want);
               if (error > max_error)
                  max_error = error;
               wrong |= error > 2;
            }
            bad += wrong;
         }
      total_bad += bad;
      printf("repeat=%u bad=%u/%u max_error=%u\n", repeat, bad, W * W,
             max_error);
   }
   printf("%s: load lifetime pixels=%u incorrect=%u max_error=%u\n",
          total_bad ? "FAIL" : "PASS", repeats * W * W, total_bad, max_error);
   return total_bad ? 1 : 0;
}
