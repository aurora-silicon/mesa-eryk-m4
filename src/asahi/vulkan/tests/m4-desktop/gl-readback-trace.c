/* SPDX-License-Identifier: MIT
 * Diagnostic EGL GL-readback interceptor. Resolves the original entry point,
 * preserves call arguments/results, and logs format/pack layout plus one CPU
 * pixel. Use only in a bounded guarded GL 3 / GLES 3 application run.
 * cc -shared -fPIC -O2 gl-readback-trace.c -o gl-readback-trace.so -ldl
 */
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*read_fn)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
static read_fn original_read;
static __eglMustCastToProperFunctionPointerType (*original_getproc)(const char*);
static void *real_symbol(void *handle,const char *name) {
#ifdef __aarch64__
 const char *version="GLIBC_2.17";
#else
 const char *version="GLIBC_2.2.5";
#endif
 void *(*lookup)(void*,const char*)=dlvsym(RTLD_NEXT,"dlsym",version);
 return lookup(handle,name);
}
static void resolve(void) {
 if(!original_getproc) original_getproc=real_symbol(RTLD_NEXT,"eglGetProcAddress");
 if(!original_read) original_read=(read_fn)original_getproc("glReadPixels");
}
void glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void *pixels) {
 resolve();
 void (*getint)(GLenum,GLint*)=(void*)original_getproc("glGetIntegerv");
 GLint pack=0,row=0,align=4,skiprow=0,skippixel=0;
 getint(GL_PIXEL_PACK_BUFFER_BINDING,&pack);
 getint(GL_PACK_ROW_LENGTH,&row);getint(GL_PACK_ALIGNMENT,&align);
 getint(GL_PACK_SKIP_ROWS,&skiprow);getint(GL_PACK_SKIP_PIXELS,&skippixel);
 original_read(x,y,w,h,format,type,pixels);
 fprintf(stderr,"M4_GL_READBACK x=%d y=%d w=%d h=%d format=0x%x type=0x%x pack=%d row=%d align=%d skip=%d,%d",x,y,w,h,format,type,pack,row,align,skiprow,skippixel);
 if(!pack && pixels && w>0 && h>0 && type==GL_UNSIGNED_BYTE && (format==GL_RGBA || format==GL_BGRA)) {
  size_t stride=((size_t)(row?row:w)*4+align-1)&~((size_t)align-1);
  const uint8_t *p=(const uint8_t*)pixels+(skiprow+h/2)*stride+(skippixel+w/2)*4;
  fprintf(stderr," center=%u,%u,%u,%u",p[0],p[1],p[2],p[3]);
  const char *directory=getenv("M4_GL_READBACK_TRACE_DIR");
  if(directory && (size_t)w*h*4<=16*1024*1024) {
   static unsigned sequence;
   char filename[4096];
   snprintf(filename,sizeof(filename),"%s/readback-%d-%u-%dx%d-%x.bin",directory,getpid(),__atomic_fetch_add(&sequence,1,__ATOMIC_RELAXED),w,h,format);
   FILE *f=fopen(filename,"wb");
   if(f) {
    for(int r=0;r<h;r++) fwrite((const uint8_t*)pixels+(skiprow+r)*stride+skippixel*4,4,w,f);
    fclose(f);fprintf(stderr," raw=%s",filename);
   }
  }
 }
 fputc('\n',stderr);
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name) {
 resolve();
 if(!strcmp(name,"glReadPixels")) return (__eglMustCastToProperFunctionPointerType)glReadPixels;
 return original_getproc(name);
}

/* Firefox resolves EGL/GL through explicit library handles, which bypasses
 * plain symbol interposition. Preserve that lookup, wrapping only these two
 * existing symbols and only when the original lookup succeeded. */
void *dlsym(void *handle,const char *name) {
 void *result=real_symbol(handle,name);
 if(result && !strcmp(name,"glReadPixels")) {
  original_read=(read_fn)result;return (void*)glReadPixels;
 }
 if(result && !strcmp(name,"eglGetProcAddress")) {
  original_getproc=result;return (void*)eglGetProcAddress;
 }
 return result;
}
