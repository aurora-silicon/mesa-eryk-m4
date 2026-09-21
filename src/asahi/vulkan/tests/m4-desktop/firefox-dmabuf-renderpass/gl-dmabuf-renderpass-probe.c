/* SPDX-License-Identifier: MIT
 * Imported sampler first use in a depth-discard render pass. Run under the
 * watchdog/thermal guard. cc -O2 FILE -lEGL -lGL -o gl-dmabuf-renderpass-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                                         \
   do {                                                                                  \
      if (!(x)) {                                                                        \
         fprintf(stderr, "FAIL line %d: %s GL=%x EGL=%x\n", __LINE__, #x, glGetError(),  \
                 eglGetError());                                                         \
         exit(1);                                                                        \
      }                                                                                  \
   } while (0)
static GLuint shader(GLenum type, const char *s)
{
   GLuint id = glCreateShader(type);
   glShaderSource(id, 1, &s, NULL);
   glCompileShader(id);
   GLint ok;
   glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
   CHECK(ok);
   return id;
}
static GLuint texture(unsigned w, unsigned h, const void *pixels)
{
   GLuint t;
   glGenTextures(1, &t);
   glBindTexture(GL_TEXTURE_2D, t);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   return t;
}
int main(int argc, char **argv)
{
   bool unbind_before_flush = false, vertex_sampler = false;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--unbind-before-flush"))
         unbind_before_flush = true;
      else if (!strcmp(argv[i], "--vertex-sampler"))
         vertex_sampler = true;
      else {
         fprintf(stderr, "Unknown option: %s\n", argv[i]);
         return 2;
      }
   }
   setbuf(stdout, NULL);
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,
                  3,
                  EGL_CONTEXT_MINOR_VERSION,
                  3,
                  EGL_CONTEXT_OPENGL_PROFILE_MASK,
                  EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                  EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));
   PFNEGLCREATEIMAGEKHRPROC create = (void *)eglGetProcAddress("eglCreateImageKHR");
   PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC query =
       (void *)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
   PFNEGLEXPORTDMABUFIMAGEMESAPROC export =
       (void *)eglGetProcAddress("eglExportDMABUFImageMESA");
   PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target =
       (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
   CHECK(create && query && export && target);
   unsigned char green[] = {0, 255, 0, 255}, red[] = {255, 0, 0, 255};
   GLuint source = texture(1, 1, green);
   glFinish();
   EGLint ia[] = {EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
                  EGL_NONE};
   EGLImage image =
       create(d, c, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)source, ia);
   CHECK(image != EGL_NO_IMAGE_KHR);
   int fourcc, planes, fd, stride, offset;
   EGLuint64KHR modifier;
   CHECK(query(d, image, &fourcc, &planes, &modifier) && planes == 1);
   CHECK(export(d, image, &fd, &stride, &offset));
   EGLint da[] = {EGL_WIDTH,
                  1,
                  EGL_HEIGHT,
                  1,
                  EGL_LINUX_DRM_FOURCC_EXT,
                  fourcc,
                  EGL_DMA_BUF_PLANE0_FD_EXT,
                  fd,
                  EGL_DMA_BUF_PLANE0_PITCH_EXT,
                  stride,
                  EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                  offset,
                  EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                  (EGLint)modifier,
                  EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
                  (EGLint)(modifier >> 32),
                  EGL_NONE};
   EGLImage imported = create(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, da);
   CHECK(imported != EGL_NO_IMAGE_KHR);
   close(fd);
   GLuint shared;
   glGenTextures(1, &shared);
   glBindTexture(GL_TEXTURE_2D, shared);
   target(GL_TEXTURE_2D, imported);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   GLuint ordinary = texture(1, 1, red), color = texture(64, 64, NULL), depth, fb, vao;
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   GLuint p = glCreateProgram();
   glAttachShader(
       p,
       shader(
           GL_VERTEX_SHADER,
           vertex_sampler
               ? "#version 330\nuniform float z;uniform sampler2D tex;void main(){vec2 "
                 "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,z,"
                 "texelFetch(tex,ivec2(0),0).a);}"
               : "#version 330\nuniform float z;void main(){vec2 "
                 "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,z,1);"
                 "}"));
   glAttachShader(p, shader(GL_FRAGMENT_SHADER,
                            "#version 330\nuniform sampler2D tex;out vec4 color;void "
                            "main(){color=texelFetch(tex,ivec2(0),0);}"));
   glLinkProgram(p);
   GLint ok;
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(p);
   GLint z = glGetUniformLocation(p, "z");
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glViewport(0, 0, 64, 64);
   glEnable(GL_DEPTH_TEST);
   for (unsigned frame = 0; frame < 200; frame++) {
      glDisable(GL_SCISSOR_TEST);
      glClearColor(0, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_SCISSOR_TEST);
      glScissor(0, 0, 32, 64);
      glBindTexture(GL_TEXTURE_2D, ordinary);
      glUniform1f(z, -0.5);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glDisable(GL_SCISSOR_TEST);
      glBindTexture(GL_TEXTURE_2D, shared);
      glUniform1f(z, 0.5);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (unbind_before_flush) {
         glBindTexture(GL_TEXTURE_2D, ordinary);
         glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
         glDepthMask(GL_FALSE);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
         glDepthMask(GL_TRUE);
      }
      GLenum discard = GL_DEPTH_ATTACHMENT;
      glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &discard);
      glFlush();
   }
   unsigned char pixels[64 * 64 * 4];
   glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   CHECK(glGetError() == GL_NO_ERROR);
   unsigned bad = 0;
   for (unsigned y = 0; y < 64; y++)
      for (unsigned x = 0; x < 64; x++)
         for (unsigned k = 0; k < 4; k++)
            bad += pixels[(y * 64 + x) * 4 + k] != (x < 32 ? red[k] : green[k]);
   printf("unbind_before_flush=%u vertex_sampler=%u pixels=4096 incorrect=%u\n",
          unbind_before_flush, vertex_sampler, bad);
   CHECK(!bad);
   puts("PASS: imported sampler preserves mid-pass depth across 200 submissions");
   return 0;
}
