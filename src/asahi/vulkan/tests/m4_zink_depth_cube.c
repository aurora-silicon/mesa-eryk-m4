/* SPDX-License-Identifier: MIT
 * Real Zink rendering checks for depth ordering, partial depth preservation,
 * and all six cubemap faces. Run inside the J713 watchdog/thermal guard.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x, \
           eglGetError(), glGetError()); exit(1); } } while (0)

static GLuint
shader(GLenum stage, const char *source)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[4096];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n", log);
   }
   CHECK(ok);
   return s;
}

static GLuint
program(const char *fs)
{
   const char *vs = "#version 300 es\nlayout(location=0) in vec2 pos; "
      "uniform float z; void main(){gl_Position=vec4(pos,z,1);}";
   GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
   GLuint p = glCreateProgram();
   glAttachShader(p, v);
   glAttachShader(p, f);
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glDeleteShader(v);
   glDeleteShader(f);
   return p;
}

static void
verify(const char *label, unsigned test, const uint8_t expected[4])
{
   uint8_t pixels[32 * 32][4];
   glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   CHECK(glGetError() == GL_NO_ERROR);
   for (unsigned i = 0; i < 32 * 32; ++i) {
      if (memcmp(pixels[i], expected, 4)) {
         fprintf(stderr, "FAIL %s test=%u pixel=%u got=%u,%u,%u,%u "
                 "expected=%u,%u,%u,%u\n", label, test, i,
                 pixels[i][0], pixels[i][1], pixels[i][2], pixels[i][3],
                 expected[0], expected[1], expected[2], expected[3]);
         exit(1);
      }
   }
   printf("PASS: %s test=%u, 1024 exact pixels\n", label, test);
   fflush(stdout);
}

int
main(int argc, char **argv)
{
   const int array_face = argc == 2 && !strcmp(argv[1], "--array-face");
   const int array = array_face || (argc == 2 && !strcmp(argv[1], "--array"));
   const int direction = argc == 2 && !strcmp(argv[1], "--direction");
   const int face_math = argc == 2 && !strcmp(argv[1], "--face-math");
   const int cube_grid = argc == 2 && !strcmp(argv[1], "--cube-grid");
   CHECK(argc == 1 || array || direction || face_math || cube_grid);
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                       EGL_DEFAULT_DISPLAY, NULL);
   CHECK(d != EGL_NO_DISPLAY && eglInitialize(d, NULL, NULL) &&
         eglBindAPI(EGL_OPENGL_ES_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   fprintf(stderr, "renderer: %s\n", renderer);
   CHECK(renderer && strstr(renderer, "zink") && strstr(renderer, "M4") &&
         !strstr(renderer, "llvmpipe"));
   GLuint flat = program("#version 300 es\nprecision highp float; "
      "uniform vec4 colour; out vec4 result; void main(){result=colour;}");
   GLuint cube = program(array_face ?
      "#version 300 es\nprecision highp float; precision highp int; "
      "uniform highp sampler2DArray tex; uniform vec3 direction; out vec4 result; void main(){"
      "vec3 q=direction*direction; "
      "uint fx=direction.x>=0.0?0u:1u, fy=direction.y>=0.0?2u:3u, fz=direction.z>=0.0?4u:5u; "
      "uint f=q.x>=q.y && q.x>=q.z ? fx : (q.y>=q.x && q.y>=q.z ? fy : fz); "
      "result=texture(tex,vec3(0.5,0.5,float(f)));}" : face_math ?
      "#version 300 es\nprecision highp float; precision highp int; "
      "uniform vec3 direction; out vec4 result; void main(){"
      "vec3 q=direction*direction; "
      "uint fx=direction.x>=0.0?0u:1u, fy=direction.y>=0.0?2u:3u, fz=direction.z>=0.0?4u:5u; "
      "uint f=q.x>=q.y && q.x>=q.z ? fx : (q.y>=q.x && q.y>=q.z ? fy : fz); "
      "result=vec4(float(f)/255.0,0,0,1);}" : direction ?
      "#version 300 es\nprecision highp float; "
      "uniform vec3 direction; out vec4 result; "
      "void main(){result=vec4(abs(direction),1);}" : array ?
      "#version 300 es\nprecision highp float; "
      "uniform highp sampler2DArray tex; uniform vec3 direction; out vec4 result; "
      "void main(){result=texture(tex,vec3(0.5,0.5,direction.x));}" :
      "#version 300 es\nprecision highp float; "
      "uniform highp samplerCube tex; uniform vec3 direction; out vec4 result; "
      "void main(){result=texture(tex,direction);}");
   GLuint vao, vbo, fbo, color, depth, texture;
   const float quad[] = {-1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1};
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glGenTextures(1, &color);
   glBindTexture(GL_TEXTURE_2D, color);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 32, 32);
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 32, 32);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 32, 32);
   glDisable(GL_DITHER);
   glUseProgram(flat);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDepthMask(GL_TRUE);
   glClearDepthf(1);
   const uint8_t green[] = {0,255,0,255};
   for (unsigned pressure = 0; pressure < 2; ++pressure) {
      glClearColor(1, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
      glUniform1f(glGetUniformLocation(flat, "z"), -0.5f);
      glUniform4f(glGetUniformLocation(flat, "colour"), 0, 1, 0, 1);
      glDrawArraysInstanced(GL_TRIANGLES, 0, 6, pressure ? 65536 : 1);
      glUniform1f(glGetUniformLocation(flat, "z"), 0.5f);
      glUniform4f(glGetUniformLocation(flat, "colour"), 0, 0, 1, 1);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      verify("depth ordering / partial preservation", pressure, green);
   }
   glDisable(GL_DEPTH_TEST);
   glUseProgram(cube);
   glGenTextures(1, &texture);
   GLenum target = array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_CUBE_MAP;
   glBindTexture(target, texture);
   if (array)
      glTexImage3D(target, 0, GL_RGBA8, 4, 4, 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   const uint8_t faces[6][4] = {
      {255,0,0,255}, {0,255,0,255}, {0,0,255,255},
      {255,255,0,255}, {0,255,255,255}, {255,0,255,255},
   };
   const float directions[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
   for (unsigned face = 0; face < 6; ++face) {
      uint8_t pixels[4 * 4][4];
      for (unsigned i = 0; i < 16; ++i) {
         memcpy(pixels[i], faces[face], 4);
         if (cube_grid) {
            pixels[i][0] = 32 + (i % 4) * 64;
            pixels[i][1] = 32 + (i / 4) * 64;
            pixels[i][2] = 16 + face * 40;
         }
      }
      if (array)
         glTexSubImage3D(target, 0, 0, 0, face, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      else
         glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8, 4, 4,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   }
   glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glUniform1i(glGetUniformLocation(cube, "tex"), 0);
   for (unsigned face = 0; face < 6; ++face) {
      if (cube_grid) {
         for (unsigned y = 0; y < 4; ++y) {
            for (unsigned x = 0; x < 4; ++x) {
               float sc = (x + 0.5f) / 2.0f - 1.0f;
               float tc = (y + 0.5f) / 2.0f - 1.0f;
               const float rays[6][3] = {
                  {1,-tc,-sc}, {-1,-tc,sc}, {sc,1,tc},
                  {sc,-1,-tc}, {sc,-tc,1}, {-sc,-tc,-1},
               };
               const uint8_t expected[4] = {32 + x*64,32 + y*64,16 + face*40,255};
               glClearColor(0, 0, 0, 0);
               glClear(GL_COLOR_BUFFER_BIT);
               glUniform3fv(glGetUniformLocation(cube, "direction"), 1, rays[face]);
               glDrawArrays(GL_TRIANGLES, 0, 6);
               verify("cubemap face texel", face*16 + y*4 + x, expected);
            }
         }
         continue;
      }
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      if (array && !array_face)
         glUniform3f(glGetUniformLocation(cube, "direction"), face, 0, 0);
      else
         glUniform3fv(glGetUniformLocation(cube, "direction"), 1, directions[face]);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      const uint8_t axes[3][4] = {{255,0,0,255},{0,255,0,255},{0,0,255,255}};
      const uint8_t face_value[4] = {face,0,0,255};
      verify(face_math ? "face selection math" : direction ? "direction uniform" : array ? "array layer" : "cubemap face",
             face, face_math ? face_value : direction ? axes[face / 2] : faces[face]);
   }
   glDeleteTextures(1, &texture);
   glDeleteTextures(1, &color);
   glDeleteRenderbuffers(1, &depth);
   glDeleteFramebuffers(1, &fbo);
   glDeleteBuffers(1, &vbo);
   glDeleteVertexArrays(1, &vao);
   glDeleteProgram(flat);
   glDeleteProgram(cube);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d, c) && eglTerminate(d));
   return 0;
}
