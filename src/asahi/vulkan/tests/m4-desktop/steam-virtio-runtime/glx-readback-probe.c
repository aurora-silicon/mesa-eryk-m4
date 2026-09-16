/* Steam's GLX visual/context path, with a GPU renderer and pixel oracle.
 * Build with cc [-m32] FILE -lGL -lX11; run under the mandatory GPU guard. */
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", #x); return 1; } } while (0)
int main(void)
{
   Display *display = XOpenDisplay(NULL);
   CHECK(display);
   int attrs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
                  GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
   XVisualInfo *visual = glXChooseVisual(display, DefaultScreen(display), attrs);
   CHECK(visual);
   GLXContext context = glXCreateContext(display, visual, NULL, True);
   CHECK(context);
   XSetWindowAttributes wa = {.colormap = XCreateColormap(display,
      RootWindow(display, visual->screen), visual->visual, AllocNone)};
   Window window = XCreateWindow(display, RootWindow(display, visual->screen),
      0, 0, 16, 16, 0, visual->depth, InputOutput, visual->visual, CWColormap, &wa);
   CHECK(window && glXMakeCurrent(display, window, context));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("GLX %zu-bit renderer: %s\n", sizeof(void *) * 8, renderer ? renderer : "null");
   CHECK(renderer && strstr(renderer, "Apple M4"));
   glClearColor(1, 0, 0, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   unsigned char pixels[16 * 16 * 4];
   glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   CHECK(glGetError() == GL_NO_ERROR);
   for (unsigned i = 0; i < 16 * 16; ++i)
      CHECK(pixels[i*4] == 255 && pixels[i*4+1] == 0 && pixels[i*4+2] == 0 && pixels[i*4+3] == 255);
   glXMakeCurrent(display, None, NULL);
   glXDestroyContext(display, context);
   XDestroyWindow(display, window);
   XFreeColormap(display, wa.colormap);
   XFree(visual);
   XCloseDisplay(display);
   printf("PASS: GLX %zu-bit visual/context/readback 256 pixels\n", sizeof(void *) * 8);
   return 0;
}
