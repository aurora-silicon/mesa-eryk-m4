/* SPDX-License-Identifier: MIT
 * Exact SuperTuxKart packed vertex / instance formats with indexed drawing.
 * Run inside the J713 watchdog/thermal guard.
 */
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x, \
           eglGetError(), glGetError()); exit(1); } } while (0)

static GLuint
shader(GLenum stage, const char *source)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[4096];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n", log);
   }
   CHECK(ok);
   return s;
}

int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
   CHECK(d!=EGL_NO_DISPLAY && eglInitialize(d,NULL,NULL) && eglBindAPI(EGL_OPENGL_ES_API));
   const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_NONE};
   EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
   CHECK(c!=EGL_NO_CONTEXT && eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
   const char *renderer=(const char *)glGetString(GL_RENDERER);
   fprintf(stderr,"renderer: %s\n",renderer);
   CHECK(renderer && strstr(renderer,"zink") && strstr(renderer,"M4") && !strstr(renderer,"llvmpipe"));
   GLuint vao,vbo,ibo,instances,fbo,color;
   glGenVertexArrays(1,&vao); glBindVertexArray(vao);
   glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
   uint8_t vertices[4][48]={0};
   const float pos[4][3]={{-1,-1,0},{1,-1,0},{-1,1,0},{1,1,0}};
   const uint32_t normal=(511u)|((uint32_t)(-511&1023)<<10)|(1u<<30);
   const uint32_t tangent=((uint32_t)(-512&1023))|(511u<<20)|(3u<<30);
   const uint8_t colour[4]={255,0,255,255};
   const uint16_t uv[2]={0x3800,0xbc00},uv2[2]={0x4000,0x3400};
   const int16_t joints[4]={-32768,-1,3,32767};
   const uint16_t weights[4]={0x3800,0x3400,0x3400,0};
   for(unsigned i=0;i<4;i++) {
      memcpy(vertices[i],pos[i],12); memcpy(vertices[i]+12,&normal,4);
      memcpy(vertices[i]+16,colour,4); memcpy(vertices[i]+20,uv,4);
      memcpy(vertices[i]+24,uv2,4); memcpy(vertices[i]+28,&tangent,4);
      memcpy(vertices[i]+32,joints,8); memcpy(vertices[i]+40,weights,8);
   }
   glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
   const GLenum types[]={GL_FLOAT,GL_INT_2_10_10_10_REV,GL_UNSIGNED_BYTE,GL_HALF_FLOAT,
                          GL_HALF_FLOAT,GL_INT_2_10_10_10_REV,GL_SHORT,GL_HALF_FLOAT};
   const unsigned sizes[]={3,4,4,2,2,4,4,4},offsets[]={0,12,16,20,24,28,32,40};
   for(unsigned i=0;i<8;i++) {
      if(i==6) glVertexAttribIPointer(i,sizes[i],types[i],48,(void *)(uintptr_t)offsets[i]);
      else glVertexAttribPointer(i,sizes[i],types[i],i==1||i==2||i==5,48,(void *)(uintptr_t)offsets[i]);
      glEnableVertexAttribArray(i);
   }
   uint8_t data[4][44]={0};
   const uint16_t scales[4][4]={{0x3c00,0x3800,0xb400,0},{0x3c00,0x3800,0xb400,0x3c00},
                               {0x3c00,0x3800,0xb400,0x4000},{0x3c00,0x3800,0xb400,0x4200}};
   for(unsigned i=0;i<4;i++) {
      float origin[3]={(float)i+1,-2,0.25},rotation[4]={0,0,0,1};
      int16_t trans[2]={-32768,32767},misc[2]={-1-(int)i,17+(int)i};
      memcpy(data[i],origin,12); memcpy(data[i]+12,rotation,16);
      memcpy(data[i]+28,scales[i],8); memcpy(data[i]+36,trans,4); memcpy(data[i]+40,misc,4);
   }
   glGenBuffers(1,&instances); glBindBuffer(GL_ARRAY_BUFFER,instances);
   glBufferData(GL_ARRAY_BUFFER,sizeof(data),data,GL_STATIC_DRAW);
   const GLenum itypes[]={GL_FLOAT,GL_FLOAT,GL_HALF_FLOAT,GL_SHORT,GL_SHORT};
   const unsigned isizes[]={3,4,4,2,2},ioffsets[]={0,12,28,36,40};
   for(unsigned i=0;i<5;i++) {
      if(i==4) glVertexAttribIPointer(8+i,isizes[i],itypes[i],44,(void *)(uintptr_t)ioffsets[i]);
      else glVertexAttribPointer(8+i,isizes[i],itypes[i],i==3,44,(void *)(uintptr_t)ioffsets[i]);
      glEnableVertexAttribArray(8+i); glVertexAttribDivisor(8+i,1);
   }
   const uint16_t indices[]={0xdead,0,1,2,2,1,3};
   glGenBuffers(1,&ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
   glGenTextures(1,&color); glBindTexture(GL_TEXTURE_2D,color); glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,32,32);
   glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,32,32); glDisable(GL_DITHER);
   const char *decl[]={"","vec4","vec4","vec2","vec2","vec4","ivec4","vec4","vec3","vec4","vec4","vec2","ivec2"};
   const char *expected[]={"","vec4(1,-1,0,1)","vec4(1,0,1,1)","vec2(0.5,-1)","vec2(2,0.25)",
      "vec4(-1,0,1,-1)","ivec4(-32768,-1,3,32767)","vec4(0.5,0.25,0.25,0)",
      "vec3(float(gl_InstanceID)+1.0,-2,0.25)","vec4(0,0,0,1)",
      "vec4(1,0.5,-0.25,float(gl_InstanceID))","vec2(-1,1)","ivec2(-1-gl_InstanceID,17+gl_InstanceID)"};
   unsigned failures=0;
   for(unsigned attr=1;attr<=12;attr++) {
      char source[2048];
      snprintf(source,sizeof(source),"#version 300 es\nprecision highp float; precision highp int; "
         "layout(location=0) in vec3 pos; layout(location=%u) in %s value; out vec2 passed; "
         "void main(){gl_Position=vec4(pos.x*0.25-0.75+float(gl_InstanceID)*0.5,pos.y,0,1); "
         "passed=vec2(all(equal(value,%s))?1.0:0.0,pos.x);}",attr,decl[attr],expected[attr]);
      GLuint vs=shader(GL_VERTEX_SHADER,source),fs=shader(GL_FRAGMENT_SHADER,
         "#version 300 es\nprecision highp float; in vec2 passed; out vec4 result; void main(){result=vec4(passed.x,(passed.y+1.0)*0.5,0,1);}");
      GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
      GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok); CHECK(ok);
      glUseProgram(p); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
      glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,(void *)2,4);
      uint8_t pixels[32][32][4]; glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      CHECK(glGetError()==GL_NO_ERROR);
      unsigned bad=0;
      for(unsigned y=0;y<32;y++) for(unsigned x=0;x<32;x++) if(pixels[y][x][0]!=255 || abs((int)pixels[y][x][1]-(int)((x%8+0.5)*255/8+0.5))>1 || pixels[y][x][2]!=0 || pixels[y][x][3]!=255) {
         if(bad<4) fprintf(stderr,"FAIL attribute=%u x=%u y=%u got=%u,%u,%u,%u\n",attr,x,y,
                           pixels[y][x][0],pixels[y][x][1],pixels[y][x][2],pixels[y][x][3]);
         bad++;
      }
      fprintf(stderr,"STK vertex layout attribute %u: 1024 pixels checked, %u failures\n",attr,bad);
      failures+=bad;
      glDeleteProgram(p); glDeleteShader(vs); glDeleteShader(fs);
   }
   glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&color);
   glDeleteBuffers(1,&vbo); glDeleteBuffers(1,&ibo); glDeleteBuffers(1,&instances); glDeleteVertexArrays(1,&vao);
   CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d,c) && eglTerminate(d));
   return failures?1:0;
}
