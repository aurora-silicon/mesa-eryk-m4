/* SPDX-License-Identifier: MIT
 * CPU-checked ordinary/sparse UBOs and shader variant rebinds; use the hardware experiment guard.
 * cc -O2 gl-ubo-variant-probe.c -lEGL -lGL -o gl-ubo-variant-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); exit(1); } } while (0)
static GLuint shader(GLenum type, const char *text) {
   GLuint s = glCreateShader(type); glShaderSource(s, 1, &text, NULL);
   glCompileShader(s); GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log); fputs(log, stderr); }
   CHECK(ok); return s;
}
int main(void) {
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));

   GLuint p = glCreateProgram();
   glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 330 core\nlayout(std140) uniform Values {uvec4 value;};out vec4 color;void main(){color=vec4(value)/255.0;}"));
   glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);
   glUseProgram(p);glUniformBlockBinding(p,glGetUniformBlockIndex(p,"Values"),0);
   GLint n;glGetIntegerv(GL_NUM_EXTENSIONS,&n);int sparse=0;
   for(int i=0;i<n;++i) sparse |= !strcmp((const char *)glGetStringi(GL_EXTENSIONS,i),"GL_ARB_sparse_buffer");
   CHECK(sparse);
   PFNGLBUFFERPAGECOMMITMENTARBPROC commit = (void *)eglGetProcAddress("glBufferPageCommitmentARB");CHECK(commit);
   GLint page;glGetIntegerv(GL_SPARSE_BUFFER_PAGE_SIZE_ARB,&page);CHECK(page>=16);
   GLuint buffers[2];glGenBuffers(2,buffers);
   for(unsigned i=0;i<2;++i){
      glBindBuffer(GL_UNIFORM_BUFFER,buffers[i]);
      glBufferStorage(GL_UNIFORM_BUFFER,page,NULL,GL_DYNAMIC_STORAGE_BIT|(i?GL_SPARSE_STORAGE_BIT_ARB:0));
      if(i)commit(GL_UNIFORM_BUFFER,0,page,GL_TRUE);
      CHECK(glGetError()==GL_NO_ERROR);
   }
   GLuint vao,tex,fb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
   glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,256,4);
   unsigned char pixels[256*4*4];unsigned bad=0;
   for(unsigned round=0;round<16;++round){
      unsigned variant=(round/2)&1;
      unsigned value[4]={(round*73+19)&255,(round*41+7)&255,(round*137+23)&255,255};
      glBindBuffer(GL_UNIFORM_BUFFER,buffers[variant]);
      glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(value),value);
      glBindBufferBase(GL_UNIFORM_BUFFER,0,buffers[variant]);
      glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,256,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      CHECK(glGetError()==GL_NO_ERROR);
      for(unsigned j=0;j<256*4;++j)for(unsigned c=0;c<4;++c)bad+=pixels[j*4+c]!=value[c];
      printf("round=%u sparse=%u bad=%u\n",round,variant,bad);fflush(stdout);
   }
   CHECK(!bad);puts("PASS: ordinary and sparse UBO shader switching and content updates");
   return 0;
}
