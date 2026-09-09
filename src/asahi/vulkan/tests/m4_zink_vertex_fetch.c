/* SPDX-License-Identifier: MIT
 * Exact pixels from vertex-stage texelFetch, with attribute and root uniforms
 * live at the same time. Run only inside the J713 watchdog/thermal guard.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x, eglGetError(), glGetError()); exit(1); } } while (0)
static GLuint compile(GLenum stage, const char *source) {
 GLuint s=glCreateShader(stage); glShaderSource(s,1,&source,NULL); glCompileShader(s);
 GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if (!ok) { char log[4096]; glGetShaderInfoLog(s,sizeof(log),NULL,log); fprintf(stderr,"%s\n",log); }
 CHECK(ok); return s;
}
int main(int argc, char **argv) {
 const int signed_attributes=argc==2 && !strcmp(argv[1],"--signed-attributes");
 const int attributes_only=signed_attributes || (argc==2 && !strcmp(argv[1],"--attributes-only"));
 CHECK(argc==1 || attributes_only);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
 CHECK(d!=EGL_NO_DISPLAY && eglInitialize(d,NULL,NULL) && eglBindAPI(EGL_OPENGL_ES_API));
 const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,0,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,ca);
 CHECK(c!=EGL_NO_CONTEXT && eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char *)glGetString(GL_RENDERER);
 fprintf(stderr,"renderer: %s\n",renderer);
 CHECK(renderer && strstr(renderer,"zink") && strstr(renderer,"M4") && !strstr(renderer,"llvmpipe"));
 const char *vs=signed_attributes ?
 "#version 300 es\nlayout(location=0) in vec2 pos; layout(location=1) in ivec4 data; uniform ivec4 reference; uniform vec4 value; out vec4 colour; void main(){gl_Position=vec4(pos,0,1);colour=all(equal(data,reference))?value:vec4(1,0,1,1); }\n" : attributes_only ?
 "#version 300 es\nlayout(location=0) in vec2 pos; uniform vec4 value; out vec4 colour; void main(){gl_Position=vec4(pos,0,1);colour=value;}\n" :
 "#version 300 es\nprecision highp float; precision highp sampler2D; layout(location=0) in vec2 pos; uniform sampler2D tex; uniform ivec2 coord; out vec4 colour; void main(){gl_Position=vec4(pos,0,1);colour=texelFetch(tex,coord,0);}\n";
 const char *fs="#version 300 es\nprecision highp float; in vec4 colour; layout(location=0) out vec4 result; void main(){result=colour;}\n";
 GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();
 glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint linked;glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);glUseProgram(p);
 const float quad[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
 GLuint vao,vbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
 GLuint integers=0;
 if(signed_attributes) {glGenBuffers(1,&integers);glBindBuffer(GL_ARRAY_BUFFER,integers);glVertexAttribIPointer(1,4,GL_SHORT,0,NULL);glEnableVertexAttribArray(1);}
 const uint8_t colours[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}};
 GLuint tex,target,fbo;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,colours);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,32,32);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glBindTexture(GL_TEXTURE_2D,tex);glViewport(0,0,32,32);glDisable(GL_DITHER);
 GLint value=glGetUniformLocation(p,attributes_only?"value":"coord");CHECK(value>=0);
 if(!attributes_only)glUniform1i(glGetUniformLocation(p,"tex"),0);
 for(unsigned frame=0;frame<4;frame++) {
  if(signed_attributes) {
   const int16_t values[4]={-32768+(int)frame,-1-(int)frame,(int)frame,32767-(int)frame};
   int16_t vertices[6][4];for(unsigned j=0;j<6;j++)memcpy(vertices[j],values,sizeof(values));
   glBindBuffer(GL_ARRAY_BUFFER,integers);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
   glUniform4i(glGetUniformLocation(p,"reference"),values[0],values[1],values[2],values[3]);
  }
  glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
  if(attributes_only)glUniform4f(value,colours[frame][0]/255.f,colours[frame][1]/255.f,colours[frame][2]/255.f,1);
  else glUniform2i(value,frame&1,frame>>1);
  glDrawArrays(GL_TRIANGLES,0,6);uint8_t pixels[32*32][4];glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned i=0;i<32*32;i++)if(memcmp(pixels[i],colours[frame],4)) {
   fprintf(stderr,"FAIL frame=%u pixel=%u got=%u,%u,%u,%u\n",frame,i,pixels[i][0],pixels[i][1],pixels[i][2],pixels[i][3]);return 1;
  }
 }
 printf("PASS: %s, 4 draws, 4096 exact pixels\n",signed_attributes?"signed 16-bit vertex attributes":attributes_only?"vertex attributes and root uniforms":"vertex texelFetch, attributes and root uniforms");
 if(integers)glDeleteBuffers(1,&integers);
 glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&target);glDeleteTextures(1,&tex);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
 CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));CHECK(eglDestroyContext(d,c));CHECK(eglTerminate(d));return 0;
}
