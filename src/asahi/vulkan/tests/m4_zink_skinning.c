/* SPDX-License-Identifier: MIT
 * Combined packed attributes, weighted skinning matrices and instance rotation.
 * Run inside the J713 watchdog/thermal guard.
 */
#include <math.h>
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

static void
cross3(const float *a, const float *b, float *r)
{
   r[0] = a[1] * b[2] - a[2] * b[1];
   r[1] = a[2] * b[0] - a[0] * b[2];
   r[2] = a[0] * b[1] - a[1] * b[0];
}
static void
rotate(float *v, const float *q)
{
   float a[3], b[3];
   cross3(v, q, a);
   for (unsigned i = 0; i < 3; i++)
      a[i] += q[3] * v[i];
   cross3(a, q, b);
   for (unsigned i = 0; i < 3; i++)
      v[i] += 2 * b[i];
}
static const char *skinning_vs =
   "#version 300 es\n"
   "precision highp float; precision highp int;\n"
   "layout(location=13) in vec2 grid;\n"
   "layout(location=0) in vec3 position;\n"
   "layout(location=1) in vec4 normal;\n"
   "layout(location=2) in vec4 colour;\n"
   "layout(location=3) in vec2 uv;\n"
   "layout(location=4) in vec2 uv2;\n"
   "layout(location=5) in vec4 tangent;\n"
   "layout(location=6) in ivec4 joints;\n"
   "layout(location=7) in vec4 weights;\n"
   "layout(location=8) in vec3 origin;\n"
   "layout(location=9) in vec4 quat;\n"
   "layout(location=10) in vec4 scale;\n"
   "layout(location=11) in vec2 trans;\n"
   "layout(location=12) in ivec2 misc;\n"
   "uniform highp sampler2D skin; uniform int stage;\n"
   "out vec4 a;out vec4 b;out vec4 c;out vec4 d;out vec4 e;out vec4 f;out vec4 g;\n"
   "vec3 rotate(vec3 v) {return v+2.0*cross(cross(v,quat.xyz)+quat.w*v,quat.xyz);}\n"
   "mat4 bone(int j) {\n"
   " int row=clamp(j+misc.x,0,2048);\n"
   " return mat4(texelFetch(skin,ivec2(0,row),0),texelFetch(skin,ivec2(1,row),0),\n"
   "             texelFetch(skin,ivec2(2,row),0),texelFetch(skin,ivec2(3,row),0));\n"
   "}\n"
   "void main() {\n"
   " mat4 m=weights.x*bone(joints.x)+weights.y*bone(joints.y)+weights.z*bone(joints.z)+weights.w*bone(joints.w);\n"
   " vec3 p=(m*vec4(position,1)).xyz;\n"
   " a=vec4(rotate(p*scale.xyz)+origin,1);\n"
   " b=vec4(rotate((m*vec4(normal.xyz,0)).xyz),0);\n"
   " c=vec4(rotate((m*vec4(tangent.xyz,0)).xyz),0);\n"
   " d=vec4(cross(b.xyz,c.xyz)*tangent.w,0);\n"
   " e=vec4(uv+trans*normal.w,uv2);f=colour.zyxw;\n"
   " g=vec4(length(a),float(misc.y)*0.01,0,0);\n"
   " vec2 pxy=grid+vec2(float(gl_InstanceID)*0.5,0);\n"
   " if(stage==1){a=vec4(m[0].xyz,0);b=vec4(m[1].xyz,0);c=vec4(m[2].xyz,0);d=vec4(m[3].xyz,0); }\n"
   " gl_Position=vec4(pxy,0,1);\n"
   "}\n";

int
main(int argc, char **argv)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(d != EGL_NO_DISPLAY && eglInitialize(d, NULL, NULL) &&
         eglBindAPI(EGL_OPENGL_ES_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT &&
         eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   fprintf(stderr, "renderer: %s\n", renderer);
   bool reference = false, identity_rotation = false, identity_bones = false,
        matrices = false;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--reference"))
         reference = true;
      else if (!strcmp(argv[i], "--identity-rotation"))
         identity_rotation = true;
      else if (!strcmp(argv[i], "--identity-bones"))
         identity_bones = true;
      else if (!strcmp(argv[i], "--matrices"))
         matrices = true;
      else
         CHECK(false);
   }
   CHECK(renderer &&
         (reference ? strstr(renderer, "llvmpipe") != NULL
                    : strstr(renderer, "zink") && strstr(renderer, "M4") &&
                         !strstr(renderer, "llvmpipe")));
   GLuint vao, vbo, instances, fbo, color;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   uint8_t vertices[24][48] = {0};
   const float pos[4][3] = {{-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, 1, 0}};
   const uint32_t normal =
      (511u) | ((uint32_t)(-511 & 1023) << 10) | (1u << 30);
   const uint32_t tangent =
      ((uint32_t)(-512 & 1023)) | (511u << 20) | (3u << 30);
   const uint8_t colour[4] = {255, 0, 255, 255};
   const uint16_t uv[2] = {0x3800, 0xbc00}, uv2[2] = {0x4000, 0x3400};
   const int16_t joints[4] = {-32768, -1, 3, 32767};
   const uint16_t weights[4] = {0x3800, 0x3400, 0x3400, 0};
   for (unsigned i = 0; i < 24; i++) {
      memcpy(vertices[i], pos[i / 6], 12);
      memcpy(vertices[i] + 12, &normal, 4);
      memcpy(vertices[i] + 16, colour, 4);
      memcpy(vertices[i] + 20, uv, 4);
      memcpy(vertices[i] + 24, uv2, 4);
      memcpy(vertices[i] + 28, &tangent, 4);
      memcpy(vertices[i] + 32, joints, 8);
      memcpy(vertices[i] + 40, weights, 8);
   }
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   const GLenum types[] = {
      GL_FLOAT,      GL_INT_2_10_10_10_REV, GL_UNSIGNED_BYTE, GL_HALF_FLOAT,
      GL_HALF_FLOAT, GL_INT_2_10_10_10_REV, GL_SHORT,         GL_HALF_FLOAT};
   const unsigned sizes[] = {3, 4, 4, 2, 2, 4, 4, 4},
                  offsets[] = {0, 12, 16, 20, 24, 28, 32, 40};
   for (unsigned i = 0; i < 8; i++) {
      if (i == 6)
         glVertexAttribIPointer(i, sizes[i], types[i], 48,
                                (void *)(uintptr_t)offsets[i]);
      else
         glVertexAttribPointer(i, sizes[i], types[i],
                               i == 1 || i == 2 || i == 5, 48,
                               (void *)(uintptr_t)offsets[i]);
      glEnableVertexAttribArray(i);
   }
   GLuint grid_buffer;
   glGenBuffers(1, &grid_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, grid_buffer);
   float grid[24][2];
   const float corners[6][2] = {{-1, -1}, {1, -1}, {-1, 1},
                                {-1, 1},  {1, -1}, {1, 1}};
   for (unsigned i = 0; i < 24; i++) {
      grid[i][0] = corners[i % 6][0] * 0.25f - 0.75f;
      grid[i][1] = corners[i % 6][1] * 0.25f + (float)(i / 6) * 0.5f - 0.75f;
   }
   glBufferData(GL_ARRAY_BUFFER, sizeof(grid), grid, GL_STATIC_DRAW);
   glVertexAttribPointer(13, 2, GL_FLOAT, GL_FALSE, 8, NULL);
   glEnableVertexAttribArray(13);
   uint8_t data[4][44] = {0};
   const uint16_t scales[4][4] = {{0x3c00, 0x3800, 0xb400, 0},
                                  {0x3c00, 0x3800, 0xb400, 0x3c00},
                                  {0x3c00, 0x3800, 0xb400, 0x4000},
                                  {0x3c00, 0x3800, 0xb400, 0x4200}};
   for (unsigned i = 0; i < 4; i++) {
      float origin[3] = {(float)i + 1, -2, 0.25},
            rotation[4] = {0.5, -0.5, 0.5, 0.5};
      if (identity_rotation) {
         rotation[0] = rotation[1] = rotation[2] = 0;
         rotation[3] = 1;
      }
      int16_t trans[2] = {-32768, 32767}, misc[2] = {-1 - (int)i, 17 + (int)i};
      memcpy(data[i], origin, 12);
      memcpy(data[i] + 12, rotation, 16);
      memcpy(data[i] + 28, scales[i], 8);
      memcpy(data[i] + 36, trans, 4);
      memcpy(data[i] + 40, misc, 4);
   }
   glGenBuffers(1, &instances);
   glBindBuffer(GL_ARRAY_BUFFER, instances);
   glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
   const GLenum itypes[] = {GL_FLOAT, GL_FLOAT, GL_HALF_FLOAT, GL_SHORT,
                            GL_SHORT};
   const unsigned isizes[] = {3, 4, 4, 2, 2}, ioffsets[] = {0, 12, 28, 36, 40};
   for (unsigned i = 0; i < 5; i++) {
      if (i == 4)
         glVertexAttribIPointer(8 + i, isizes[i], itypes[i], 44,
                                (void *)(uintptr_t)ioffsets[i]);
      else
         glVertexAttribPointer(8 + i, isizes[i], itypes[i], i == 3, 44,
                               (void *)(uintptr_t)ioffsets[i]);
      glEnableVertexAttribArray(8 + i);
      glVertexAttribDivisor(8 + i, 1);
   }
   glGenTextures(1, &color);
   glBindTexture(GL_TEXTURE_2D, color);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          color, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 4, 4);
   glDisable(GL_DITHER);
   float bones[2049][4][4];
   for (unsigned row = 0; row < 2049; row++)
      for (unsigned col = 0; col < 4; col++)
         for (unsigned j = 0; j < 4; j++)
            bones[row][col][j] =
               (col == j ? 1.0f : 0.0f) +
               (identity_bones
                   ? 0.0f
                   : (float)((int)((row * 3 + col * 5 + j * 7) % 17) - 8) *
                        0.03125f);
   GLuint skin;
   glGenTextures(1, &skin);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, skin);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, 4, 2049);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 2049, GL_RGBA, GL_FLOAT, bones);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   GLuint vs = shader(GL_VERTEX_SHADER, skinning_vs);
   GLuint fs = shader(
      GL_FRAGMENT_SHADER,
      "#version 300 es\nprecision highp float; precision highp int; "
      "in vec4 a; in vec4 b; in vec4 c; in vec4 d; in vec4 e; in vec4 f; in vec4 g; "
      "uniform int mode; uniform int component; out vec4 result; void main(){ "
      "vec4 value=mode==0?a:mode==1?b:mode==2?c:mode==3?d:mode==4?e:mode==5?f:g; "
      "uint bits=floatBitsToUint(value[component]); "
      "result=vec4(uvec4(bits,bits>>8,bits>>16,bits>>24)&255u)/255.0;}");
   GLuint program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint ok;
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(program);
   glUniform1i(glGetUniformLocation(program, "skin"), 0);
   glUniform1i(glGetUniformLocation(program, "stage"), matrices ? 1 : 0);
   unsigned failures = 0;
   for (unsigned mode = 0; mode < 7; mode++) {
      glUniform1i(glGetUniformLocation(program, "mode"), mode);
      float pixels[4][4][4];
      for (unsigned component = 0; component < 4; component++) {
         glUniform1i(glGetUniformLocation(program, "component"), component);
         glClearColor(0, 0, 0, 0);
         glClear(GL_COLOR_BUFFER_BIT);
         glDrawArraysInstanced(GL_TRIANGLES, 0, 24, 4);
         uint8_t raw[4][4][4];
         glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, raw);
         CHECK(glGetError() == GL_NO_ERROR);
         for (unsigned y = 0; y < 4; y++)
            for (unsigned x = 0; x < 4; x++) {
               uint32_t bits = (uint32_t)raw[y][x][0] |
                               ((uint32_t)raw[y][x][1] << 8) |
                               ((uint32_t)raw[y][x][2] << 16) |
                               ((uint32_t)raw[y][x][3] << 24);
               memcpy(&pixels[y][x][component], &bits, 4);
            }
      }
      unsigned bad = 0;
      for (unsigned vertex = 0; vertex < 4; vertex++)
         for (unsigned inst = 0; inst < 4; inst++) {
            float matrix[4][4] = {0};
            for (unsigned k = 0; k < 4; k++) {
               int row = joints[k] - 1 - (int)inst;
               row = row < 0 ? 0 : row > 2048 ? 2048 : row;
               float weight = k == 0 ? 0.5f : k == 3 ? 0 : 0.25f;
               for (unsigned col = 0; col < 4; col++)
                  for (unsigned j = 0; j < 4; j++)
                     matrix[col][j] += weight * bones[row][col][j];
            }
            float local[4] = {pos[vertex][0], pos[vertex][1], pos[vertex][2],
                              1};
            float sn[4] = {1, -1, 0, 0}, st[4] = {-1, 0, 1, 0};
            float wp[4] = {0}, wn[4] = {0}, wt[4] = {0};
            for (unsigned col = 0; col < 4; col++)
               for (unsigned j = 0; j < 4; j++) {
                  wp[j] += matrix[col][j] * local[col];
                  wn[j] += matrix[col][j] * sn[col];
                  wt[j] += matrix[col][j] * st[col];
               }
            wp[1] *= 0.5f;
            wp[2] *= -0.25f;
            float q[4] = {0.5f, -0.5f, 0.5f, 0.5f};
            if (identity_rotation) {
               q[0] = q[1] = q[2] = 0;
               q[3] = 1;
            }
            rotate(wp, q);
            rotate(wn, q);
            rotate(wt, q);
            wp[0] += (float)inst + 1;
            wp[1] -= 2;
            wp[2] += 0.25f;
            wp[3] = 1;
            float bt[4];
            cross3(wn, wt, bt);
            for (unsigned j = 0; j < 3; j++)
               bt[j] = -bt[j];
            float expected[7][4];
            memcpy(expected[0], wp, 16);
            for (unsigned j = 0; j < 3; j++) {
               expected[1][j] = wn[j];
               expected[2][j] = wt[j];
               expected[3][j] = bt[j];
            }
            expected[1][3] = expected[2][3] = expected[3][3] = 0;
            const float uvout[4] = {-0.5f, 0, 2, 0.25f};
            memcpy(expected[4], uvout, 16);
            const float cout[4] = {1, 0, 1, 1};
            memcpy(expected[5], cout, 16);
            expected[6][0] =
               sqrtf(wp[0] * wp[0] + wp[1] * wp[1] + wp[2] * wp[2] + 1);
            expected[6][1] = (17 + (float)inst) * 0.01f;
            expected[6][2] = expected[6][3] = 0;
            if (matrices && mode < 4) {
               for (unsigned j = 0; j < 3; j++)
                  expected[mode][j] = matrix[mode][j];
               expected[mode][3] = 0;
            }
            for (unsigned j = 0; j < 4; j++)
               if (!isfinite(pixels[vertex][inst][j]) ||
                   fabsf(pixels[vertex][inst][j] - expected[mode][j]) >
                      0.0002f) {
                  if (bad < 8)
                     fprintf(
                        stderr,
                        "FAIL mode=%u vertex=%u instance=%u channel=%u got=%.9g expected=%.9g\n",
                        mode, vertex, inst, j, pixels[vertex][inst][j],
                        expected[mode][j]);
                  bad++;
               }
         }
      fprintf(stderr, "skinning mode %u: 64 floats checked, %u failures\n",
              mode, bad);
      failures += bad;
   }
   glDeleteTextures(1, &skin);
   glDeleteProgram(program);
   glDeleteShader(vs);
   glDeleteShader(fs);
   glDeleteFramebuffers(1, &fbo);
   glDeleteTextures(1, &color);
   glDeleteBuffers(1, &grid_buffer);
   glDeleteBuffers(1, &vbo);
   glDeleteBuffers(1, &instances);
   glDeleteVertexArrays(1, &vao);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d, c) && eglTerminate(d));
   return failures ? 1 : 0;
}
