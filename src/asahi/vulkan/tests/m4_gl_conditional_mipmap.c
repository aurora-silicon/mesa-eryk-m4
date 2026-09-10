/* SPDX-License-Identifier: MIT
 * Diagnose implicit texture sampling inside fragment control flow.
 * Use different height/base coordinate values so register allocation cannot
 * accidentally reuse staging and hide stale helper coordinates. The direct
 * sample is the control; conditional, loop and register-LOD samples precede
 * a second implicit sample. All cases must select the blue base mip;
 * nested-loop cases also encode the completed iteration count in red.
 * Live M4 runs require the watchdog and thermal guards. No game assets.
 */
#define GL_GLEXT_PROTOTYPES
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
         fprintf(stderr, "%d: %s EGL=%x GL=%x\n", __LINE__, #x, eglGetError(), \
                 glGetError());                                                \
         exit(1);                                                              \
      }                                                                        \
   } while (0)
#define W 128

static GLuint
program(unsigned mode)
{
   const char *vs = "#version 330\nlayout(location=0) in vec2 pos;"
                    "layout(location=1) in vec2 tex;out vec2 uv;"
                    "void main(){gl_Position=vec4(pos,0,1);uv=tex;}";
   char fs[2048];
   snprintf(
      fs, sizeof(fs),
      "#version 330\n#define MODE %u\n"
      "in vec2 uv;out vec4 color;uniform sampler2D baseTex,heightTex;"
      "uniform float offset,heightScale,lod;uniform int enabled,iterations;"
      "void main(){vec2 p=uv; int count=0;\n"
      "#if MODE == 1\n"
      "if(enabled!=0) p+=texture(heightTex,p*heightScale).aa*offset;\n"
      "#elif MODE == 2\n"
      "for(int i=0;i<iterations;i++) p+=texture(heightTex,p*heightScale).aa*offset;\n"
      "#elif MODE == 3\n"
      "if(enabled!=0) p+=textureLod(heightTex,p*heightScale,lod).aa*offset;\n"
      "#elif MODE == 4\n"
      "for(int i=0;i<iterations;i++){p+=texture(heightTex,p*heightScale).aa*offset;"
      "if(enabled!=0)p+=texture(heightTex,p).aa*offset;}\n"
      "#elif MODE == 5 || MODE == 6\n"
      "if(enabled!=0 && (MODE==5 || uv.x>2.0)){"
      "for(int i=0;i<iterations;i++){p+=texture(heightTex,p*heightScale).aa*offset;"
      "if(enabled!=0)p+=texture(heightTex,p).aa*offset;count++;}}\n"
      "#endif\n"
      "color=texture(baseTex,p);\n#if MODE>=5\ncolor.r=float(count)/32.0;\n#endif\n}",
      mode);
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
         fputs(log, stderr);
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
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   fflush(stdout);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   GLuint fb, target, textures[2], vbo;
   glGenTextures(1, &target);
   glBindTexture(GL_TEXTURE_2D, target);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, W);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          target, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glGenTextures(2, textures);
   const unsigned char colors[7][4] = {{255, 0, 0, 255},    {0, 255, 0, 255},
                                       {0, 0, 255, 255},    {255, 255, 0, 255},
                                       {255, 0, 255, 255},  {0, 255, 255, 255},
                                       {255, 255, 255, 255}};
   for (unsigned t = 0; t < 2; t++) {
      glActiveTexture(GL_TEXTURE0 + t);
      glBindTexture(GL_TEXTURE_2D, textures[t]);
      for (unsigned level = 0, size = 64; size; level++, size /= 2) {
         unsigned char data[64 * 64 * 4];
         for (unsigned j = 0; j < size * size; j++) {
            if (t)
               memset(data + j * 4, 128, 4);
            else
               memcpy(data + j * 4, colors[level], 4);
         }
         glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, size, size, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, data);
      }
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   }
   /* Eight repeats over 128 pixels, with a 64-pixel texture, select LOD 2.
    * Both triangles have the same affine UV mapping across their diagonal. */
   const float verts[] = {-1, -1, 0, 0, 1, -1, 8, 0, -1, 1, 0, 8,
                          1,  -1, 8, 0, 1, 1,  8, 8, -1, 1, 0, 8};
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
   for (unsigned i = 0; i < 2; i++) {
      glVertexAttribPointer(i, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            (void *)(size_t)(i * 2 * sizeof(float)));
      glEnableVertexAttribArray(i);
   }
   glViewport(0, 0, W, W);
   unsigned total_bad = 0;
   for (unsigned mode = 0; mode < 7; mode++) {
      GLuint p = program(mode);
      glUseProgram(p);
      glUniform1i(glGetUniformLocation(p, "baseTex"), 0);
      glUniform1i(glGetUniformLocation(p, "heightTex"), 1);
      glUniform1i(glGetUniformLocation(p, "enabled"), 1);
      glUniform1i(glGetUniformLocation(p, "iterations"), 20);
      glUniform1f(glGetUniformLocation(p, "offset"), 0);
      glUniform1f(glGetUniformLocation(p, "heightScale"), 2);
      glUniform1f(glGetUniformLocation(p, "lod"), 0.5f);
      glClearColor(1, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 6);
      unsigned char pixels[W * W * 4];
      glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      CHECK(glGetError() == GL_NO_ERROR);
      unsigned bad = 0;
      for (unsigned y = 0; y < W; y++)
         for (unsigned x = 0; x < W; x++) {
            const unsigned char *q = pixels + (y * W + x) * 4;
            unsigned expected_red =
               mode == 5 || (mode == 6 && x >= 32) ? 159 : 0;
            if (abs((int)q[0] - (int)expected_red) > 2 || q[1] > 2 ||
                q[2] < 253 || q[3] < 253) {
               if (bad < 3)
                  fprintf(
                     stderr,
                     "mode=%u pixel=%u,%u got=%u,%u,%u,%u expected=%u,0,255,255\n",
                     mode, x, y, q[0], q[1], q[2], q[3], expected_red);
               bad++;
            }
         }
      printf("mode=%u bad=%u/%u\n", mode, bad, W * W);
      fflush(stdout);
      total_bad += bad;
      glDeleteProgram(p);
   }
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, ctx);
   eglTerminate(d);
   printf("%s: conditional mipmaps, %u incorrect pixels\n",
          total_bad ? "FAIL" : "PASS", total_bad);
   return total_bad ? 1 : 0;
}
