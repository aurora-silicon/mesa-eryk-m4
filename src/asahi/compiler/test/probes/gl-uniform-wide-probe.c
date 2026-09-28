/* SPDX-License-Identifier: MIT
 * CPU-checked wide uniform exports: 256 U32 loads, each used with a varying.
 * Tests the full preamble uniform footprint over changing UBO contents.
 * --record-only reports pixel FAIL without requesting watchdog recovery;
 * GL/API errors still return failure. This mode is diagnostic, not a pass.
 * Run only under gpu-experiment.py's watchdog and temperature guard.
 * cc -O2 gl-uniform-wide-probe.c -lEGL -lGL -lm -o gl-uniform-wide-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <inttypes.h>
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
   int record_only=0,gpu_updates=0,partial_coverage=0;
   unsigned width=64,height=4,rounds=32,program_count=1;
   for(int i=1;i<argc;++i) {
      if(!strcmp(argv[i],"--record-only")) record_only=1;
      else if(!strcmp(argv[i],"--gpu-updates")) gpu_updates=1;
      else if(!strcmp(argv[i],"--partial-coverage")) partial_coverage=1;
      else if(!strcmp(argv[i],"--size") && i+2<argc) {
         char *end;
         unsigned long w=strtoul(argv[++i],&end,10); CHECK(*end==0 && w>=1 && w<=4096);
         unsigned long h=strtoul(argv[++i],&end,10); CHECK(*end==0 && h>=1 && h<=4096);
         width=w; height=h;
      } else if(!strcmp(argv[i],"--rounds") && i+1<argc) {
         char *end;
         unsigned long n=strtoul(argv[++i],&end,10); CHECK(*end==0 && n>=1 && n<=32);
         rounds=n;
      } else if(!strcmp(argv[i],"--programs") && i+1<argc) {
         char *end;
         unsigned long n=strtoul(argv[++i],&end,10); CHECK(*end==0 && n>=1 && n<=4);
         program_count=n;
      } else {fprintf(stderr,"Usage: %s [--record-only] [--gpu-updates] [--partial-coverage] [--size WIDTH HEIGHT] [--rounds 1..32] [--programs 1..4]\n",argv[0]);return 1;}
   }
   EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
   CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
   EGLint attr[]={EGL_CONTEXT_MAJOR_VERSION,gpu_updates?4:3,EGL_CONTEXT_MINOR_VERSION,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
   EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attr);
   CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
   printf("Renderer: %s\n",glGetString(GL_RENDERER));
   GLuint programs[4]={0};
   GLint ok;
   for(unsigned program=0;program<program_count;++program) {
   GLuint p=programs[program]=glCreateProgram();
   glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"));
   char fragment[16384];
   size_t len=snprintf(fragment,sizeof(fragment),"#version 330 core\nlayout(std140) uniform Values {uvec4 v[64];};out uvec4 color;void main(){uvec4 a=uvec4(0u);uint x=uint(gl_FragCoord.x);\n");
   for(unsigned i=0;i<64-16*program;++i){
      int n=snprintf(fragment+len,sizeof(fragment)-len,"a+=(v[%u]^uvec4(x*%uu));\n",i,i*37+1);
      CHECK(n>0&&(size_t)n<sizeof(fragment)-len);len+=(size_t)n;
   }
   CHECK(snprintf(fragment+len,sizeof(fragment)-len,"color=a;}")>0);
   glAttachShader(p,shader(GL_FRAGMENT_SHADER,fragment));
   glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);
   glUseProgram(p);glUniformBlockBinding(p,glGetUniformBlockIndex(p,"Values"),0);
   }
   GLuint update=0;
   GLint round_location=-1;
   if(gpu_updates) {
      update=glCreateProgram();
      glAttachShader(update,shader(GL_COMPUTE_SHADER,
         "#version 430 core\nlayout(local_size_x=64) in;layout(std430,binding=0) buffer Values {uvec4 v[64];};uniform uint round_index;\n"
         "const uint seeds[8]=uint[8](0u,1u,0x7fffffffu,0x80000000u,0xffffffffu,0x7fc00000u,0x00010001u,0xdeadbeefu);\n"
         "void main(){uint j=gl_LocalInvocationID.x;for(uint k=0u;k<4u;++k)v[j][k]=seeds[(round_index+j+k)%8u]^(round_index*0x9e3779b9u+j*0x10001u+k);}"));
      glLinkProgram(update);glGetProgramiv(update,GL_LINK_STATUS,&ok);CHECK(ok);
      round_location=glGetUniformLocation(update,"round_index");CHECK(round_location>=0);
   }
   GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_UNIFORM_BUFFER,buffer);
   glBufferData(GL_UNIFORM_BUFFER,1024,NULL,GL_DYNAMIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,buffer);
   GLuint vao,tex,fb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,width,height,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,NULL);
   glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,width,height);
   uint32_t *pixels=malloc((size_t)width*height*4*sizeof(*pixels));
   uint32_t *expected=malloc((size_t)width*4*sizeof(*expected));
   CHECK(pixels && expected);
   uint64_t bad=0;
   for(unsigned round=0;round<rounds;++round){
      unsigned program=round%program_count;
      const uint32_t seeds[]={0,1,0x7fffffff,0x80000000,0xffffffff,0x7fc00000,0x00010001,0xdeadbeef};
      uint32_t v[64][4];
      for(unsigned j=0;j<64;++j)for(unsigned k=0;k<4;++k)
         v[j][k]=seeds[(round+j+k)%8]^(round*0x9e3779b9u+j*0x10001u+k);
      if(gpu_updates) {
         glUseProgram(update);glUniform1ui(round_location,round);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);glDispatchCompute(1,1,1);
         glMemoryBarrier(GL_UNIFORM_BARRIER_BIT);
      } else glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(v),v);
      glUseProgram(programs[program]);
      unsigned vx=0,vy=0,vw=width,vh=height;
      if(partial_coverage) {
         const unsigned sizes[]={1,2,3,5,31,32,33,127};
         vw=sizes[round%8]<width?sizes[round%8]:width;
         vh=sizes[(round+3)%8]<height?sizes[(round+3)%8]:height;
         vx=(width-vw)/2;vy=(height-vh)/2;
         const GLuint zero[4]={0,0,0,0};
         glClearBufferuiv(GL_COLOR,0,zero);
      }
      glViewport(vx,vy,vw,vh);
      glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,width,height,GL_RGBA_INTEGER,GL_UNSIGNED_INT,pixels);CHECK(glGetError()==GL_NO_ERROR);
      for(unsigned x=0;x<width;++x)for(unsigned k=0;k<4;++k){
         expected[x*4+k]=0;
         for(unsigned i=0;i<64-16*program;++i)expected[x*4+k]+=(v[i][k]^(x*(i*37u+1u)));
      }
      for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned k=0;k<4;++k){
         uint32_t expect=(x>=vx && x<vx+vw && y>=vy && y<vy+vh)?expected[x*4+k]:0;
         uint32_t actual=pixels[(y*width+x)*4+k];
         if(actual!=expect){if(bad<8)printf("round=%u x=%u y=%u c=%u actual=%08x expected=%08x\n",round,x,y,k,actual,expect);bad++;}
      }
      printf("round=%u program=%u bad=%"PRIu64"\n",round,program,bad);fflush(stdout);
   }
   printf("%s: U32 wide-uniform export oracle; updates=%s programs=%u partial=%d size=%ux%u rounds=%u values=%"PRIu64" mismatches=%"PRIu64"\n",bad?"FAIL":"PASS",gpu_updates?"gpu":"cpu",program_count,partial_coverage,width,height,rounds,(uint64_t)width*height*4*rounds,bad);
   free(expected);free(pixels);
   if(!record_only)CHECK(!bad);
   return 0;
}
