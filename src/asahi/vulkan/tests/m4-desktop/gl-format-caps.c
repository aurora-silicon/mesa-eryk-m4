/* Query the renderbuffer sample counts relevant to ANGLE ES3 admission.
 * Build: cc -O2 gl-format-caps.c -o gl-format-caps -lEGL -lGL
 * Run on J713 only inside gpu-experiment.py through run-m4-gl.py.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   if (!eglInitialize(d, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API)) return 1;
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) return 1;
   printf("GL: %s\nRenderer: %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
#define F(x) {#x, x}
   const struct { const char *name; GLenum format; } formats[] = {
      F(GL_R8), F(GL_RG8), F(GL_RGB8), F(GL_RGBA8), F(GL_SRGB8_ALPHA8),
      F(GL_RGB565), F(GL_RGBA4), F(GL_RGB5_A1), F(GL_RGB10_A2),
      F(GL_DEPTH_COMPONENT16), F(GL_DEPTH_COMPONENT24), F(GL_DEPTH_COMPONENT32F),
      F(GL_STENCIL_INDEX8), F(GL_DEPTH24_STENCIL8), F(GL_DEPTH32F_STENCIL8),
   };
   for (unsigned i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) {
      GLint n = 0, samples[32] = {0};
      glGetInternalformativ(GL_RENDERBUFFER, formats[i].format, GL_NUM_SAMPLE_COUNTS, 1, &n);
      if (n < 0 || n > 32) return 2;
      if (n) glGetInternalformativ(GL_RENDERBUFFER, formats[i].format, GL_SAMPLES, n, samples);
      printf("%s: count=%d samples=", formats[i].name, n);
      for (int j = 0; j < n; ++j) printf("%s%d", j ? "," : "", samples[j]);
      printf(" error=%x\n", glGetError());
   }
   GLint components = 0;
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS, &components);
   printf("Transform feedback interleaved components: %d error=%x\n", components, glGetError());
   eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(d, c);
   eglTerminate(d);
   puts("PASS: capability queries completed (no rendering qualification)");
   return 0;
}
