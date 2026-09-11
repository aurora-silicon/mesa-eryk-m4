/* SPDX-License-Identifier: MIT
 * Analytic 2D depth comparison/PCF regression. No game assets.
 * Live J713 execution requires watchdog and thermal guards.
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
         return 1;                                                             \
      }                                                                        \
   } while (0)
static float
compare(unsigned c, float r, float d)
{
   switch (c) {
   case 0:
      return 0;
   case 1:
      return r < d;
   case 2:
      return r == d;
   case 3:
      return r <= d;
   case 4:
      return r > d;
   case 5:
      return r != d;
   case 6:
      return r >= d;
   default:
      return 1;
   }
}
static int
edge(int i)
{
   return i < 0 ? 0 : i > 3 ? 3 : i;
}
static float
sample_expected(const float *depths, unsigned filter, unsigned c, float r,
                float u, float v)
{
   if (!filter)
      return compare(c, r, depths[edge(floorf(v)) * 4 + edge(floorf(u))]);
   u -= .5f;
   v -= .5f;
   int ix = floorf(u), iy = floorf(v);
   float fx = u - ix, fy = v - iy, e = 0;
   for (unsigned dy = 0; dy < 2; dy++)
      for (unsigned dx = 0; dx < 2; dx++)
         e += compare(c, r, depths[edge(iy + dy) * 4 + edge(ix + dx)]) *
              (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
   return e;
}
int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                        EGL_DEFAULT_DISPLAY, NULL);
   CHECK(eglInitialize(d, NULL, NULL));
   CHECK(eglBindAPI(EGL_OPENGL_ES_API));
   EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
   EGLContext ctx =
      eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(ctx != EGL_NO_CONTEXT);
   CHECK(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer);
   fflush(stdout);
   CHECK((strstr(renderer, "zink") && strstr(renderer, "M4")) ||
         (getenv("M4_TEST_LLVMPIPE") && strstr(renderer, "llvmpipe")));
   const char *sources[] = {
      "#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}",
      "#version 300 es\nprecision highp float;precision highp sampler2DShadow;uniform sampler2DShadow depthTex;uniform float reference;uniform int wide;out vec4 color;void main(){vec2 uv=gl_FragCoord.xy/16.*1.5-.25;float result;if(wide==0){result=texture(depthTex,vec3(uv,reference));}else{vec2 xy=uv*4.;vec2 offset=fract(xy-.5);vec4 size=vec4(offset+1.,2.-offset);vec4 w=(vec4(1.,1.,-.5,-.5)+(xy-.5*offset).xyxy)*.25;vec4 taps=vec4(texture(depthTex,vec3(w.zw,reference)),texture(depthTex,vec3(w.xw,reference)),texture(depthTex,vec3(w.zy,reference)),texture(depthTex,vec3(w.xy,reference)));result=dot(size.zxzx*size.wwyy,taps)/9.;}color=vec4(result);}"};
   GLuint prog = glCreateProgram();
   GLenum kinds[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
   for (unsigned i = 0; i < 2; i++) {
      GLuint sh = glCreateShader(kinds[i]);
      glShaderSource(sh, 1, &sources[i], NULL);
      glCompileShader(sh);
      GLint ok;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (!ok) {
         char msg[4096];
         glGetShaderInfoLog(sh, sizeof(msg), NULL, msg);
         fputs(msg, stderr);
      }
      CHECK(ok);
      glAttachShader(prog, sh);
      glDeleteShader(sh);
   }
   glLinkProgram(prog);
   GLint ok;
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(prog);
   GLuint target, fb, depth;
   glGenTextures(1, &target);
   glBindTexture(GL_TEXTURE_2D, target);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 16, 16);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          target, 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   float depths[16];
   for (unsigned y = 0; y < 4; y++)
      for (unsigned x = 0; x < 4; x++)
         depths[y * 4 + x] = ((x + 2 * y) % 5) * .25f;
   glGenTextures(1, &depth);
   glBindTexture(GL_TEXTURE_2D, depth);
   const GLenum depth_format = getenv("M4_TEST_DEPTH24")
                                  ? GL_DEPTH_COMPONENT24
                                  : GL_DEPTH_COMPONENT32F;
   printf("Depth format: %#x\n", depth_format);
   glTexStorage2D(GL_TEXTURE_2D, 1, depth_format, 4, 4);
   /* Create the depth map through rendering, as a game's shadow pass does.
    * This also checks depth attachment -> sampled texture visibility. */
   const char *depth_sources[] = {
      "#version 300 es\nuniform float z;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,z,1);}",
      "#version 300 es\nprecision highp float;void main(){}"};
   GLuint depth_prog = glCreateProgram();
   for (unsigned i = 0; i < 2; i++) {
      GLuint sh = glCreateShader(kinds[i]);
      glShaderSource(sh, 1, &depth_sources[i], NULL);
      glCompileShader(sh);
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      CHECK(ok);
      glAttachShader(depth_prog, sh);
      glDeleteShader(sh);
   }
   glLinkProgram(depth_prog);
   glGetProgramiv(depth_prog, GL_LINK_STATUS, &ok);
   CHECK(ok);
   glUseProgram(depth_prog);
   GLint z = glGetUniformLocation(depth_prog, "z");
   CHECK(z >= 0);
   GLuint depth_fb;
   glGenFramebuffers(1, &depth_fb);
   glBindFramebuffer(GL_FRAMEBUFFER, depth_fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                          depth, 0);
   const GLenum none = GL_NONE;
   glDrawBuffers(1, &none);
   glReadBuffer(GL_NONE);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   glViewport(0, 0, 4, 4);
   GLint depth_bits, depth_type;
   glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth_bits);
   glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &depth_type);
   printf("Depth attachment: bits=%d type=%#x\n", depth_bits, depth_type);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glEnable(GL_SCISSOR_TEST);
   for (unsigned y = 0; y < 4; y++) {
      for (unsigned x = 0; x < 4; x++) {
         glScissor(x, y, 1, 1);
         glUniform1f(z, depths[y * 4 + x] * 2 - 1);
         glDrawArrays(GL_TRIANGLES, 0, 3);
      }
   }
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_SCISSOR_TEST);
   /* This optional control isolates Zink's D24->D32F backing promotion. It is
    * not a D24 conformance pass: keep the strict quantized oracle available. */
   const bool float_reference = getenv("M4_TEST_DEPTH_FLOAT_REFERENCE") != NULL;
   printf("Reference storage: %s\n", float_reference ? "float control" : "requested format");
   if (depth_format == GL_DEPTH_COMPONENT24 && !float_reference) {
      for (unsigned i = 0; i < 16; i++)
         depths[i] = (float)(round((double)depths[i] * 16777215.0) / 16777215.0);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glUseProgram(prog);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                   GL_COMPARE_REF_TO_TEXTURE);
   glUniform1i(glGetUniformLocation(prog, "depthTex"), 0);
   GLint ref = glGetUniformLocation(prog, "reference");
   CHECK(ref >= 0);
   glViewport(0, 0, 16, 16);
   const GLenum funcs[] = {GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                           GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
   const float refs[] = {.125, .5, .875};
   unsigned bad = 0, total = 0, max_error = 0;
   for (unsigned wide = 0; wide < 2; wide++)
      for (unsigned filter = 0; filter < 2; filter++)
         for (unsigned c = 0; c < 8; c++)
            for (unsigned r = 0; r < 3; r++) {
               glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                               filter ? GL_LINEAR : GL_NEAREST);
               glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                               filter ? GL_LINEAR : GL_NEAREST);
               glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC,
                               funcs[c]);
               glUniform1f(ref, refs[r]);
               glUniform1i(glGetUniformLocation(prog, "wide"), wide);
               glClearColor(1, 0, 1, 1);
               glClear(GL_COLOR_BUFFER_BIT);
               glDrawArrays(GL_TRIANGLES, 0, 3);
               unsigned char pixels[16 * 16 * 4];
               glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
               CHECK(glGetError() == GL_NO_ERROR);
               unsigned wrong = 0;
               for (unsigned y = 0; y < 16; y++)
                  for (unsigned x = 0; x < 16; x++) {
                     float u = ((x + .5f) / 16 * 1.5f - .25f) * 4,
                           v = ((y + .5f) / 16 * 1.5f - .25f) * 4, e;
                     if (!wide) {
                        e = sample_expected(depths, filter, c, refs[r], u, v);
                     } else {
                        float ox = u - .5f - floorf(u - .5f);
                        float oy = v - .5f - floorf(v - .5f);
                        float wx[] = {u - .5f * ox - .5f, u - .5f * ox + 1};
                        float wy[] = {v - .5f * oy - .5f, v - .5f * oy + 1};
                        e = 0;
                        for (unsigned dy = 0; dy < 2; dy++)
                           for (unsigned dx = 0; dx < 2; dx++)
                              e += sample_expected(depths, filter, c, refs[r],
                                                   wx[dx], wy[dy]) *
                                   (dx ? 1 + ox : 2 - ox) *
                                   (dy ? 1 + oy : 2 - oy) / 9;
                     }
                     unsigned err = 0;
                     for (unsigned k = 0; k < 4; k++) {
                        unsigned q = abs((int)pixels[(y * 16 + x) * 4 + k] -
                                         (int)lroundf(e * 255));
                        if (q > err)
                           err = q;
                     }
                     if (err > max_error)
                        max_error = err;
                     wrong += err > 2;
                  }
               printf("wide=%u filter=%u compare=%u ref=%g wrong=%u\n", wide,
                      filter, c, refs[r], wrong);
               fflush(stdout);
               bad += wrong;
               total += 256;
            }
   printf("pixels=%u incorrect=%u max_error=%u\n", total, bad, max_error);
   return bad ? 1 : 0;
}
