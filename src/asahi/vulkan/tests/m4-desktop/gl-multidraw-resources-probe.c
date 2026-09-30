/* SPDX-License-Identifier: MIT
 * Pixel oracle for indexed multi-draw resource reuse. Run with the hardware
 * watchdog/thermal guard. cc -O2 -Wall -Wextra -Werror FILE -lEGL -lGL -o PROBE
 * Switches ordinary/sparse UBOs, ranges, contents and edge/border samplers
 * between API calls. Both vertex and fragment shaders consume uniform data.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); \
   exit(1); } } while (0)

static int
extension(const char *name)
{
   GLint count;
   glGetIntegerv(GL_NUM_EXTENSIONS, &count);
   for (int i = 0; i < count; ++i)
      if (!strcmp((const char *)glGetStringi(GL_EXTENSIONS, i), name))
         return 1;
   return 0;
}

static GLuint
shader(GLenum type, const char *text)
{
   GLuint s = glCreateShader(type);
   glShaderSource(s, 1, &text, NULL);
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

int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
      EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
   CHECK(c != EGL_NO_CONTEXT &&
         eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));
   CHECK(extension("GL_ARB_multi_draw_indirect"));
   CHECK(extension("GL_ARB_shader_draw_parameters"));
   CHECK(extension("GL_ARB_sparse_buffer"));
   PFNGLBUFFERPAGECOMMITMENTARBPROC commit =
      (void *)eglGetProcAddress("glBufferPageCommitmentARB");
   CHECK(commit);

   GLuint p = glCreateProgram();
   glAttachShader(p, shader(GL_VERTEX_SHADER,
      "#version 330 core\n#extension GL_ARB_shader_draw_parameters : require\n"
      "layout(std140) uniform Mask {uvec4 mask;};"
      "flat out uvec4 vmask;flat out uvec3 vdraw;void main(){vmask=mask;"
      "vdraw=uvec3(gl_DrawIDARB,gl_BaseVertexARB,gl_BaseInstanceARB);"
      "int i=gl_VertexID-gl_BaseVertexARB;vec2 p=vec2((i<<1)&2,i&2);"
      "gl_Position=vec4(p*2.0-1.0,0,1);}"));
   glAttachShader(p, shader(GL_FRAGMENT_SHADER,
      "#version 330 core\nlayout(std140) uniform Values {uvec4 value;};"
      "flat in uvec4 vmask;flat in uvec3 vdraw;uniform sampler2D image;"
      "out vec4 color;void main(){uint id=vdraw.x;"
      "uvec4 tag=uvec4((id^(id>>8)^(id>>16)^(id>>24))&255u,"
      "vdraw.y&255u,vdraw.z&255u,0u);color=texture(image,vec2(-0.5))*"
      "vec4(value^vmask^tag)/255.0;}"));
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(p);
   glUniformBlockBinding(p, glGetUniformBlockIndex(p, "Values"), 0);
   glUniformBlockBinding(p, glGetUniformBlockIndex(p, "Mask"), 1);

   GLint page, alignment;
   glGetIntegerv(GL_SPARSE_BUFFER_PAGE_SIZE_ARB, &page);
   glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
   CHECK(page >= 16 && alignment > 0 && page % alignment == 0);
   GLuint buffers[3];
   glGenBuffers(3, buffers);
   for (unsigned i = 0; i < 3; ++i) {
      glBindBuffer(GL_UNIFORM_BUFFER, buffers[i]);
      glBufferStorage(GL_UNIFORM_BUFFER, 2 * page, NULL,
         GL_DYNAMIC_STORAGE_BIT | (i == 1 ? GL_SPARSE_STORAGE_BIT_ARB : 0));
      if (i == 1)
         commit(GL_UNIFORM_BUFFER, 0, 2 * page, GL_TRUE);
   }
   CHECK(glGetError() == GL_NO_ERROR);

   GLuint vao, indices, indirect;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &indices);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
   const uint32_t index_data[] = {0, 1, 2};
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_data), index_data,
                GL_STATIC_DRAW);
   struct { uint32_t count, instances, first_index, base_vertex, base_instance; }
      draws[257];
   for (unsigned i = 0; i < 257; ++i)
      draws[i] = (__typeof__(draws[0])){3, 1, 0, i % 13, i * 2 + 7};
   glGenBuffers(1, &indirect);
   glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect);
   glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(draws), draws, GL_STATIC_DRAW);

   const unsigned char texel[4] = {64, 128, 192, 255};
   GLuint input, samplers[2], output, fb;
   glGenTextures(1, &input);
   glBindTexture(GL_TEXTURE_2D, input);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, texel);
   glGenSamplers(2, samplers);
   for (unsigned i = 0; i < 2; ++i) {
      glSamplerParameteri(samplers[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glSamplerParameteri(samplers[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glSamplerParameteri(samplers[i], GL_TEXTURE_WRAP_S,
                          i ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE);
      glSamplerParameteri(samplers[i], GL_TEXTURE_WRAP_T,
                          i ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE);
      const float border[4] = {1, 0.25, 0, 1};
      glSamplerParameterfv(samplers[i], GL_TEXTURE_BORDER_COLOR, border);
   }
   glGenTextures(1, &output);
   glBindTexture(GL_TEXTURE_2D, output);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 4, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 256, 4);
   glBindTexture(GL_TEXTURE_2D, input);

   unsigned char pixels[256 * 4 * 4];
   unsigned bad = 0;
   for (unsigned round = 0; round < 48; ++round) {
      const unsigned sparse = (round / 2) & 1;
      const unsigned border = (round / 4) & 1;
      const GLintptr offset = ((round / 8) & 1) * page;
      const unsigned counts[] = {2, 129, 257};
      const unsigned count = counts[round / 16];
      const unsigned value[4] = {(round * 73 + 19) & 255,
         (round * 41 + 7) & 255, (round * 137 + 23) & 255, 255};
      const unsigned mask[4] = {(round * 17) & 255,
         (round * 59) & 255, (round * 101) & 255, 0};
      glBindBuffer(GL_UNIFORM_BUFFER, buffers[sparse]);
      glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(value), value);
      glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffers[sparse], offset, 16);
      glBindBuffer(GL_UNIFORM_BUFFER, buffers[2]);
      glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(mask), mask);
      glBindBufferRange(GL_UNIFORM_BUFFER, 1, buffers[2], offset, 16);
      glBindSampler(0, samplers[border]);
      glClearColor(0.75, 0.5, 0.25, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, NULL,
                                  count, sizeof(draws[0]));
      glReadPixels(0, 0, 256, 4, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned expected[4];
      for (unsigned component = 0; component < 4; ++component) {
         /* Border components are exactly 1, 1/4, 0, 1. */
         const unsigned scale[] = {4, 1, 0, 4};
         const unsigned id = count - 1;
         const unsigned tag[] = {(id ^ (id >> 8) ^ (id >> 16) ^ (id >> 24)) & 255,
                                 id % 13, (id * 2 + 7) & 255, 0};
         const unsigned color = value[component] ^ mask[component] ^ tag[component];
         expected[component] = border ?
            (color * scale[component] + 2) / 4 :
            (color * texel[component] + 127) / 255;
      }
      for (unsigned j = 0; j < 256 * 4; ++j)
         for (unsigned component = 0; component < 4; ++component)
            bad += abs((int)pixels[j * 4 + component] -
                       (int)expected[component]) > 1;
      printf("round=%u draws=%u sparse=%u border=%u offset=%ld bad=%u\n",
             round, count, sparse, border, (long)offset, bad);
      fflush(stdout);
   }
   CHECK(!bad);
   puts("PASS: 48 indexed multi-draw buffer/sampler/content/range switches");
   return 0;
}
