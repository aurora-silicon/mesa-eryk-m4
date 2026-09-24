/* SPDX-License-Identifier: MIT
 * Regression for EGL/X11 reporting new dimensions with stale drawable bounds.
 * Build: cc egl-x11-resize.c -o egl-x11-resize $(pkg-config --cflags --libs egl gl x11)
 * Run under Xvfb. Hardware execution requires the normal watchdog/thermal guard.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#include <GL/gl.h>

int main(void)
{
   Display *x = XOpenDisplay(NULL);
   assert(x);
   EGLDisplay d = eglGetDisplay((EGLNativeDisplayType)x);
   assert(eglInitialize(d, NULL, NULL));
   assert(eglBindAPI(EGL_OPENGL_API));
   EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
   EGLConfig config;
   EGLint count, visual_id;
   assert(eglChooseConfig(d, attrs, &config, 1, &count) && count);
   assert(eglGetConfigAttrib(d, config, EGL_NATIVE_VISUAL_ID, &visual_id));
   XVisualInfo match = {.visualid = visual_id};
   int nvisuals;
   XVisualInfo *vi = XGetVisualInfo(x, VisualIDMask, &match, &nvisuals);
   assert(vi);
   XSetWindowAttributes wa = {
      .colormap = XCreateColormap(x, RootWindow(x, vi->screen), vi->visual, AllocNone)
   };
   Window window = XCreateWindow(x, RootWindow(x, vi->screen), 0, 0, 32, 32,
      0, vi->depth, InputOutput, vi->visual, CWColormap, &wa);
   EGLSurface surface = eglCreateWindowSurface(d, config,
      (EGLNativeWindowType)window, NULL);
   assert(surface != EGL_NO_SURFACE);
   EGLContext ctx = eglCreateContext(d, config, EGL_NO_CONTEXT, NULL);
   assert(ctx != EGL_NO_CONTEXT);
   assert(eglMakeCurrent(d, surface, surface, ctx));
   printf("renderer: %s\n", glGetString(GL_RENDERER));
   glViewport(0, 0, 32, 32);
   glClearColor(0, 0, 0, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   const int sizes[][2] = {{128,96}, {47,61}, {193,127}, {32,32}};
   int failed = 0;
   for (unsigned step = 0; step < 8; step++) {
      int w = sizes[step % 4][0], h = sizes[step % 4][1];
      assert(eglWaitClient());
      XResizeWindow(x, window, w, h);
      XSync(x, False);
      assert(eglWaitNative(EGL_CORE_NATIVE_ENGINE));
      EGLint width, height;
      assert(eglQuerySurface(d, surface, EGL_WIDTH, &width));
      assert(eglQuerySurface(d, surface, EGL_HEIGHT, &height));
      assert(width == w && height == h);
      /* A previously acquired swapchain image can retain its old size
       * until presented. Complete that frame before testing later resizes.
       * Keep the first iteration unswapped to reproduce the trace defect.
       */
      if (step)
         assert(eglSwapBuffers(d, surface));
      glViewport(0, 0, w, h);
      glScissor(0, 0, w, h);
      glClearColor(1, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      unsigned char *pixels = calloc((size_t)w * h, 4);
      assert(pixels);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      unsigned red = 0;
      for (int i = 0; i < w * h; i++)
         red += pixels[4*i] == 255 && pixels[4*i+1] == 0 && pixels[4*i+2] == 0;
      GLenum error = glGetError();
      printf("step %u: %dx%d, red %u/%d, GL error 0x%x\n", step, w, h, red, w*h, error);
      free(pixels);
      if (red != (unsigned)(w*h) || error != GL_NO_ERROR) {
         failed = 1;
         break;
      }
   }
   assert(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
   eglDestroyContext(d, ctx);
   eglDestroySurface(d, surface);
   eglTerminate(d);
   XDestroyWindow(x, window);
   XFreeColormap(x, wa.colormap);
   XFree(vi);
   XCloseDisplay(x);
   puts(failed ? "RESIZE_PROBE_FAIL" : "RESIZE_PROBE_PASS");
   return failed;
}
