/* SPDX-License-Identifier: MIT
 * Half-float framebuffer overflow policy regression, via Zink/Vulkan.
 * Hardware execution requires the watchdog/thermal experiment guard.
 * Finite saturation is an implementation choice permitted by Vulkan;
 * preservation of actual infinities/NaNs is independently checked.
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
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static GLuint shader(GLenum stage, const char *source) {
   GLuint s = glCreateShader(stage); glShaderSource(s, 1, &source, NULL);
   glCompileShader(s); GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log); puts(log); }
   CHECK(ok); return s;
}
int main(void) {
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   const EGLint attrs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("Renderer: %s\n", renderer); fflush(stdout);
   CHECK(strstr(renderer, "zink") && strstr(renderer, "M4"));
   const float input[] = {0, -0.0f, 1, -1, 65504, -65504, 65520, -65520,
      65536, -65536, 1e10f, -1e10f, INFINITY, -INFINITY, NAN, .5f};
   enum { N = sizeof(input)/sizeof(input[0]) };
   GLuint textures[2], fbo, vao; glGenTextures(2, textures);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, N, 1, 0, GL_RED, GL_FLOAT, input);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, N, 1, 0, GL_RGBA, GL_FLOAT, NULL);
   glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[1], 0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   GLuint program = glCreateProgram();
   glAttachShader(program, shader(GL_VERTEX_SHADER,
      "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
   glAttachShader(program, shader(GL_FRAGMENT_SHADER,
      "#version 330 core\nuniform sampler2D src;layout(location=0)out vec4 color;void main(){color=vec4(texelFetch(src,ivec2(gl_FragCoord.xy),0).r);}"));
   glLinkProgram(program); GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok); CHECK(ok);
   glUseProgram(program); glUniform1i(glGetUniformLocation(program,"src"),0);
   glBindTexture(GL_TEXTURE_2D, textures[0]); glGenVertexArrays(1,&vao); glBindVertexArray(vao);
   glViewport(0,0,N,1); unsigned bad=0;
   for (unsigned blend=0; blend<2; blend++) {
      glDisable(GL_BLEND); glDrawArrays(GL_TRIANGLES,0,3);
      if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE); glDrawArrays(GL_TRIANGLES,0,3); }
      float output[N][4]; glReadPixels(0,0,N,1,GL_RGBA,GL_FLOAT,output); CHECK(glGetError()==GL_NO_ERROR);
      for (unsigned i=0;i<N;i++) {
         float expected = isfinite(input[i]) ? fmaxf(-65504,fminf(65504,input[i])) : input[i];
         if (blend && isfinite(expected)) expected=fmaxf(-65504,fminf(65504,2*expected));
         for (unsigned k=0;k<4;k++) {
            float got=output[i][k];
            if (!(isnan(expected) ? isnan(got) : got==expected) ||
                (!blend && expected == 0 && !!signbit(got) != !!signbit(expected))) {
               printf("blend=%u input[%u]=%g channel=%u expected=%g got=%g\n",blend,i,input[i],k,expected,got); bad++;
            }
         }
      }
   }
   printf("%s: half-float framebuffer finite overflow and special values; bad=%u/128\n",bad?"FAIL":"PASS",bad);
   return bad != 0;
}
