/* SPDX-License-Identifier: MIT
 * Exact cube projection + explicit LOD in compute through Zink.
 * Execute hardware only through gpu-experiment.py watchdog/thermal guards.
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
static float pixel(unsigned width, unsigned size, unsigned layer, unsigned face,
                   unsigned level, unsigned x, unsigned y, unsigned c,
                   int unorm) {
  if (!unorm)
    return layer * 16384 + face * 1024 + level * 512 + y * width + x +
           c * .125f;
  unsigned g = x + y + 6 * layer + face;
  unsigned rgba[4] = {255 - 255 * g / (2 * size + 12 - 3),
                      255 - ((g % 2 == 0 ? 127 : 0) + (g % 4 < 3 ? 128 : 0)),
                      128 - 64 * level, 255};
  return rgba[c] / 255.0f;
}
int main(void) {
  const int paired = getenv("M4_TEST_CUBE_PAIRED") != NULL;
  const int linear = getenv("M4_TEST_CUBE_LINEAR") != NULL;
  const int unorm = getenv("M4_TEST_CUBE_UNORM") != NULL;
  const unsigned width = getenv("M4_TEST_CUBE_64") ? 64 : 16;
  const int constants = getenv("M4_TEST_CUBE_CONSTANTS") != NULL;
  const unsigned layers = getenv("M4_TEST_CUBE_ARRAY") ? 2 : 1;
  const GLenum target =
      layers > 1 ? GL_TEXTURE_CUBE_MAP_ARRAY : GL_TEXTURE_CUBE_MAP;
  CHECK(!constants || layers > 1);
  CHECK(!linear || constants);
  CHECK(!paired || (constants && linear && layers > 1));
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
  EGLContext ctx =
      eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  CHECK(ctx != EGL_NO_CONTEXT &&
        eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s\n", renderer);
  fflush(stdout);
  CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
        (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
  if (getenv("M4_TEST_CUBE_SEAMLESS"))
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(target, tex);
  if (layers > 1)
    glTexStorage3D(target, 2, unorm ? GL_RGBA8 : GL_RGBA32F, width, width,
                   6 * layers);
  else
    glTexStorage2D(target, 2, unorm ? GL_RGBA8 : GL_RGBA32F, width, width);
  for (unsigned level = 0; level < 2; level++)
    for (unsigned layer = 0; layer < layers; layer++)
      for (unsigned face = 0; face < 6; face++) {
        unsigned size = width >> level;
        float data[size * size * 4];
        for (unsigned y = 0; y < size; y++)
          for (unsigned x = 0; x < size; x++)
            for (unsigned c = 0; c < 4; c++)
              data[(y * size + x) * 4 + c] =
                  pixel(width, size, layer, face, level, x, y, c, unorm);
        if (layers > 1)
          glTexSubImage3D(target, level, 0, 0, layer * 6 + face, size, size, 1,
                          GL_RGBA, GL_FLOAT, data);
        else
          glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, 0, 0,
                          size, size, GL_RGBA, GL_FLOAT, data);
      }
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                  linear ? GL_LINEAR_MIPMAP_NEAREST
                         : GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER,
                  linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 1);
  if (paired) {
    GLuint samplers[2];
    glGenSamplers(2, samplers);
    for (unsigned i = 0; i < 2; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(target, tex);
      glSamplerParameteri(samplers[i], GL_TEXTURE_MIN_FILTER,
                          i ? GL_NEAREST_MIPMAP_NEAREST
                            : GL_LINEAR_MIPMAP_NEAREST);
      glSamplerParameteri(samplers[i], GL_TEXTURE_MAG_FILTER,
                          i ? GL_NEAREST : GL_LINEAR);
      glBindSampler(i, samplers[i]);
    }
  }
  float directions[96 * layers][4], actual[96 * layers][4];
  for (unsigned layer = 0; layer < layers; layer++)
    for (unsigned face = 0; face < 6; face++)
      for (unsigned y = 0; y < 4; y++)
        for (unsigned x = 0; x < 4; x++) {
          float s = (x + .5f) * .5f - 1, t = (y + .5f) * .5f - 1;
          const float v[6][4] = {{1, -t, -s, 0}, {-1, -t, s, 0},
                                 {s, 1, t, 0},   {s, -1, -t, 0},
                                 {s, -t, 1, 0},  {-s, -t, -1, 0}};
          memcpy(directions[layer * 96 + face * 16 + y * 4 + x], v[face],
                 sizeof(v[0]));
          directions[layer * 96 + face * 16 + y * 4 + x][3] = layer;
        }
  GLuint buffers[2];
  glGenBuffers(2, buffers);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(directions), directions,
               GL_STATIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(actual), NULL, GL_DYNAMIC_READ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
  unsigned bad = 0, checked = 0;
  for (unsigned level = 0; level < 2; level++) {
    char source[4096];
    snprintf(
        source, sizeof(source),
        "#version 430 core\nlayout(local_size_x=32) in; "
        "layout(std430,binding=0) readonly buffer D{vec4 "
        "dirs[];};layout(std430,binding=1) writeonly buffer O{vec4 "
        "result[];};layout(binding=0) uniform samplerCube%s tex; void "
        "main(){uint "
        "i=gl_GlobalInvocationID.x;result[i]=textureLod(tex,dirs[i].%s,%u.0);}",
        layers > 1 ? "Array" : "", layers > 1 ? "xyzw" : "xyz", level);
    if (constants)
      snprintf(
          source, sizeof(source),
          "#version 430 core\nlayout(local_size_x=32) "
          "in;layout(std430,binding=1) writeonly buffer O{vec4 "
          "result[];};layout(binding=0) uniform samplerCubeArray tex;void "
          "main(){uint i=gl_GlobalInvocationID.x;vec4 "
          "v;if((i&3u)==0u)v=textureLod(tex,vec4(-0.976562,-0.984375,1,1),%u.0)"
          ";else "
          "if((i&3u)==1u)v=textureLod(tex,vec4(-0.585938,-1,-0.484375,0),%u.0);"
          "else "
          "if((i&3u)==2u)v=textureLod(tex,vec4(-1,-0.359375,-0.664062,1),%u.0);"
          "else "
          "v=textureLod(tex,vec4(1,-0.648438,-0.382812,1),%u.0);result[i]=v;}",
          level, level, level, level);
    if (paired) {
      char dual[8192];
      char *start =
          strstr(source, "void main(){uint i=gl_GlobalInvocationID.x;");
      CHECK(start);
      *start = 0;
      const char *body =
          start + strlen("void main(){uint i=gl_GlobalInvocationID.x;");
      char expression[4096];
      strcpy(expression, body);
      char *finish = strstr(expression, "result[i]=v;}");
      CHECK(finish);
      *finish = 0;
      snprintf(
          dual, sizeof(dual),
          "%slayout(binding=1) uniform samplerCubeArray tex1;vec4 fetch(uint "
          "i,samplerCubeArray tex){%sreturn v;}void main(){uint "
          "i=gl_GlobalInvocationID.x;result[i]=(fetch(i,tex)+fetch(i,tex1))*.5;"
          "}",
          source, expression);
      CHECK(strlen(dual) < sizeof(source));
      strcpy(source, dual);
    }
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char *text = source;
    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetShaderInfoLog(shader, sizeof(log), NULL, log);
      fputs(log, stderr);
    }
    CHECK(ok);
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    CHECK(ok);
    glUseProgram(program);
    glDispatchCompute(3 * layers, 1, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(actual), actual);
    CHECK(glGetError() == GL_NO_ERROR);
    unsigned size = width >> level;
    for (unsigned layer = 0; layer < layers; layer++)
      for (unsigned face = 0; face < 6; face++)
        for (unsigned y = 0; y < 4; y++)
          for (unsigned x = 0; x < 4; x++)
            for (unsigned c = 0; c < 4; c++) {
              unsigned px = (2 * x + 1) * size / 8, py = (2 * y + 1) * size / 8,
                       i = layer * 96 + face * 16 + y * 4 + x;
              float expected =
                  pixel(width, size, layer, face, level, px, py, c, unorm);
              if (constants) {
                const unsigned faces[4] = {4, 3, 1, 0}, ls[4] = {1, 0, 1, 1};
                const float st[4][2] = {{.011719f, .9921875f},
                                        {.207031f, .7421875f},
                                        {.167969f, .6796875f},
                                        {.691406f, .824219f}};
                unsigned q = i & 3;
                expected = pixel(width, size, ls[q], faces[q], level,
                                 (unsigned)(st[q][0] * size),
                                 (unsigned)(st[q][1] * size), c, unorm);
                float nearest = expected;
                if (linear) {
                  float fx = st[q][0] * size - .5f, fy = st[q][1] * size - .5f;
                  int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
                  fx -= x0;
                  fy -= y0;
                  /* The constant probe checks interior bilinear filtering, not
                   * cube seams. */
                  if (x0 < 0 || y0 < 0 || x0 + 1 >= (int)size ||
                      y0 + 1 >= (int)size)
                    continue;
                  expected = 0;
                  for (unsigned dy = 0; dy < 2; dy++)
                    for (unsigned dx = 0; dx < 2; dx++)
                      expected += pixel(width, size, ls[q], faces[q], level,
                                        x0 + dx, y0 + dy, c, unorm) *
                                  (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                }
                if (paired)
                  expected = (expected + nearest) * .5f;
              }
              checked++;
              if (fabsf(actual[i][c] - expected) >
                  (unorm ? 1.1f / 255.f : .01f)) {
                if (bad < 6)
                  fprintf(stderr,
                          "lod%u layer%u face%u (%u,%u) c%u=%g expected %g\n",
                          level, layer, face, x, y, c, actual[i][c], expected);
                bad++;
              }
            }
    glDeleteProgram(program);
    glDeleteShader(shader);
  }
  printf("%s: compute cube%s%s, %u checked components (%u mismatched words)\n",
         bad ? "FAIL" : "PASS", layers > 1 ? " array" : "",
         constants ? " constant directions" : " six faces", checked, bad);
  glDeleteBuffers(2, buffers);
  glDeleteTextures(1, &tex);
  eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(d, ctx);
  eglTerminate(d);
  return !!bad;
}
