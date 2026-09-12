/* SPDX-License-Identifier: MIT
 * A GBM/EGL window on a display-only DRM node must export each swapped buffer.
 * Does not acquire DRM master or change display state. Run only under the guard.
 * cc -O2 -Wall -Wextra gbm-window-probe.c -o gbm-window-probe -lgbm -lEGL -lGLESv2
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL: %s EGL=%x line=%d\n",#x,eglGetError(),__LINE__); exit(1); } } while (0)
int main(int argc, char **argv)
{
 CHECK(argc==2);
 int fd=open(argv[1],O_RDWR|O_CLOEXEC); CHECK(fd>=0);
 struct gbm_device *g=gbm_create_device(fd); CHECK(g);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,g,NULL); CHECK(d!=EGL_NO_DISPLAY);
 CHECK(eglInitialize(d,NULL,NULL)); CHECK(eglBindAPI(EGL_OPENGL_ES_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
 EGLConfig cfg[64],chosen=NULL; EGLint count;
 CHECK(eglChooseConfig(d,attrs,cfg,64,&count));
 for (int i=0;i<count;i++) { EGLint v; CHECK(eglGetConfigAttrib(d,cfg[i],EGL_NATIVE_VISUAL_ID,&v)); if(v==GBM_FORMAT_XRGB8888){chosen=cfg[i];break;} }
 CHECK(chosen);
 struct gbm_surface *s=gbm_surface_create(g,64,64,GBM_FORMAT_XRGB8888,GBM_BO_USE_RENDERING|GBM_BO_USE_SCANOUT); CHECK(s);
 EGLSurface es=eglCreateWindowSurface(d,chosen,(EGLNativeWindowType)s,NULL); CHECK(es!=EGL_NO_SURFACE);
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 EGLContext c=eglCreateContext(d,chosen,EGL_NO_CONTEXT,ca); CHECK(c!=EGL_NO_CONTEXT);
 CHECK(eglMakeCurrent(d,es,es,c));
 printf("RENDERER: %s\n",glGetString(GL_RENDERER)); fflush(stdout);
 struct gbm_bo *previous=NULL;
 for(int frame=0;frame<4;frame++) {
  glClearColor(frame&1,!(frame&1),0,1);glClear(GL_COLOR_BUFFER_BIT);
  unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  CHECK(glGetError()==GL_NO_ERROR); if(pixel[0]!=((frame&1)?255:0) || pixel[1]!=((frame&1)?0:255)) { fprintf(stderr,"FAIL: frame=%d pixel=%u,%u,%u,%u\n",frame,pixel[0],pixel[1],pixel[2],pixel[3]);exit(1); }
  CHECK(eglSwapBuffers(d,es));
  struct gbm_bo *bo=gbm_surface_lock_front_buffer(s); CHECK(bo && bo!=previous);
  int dma=gbm_bo_get_fd(bo);CHECK(dma>=0);close(dma);
  CHECK(gbm_bo_get_width(bo)==64 && gbm_bo_get_stride(bo)>=256);
  printf("frame=%d handle=%u modifier=%llu stride=%u\n",frame,gbm_bo_get_handle(bo).u32,(unsigned long long)gbm_bo_get_modifier(bo),gbm_bo_get_stride(bo));
  if(previous)gbm_surface_release_buffer(s,previous);
  previous=bo;
 }
 gbm_surface_release_buffer(s,previous);
 CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
 CHECK(eglDestroyContext(d,c));CHECK(eglDestroySurface(d,es));CHECK(eglTerminate(d));
 gbm_surface_destroy(s);gbm_device_destroy(g);close(fd);
 puts("PASS: GBM window pixels and four exported front buffers");
}
