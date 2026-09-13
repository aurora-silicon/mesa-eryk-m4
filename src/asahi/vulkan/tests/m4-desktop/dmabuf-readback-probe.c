/* SPDX-License-Identifier: MIT
 * EGL DMA-BUF framebuffer colour/readback oracle. Run only under GPU guard.
 * cc -O2 dmabuf-readback-probe.c -o probe -lEGL -lGL -lgbm
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static GLuint shader(GLenum type,const char *src) {
 GLuint s=glCreateShader(type);glShaderSource(s,1,&src,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);CHECK(ok);return s;
}
static unsigned check_pixels(const unsigned char *p,int bgra) {
 unsigned bad=0;
 for(unsigned i=0;i<64*64;i++) {
  unsigned char e[4]={i%64<32?191:32,i/64<32?64:128,16,255};
  if(bgra) {e[2]=e[0];e[0]=16;}
  for(unsigned c=0;c<4;c++)bad+=abs(p[i*4+c]-e[c])>1;
 }
 return bad;
}
int main(int argc,char **argv) {
 uint64_t requested_mod=argc>1?strtoull(argv[1],NULL,0):0;
 unsigned samples=argc>2?strtoul(argv[2],NULL,0):1;CHECK(samples==1||samples==4);
 int use_renderbuffer=argc>3 && !strcmp(argv[3],"renderbuffer");
 setvbuf(stdout,NULL,_IOLBF,0);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,ca);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer %s\n",renderer);
 CHECK(strstr(renderer,"zink")&&strstr(renderer,"M4"));
 int drm=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(drm>=0);
 struct gbm_device *gd=gbm_create_device(drm);CHECK(gd);
 PFNEGLCREATEIMAGEKHRPROC create=(void*)eglGetProcAddress("eglCreateImageKHR");
 PFNEGLDESTROYIMAGEKHRPROC destroy=(void*)eglGetProcAddress("eglDestroyImageKHR");
 PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target=(void*)eglGetProcAddress("glEGLImageTargetTexture2DOES");
 PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC targetrb=(void*)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
 CHECK(create&&destroy&&target&&targetrb);
 GLuint program=glCreateProgram();
 GLuint vs=shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}");
 GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 330 core\nout vec4 color;void main(){color=vec4(gl_FragCoord.x<32.?191.:32.,gl_FragCoord.y<32.?64.:128.,16.,255.)/255.;}");
 glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
 GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);CHECK(linked);glUseProgram(program);
 GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 unsigned total=0;
 for(unsigned order=0;order<2;order++) {
  uint32_t format=order?GBM_FORMAT_ABGR8888:GBM_FORMAT_ARGB8888;
  struct gbm_bo *bo=gbm_bo_create_with_modifiers2(gd,64,64,format,&requested_mod,1,GBM_BO_USE_RENDERING);CHECK(bo);
  int fd=gbm_bo_get_fd(bo);CHECK(fd>=0);uint64_t mod=gbm_bo_get_modifier(bo);
  EGLint ia[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_LINUX_DRM_FOURCC_EXT,format,
   EGL_DMA_BUF_PLANE0_FD_EXT,fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,
   EGL_DMA_BUF_PLANE0_PITCH_EXT,gbm_bo_get_stride(bo),
   EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,(uint32_t)mod,EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,mod>>32,EGL_NONE};
  EGLImageKHR image=create(d,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,ia);
  CHECK(image!=EGL_NO_IMAGE_KHR);close(fd);
  GLuint tex=0,rb=0,fb;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
  if(use_renderbuffer) {
   glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);targetrb(GL_RENDERBUFFER,image);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
  } else {
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);target(GL_TEXTURE_2D,image);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  }
  CHECK(glGetError()==GL_NO_ERROR);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  GLuint msfb=0,msrb=0;
  if(samples>1) {
   glGenFramebuffers(1,&msfb);glBindFramebuffer(GL_FRAMEBUFFER,msfb);
   glGenRenderbuffers(1,&msrb);glBindRenderbuffer(GL_RENDERBUFFER,msrb);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,64,64);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,msrb);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  }
  glViewport(0,0,64,64);glClearColor(191./255,64./255,16./255,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
  if(samples>1) {
   glBindFramebuffer(GL_READ_FRAMEBUFFER,msfb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fb);
   glBlitFramebuffer(0,0,64,64,0,0,64,64,GL_COLOR_BUFFER_BIT,GL_NEAREST);
   glBindFramebuffer(GL_FRAMEBUFFER,fb);CHECK(glGetError()==GL_NO_ERROR);
  }
  printf("samples=%u\n",samples);
  for(unsigned bgra=0;bgra<2;bgra++)for(unsigned pbo=0;pbo<2;pbo++) {
   unsigned char pixels[64*64*4];memset(pixels,0xa5,sizeof(pixels));GLuint pack=0;
   if(pbo){glGenBuffers(1,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(pixels),pixels,GL_STREAM_READ);}
   glReadPixels(0,0,64,64,bgra?GL_BGRA:GL_RGBA,GL_UNSIGNED_BYTE,pbo?NULL:pixels);
   if(pbo){glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(pixels),pixels);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pack);}
   CHECK(glGetError()==GL_NO_ERROR);unsigned bad=check_pixels(pixels,bgra);total+=bad;
   printf("fourcc=%08x modifier=%llx bgra=%u pbo=%u bad=%u first=%u,%u,%u,%u\n",format,(unsigned long long)mod,bgra,pbo,bad,pixels[0],pixels[1],pixels[2],pixels[3]);
  }
  glFinish();glDeleteFramebuffers(1,&msfb);glDeleteRenderbuffers(1,&msrb);glDeleteFramebuffers(1,&fb);glDeleteTextures(1,&tex);glDeleteRenderbuffers(1,&rb);destroy(d,image);gbm_bo_destroy(bo);
 }
 glDeleteVertexArrays(1,&vao);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
 gbm_device_destroy(gd);close(drm);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);
 printf("%s: DMA-BUF framebuffer readback\n",total?"FAIL":"PASS");return total?1:0;
}
