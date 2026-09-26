/* SPDX-License-Identifier: MIT
 * CPU-checked draw-uniform F32 comparison preamble preservation, including changed uniforms.
 * --record-only reports pixel FAIL without requesting watchdog recovery;
 * GL/API errors still return failure. This mode is diagnostic, not a pass.
 * Run only under gpu-experiment.py's watchdog and temperature guard.
 * cc -O2 gl-uniform-float-bool-probe.c -lEGL -lGL -lm -o gl-uniform-bits-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError()); exit(1); } } while(0)
static GLuint shader(GLenum type,const char *text) {
   GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);
   GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
   if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
   CHECK(ok);return s;
}
int main(int argc, char **argv) {
   int record_only=argc==2&&!strcmp(argv[1],"--record-only");
   int flush_subnormals=argc==2&&!strcmp(argv[1],"--flush-subnormals");
   CHECK(argc==1||record_only||flush_subnormals);
   EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
   CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
   EGLint attr[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
   EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attr);
   CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
   printf("Renderer: %s\n",glGetString(GL_RENDERER));
   GLuint p=glCreateProgram();
   glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 330 core\nlayout(std140) uniform Values {uvec4 a;uvec4 b;uvec4 c;uvec4 d;};out uvec4 color;void main(){uvec4 v=a; vec4 q=uintBitsToFloat(v), r=uintBitsToFloat(d); bvec4 choice=bvec4(q.x<r.x,q.y>=r.y,q.z==r.z,q.w!=r.w); uint x=uint(gl_FragCoord.x); uint lo=x+11u, hi=(x*x)^0xcafe1234u; color=uvec4(choice.x?hi:lo,choice.y?hi:lo,choice.z?hi:lo,choice.w?hi:lo);}"));
   glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);
   glUseProgram(p);glUniformBlockBinding(p,glGetUniformBlockIndex(p,"Values"),0);
   GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_UNIFORM_BUFFER,buffer);
   glBufferData(GL_UNIFORM_BUFFER,64,NULL,GL_DYNAMIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,buffer);
   GLuint vao,tex,fb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,64,4,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,NULL);
   glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,64,4);uint32_t pixels[64*4*4];unsigned bad=0;
   for(unsigned round=0;round<144;++round){
      const uint32_t seeds[]={0,0x80000000,1,0x80000001,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00000,0xffc12345,0x7fa00001,0x007fffff};
      uint32_t v[4][4];
      for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)
         v[j][k]=seeds[((j==0?round/12:round%12)+k)%12];
      glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(v),v);glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,64,4,GL_RGBA_INTEGER,GL_UNSIGNED_INT,pixels);CHECK(glGetError()==GL_NO_ERROR);
      for(unsigned y=0;y<4;++y)for(unsigned x=0;x<64;++x)for(unsigned k=0;k<4;++k){
         uint32_t calc=v[0][k];
         uint32_t rhs_bits=v[3][k];
         if(flush_subnormals){
            if(!(calc&0x7f800000u))calc&=0x80000000u;
            if(!(rhs_bits&0x7f800000u))rhs_bits&=0x80000000u;
         }
         float lhs,rhs; memcpy(&lhs,&calc,4); memcpy(&rhs,&rhs_bits,4);
         int choice=k==0?lhs<rhs:k==1?lhs>=rhs:k==2?lhs==rhs:lhs!=rhs;
         uint32_t expect=choice?((x*x)^0xcafe1234u):x+11u;
         uint32_t actual=pixels[(y*64+x)*4+k];
         if(actual!=expect){if(bad<8)printf("round=%u x=%u y=%u c=%u actual=%08x expected=%08x\n",round,x,y,k,actual,expect);bad++;}
      }
      printf("round=%u bad=%u\n",round,bad);fflush(stdout);
   }
   printf("%s: draw-uniform F32 comparison oracle (%s); mismatches=%u\n",bad?"FAIL":"PASS",flush_subnormals?"flush-subnormals":"strict IEEE",bad);
   if(!record_only)CHECK(!bad);
   return 0;
}
