/* SPDX-License-Identifier: MIT
 * CPU-checked maximum varying transport, including live clip distances.
 * Run only through gpu-experiment.py. Build: cc -O2 FILE -lEGL -lGL -o OUT
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError());exit(1); } } while(0)
static GLuint shader(GLenum stage,const char *text) {
 GLuint s=glCreateShader(stage);glShaderSource(s,1,&text,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return s;
}
int main(int argc, char **argv) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,6,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 printf("Renderer: %s\n",glGetString(GL_RENDERER));
 GLuint vao,tex,fb,ubo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1,1,0,GL_RGBA,GL_FLOAT,NULL);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,1,1);
 glGenBuffers(1,&ubo);glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,33*16,NULL,GL_DYNAMIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
 /* Position consumes four of the 128 output components. Larger explicit
  * arguments are retained for guarded negative diagnostics only. */
 unsigned configs[][2]={{29,0},{31,0},{29,8}};
 unsigned num_configs=sizeof(configs)/sizeof(configs[0]);
 if(argc>1){configs[0][0]=atoi(argv[1]);configs[0][1]=argc>2?atoi(argv[2]):0;num_configs=1;CHECK(configs[0][0]>0&&configs[0][0]<=32);}
 unsigned draws=0;
 for(unsigned t=0;t<num_configs;t++) {
  unsigned n=configs[t][0],clips=configs[t][1];char vs[8192],fs[8192],clipvs[256]="",clipfs[512]="";
  if(clips){snprintf(clipvs,sizeof(clipvs),"for(int j=0;j<%u;j++)gl_ClipDistance[j]=extra.x+float(j);",clips);snprintf(clipfs,sizeof(clipfs),"for(int j=0;j<%u;j++) color.w+=gl_ClipDistance[j];",clips);}
  snprintf(vs,sizeof(vs),"#version 460 core\nlayout(std140,binding=0)uniform Data{vec4 v[32];vec4 extra;};flat out vec4 a[%u];%s void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);for(int i=0;i<%u;i++)a[i]=v[i];%s}",n,clips?"out float gl_ClipDistance[8];":"",n,clipvs);
  snprintf(fs,sizeof(fs),"#version 460 core\nflat in vec4 a[%u];%s uniform int pick;out vec4 color;void main(){color=a[pick];%s}",n,clips?"in float gl_ClipDistance[8];":"",clipfs);
  if(getenv("PROBE_SUM"))snprintf(fs,sizeof(fs),"#version 460 core\nflat in vec4 a[%u];out vec4 color;void main(){color=vec4(0);for(int i=0;i<%u;i++)color+=a[i];}",n,n);
  GLuint p=glCreateProgram(),v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);glUseProgram(p);
  for(unsigned k=0;k<8;k++){if(k<clips)glEnable(GL_CLIP_DISTANCE0+k);else glDisable(GL_CLIP_DISTANCE0+k);}
  for(unsigned round=0;round<2;round++) {
   float data[33][4];for(unsigned i=0;i<33;i++)for(unsigned j=0;j<4;j++)data[i][j]=(i*4+j+1+round*137)*0.125f;data[32][0]=round+1;
   glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(data),data);
   for(unsigned i=0;i<n;i++){glUniform1i(glGetUniformLocation(p,"pick"),i);glDrawArrays(GL_TRIANGLES,0,3);float out[4];glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,out);CHECK(glGetError()==GL_NO_ERROR);for(unsigned j=0;j<4;j++){float expect=data[i][j];if(getenv("PROBE_SUM")){expect=0;for(unsigned k=0;k<n;k++)expect+=data[k][j];}if(j==3)for(unsigned k=0;k<clips;k++)expect+=data[32][0]+k;if(out[j]!=expect)fprintf(stderr,"n=%u clips=%u i=%u j=%u actual=%a expected=%a\n",n,clips,i,j,out[j],expect);CHECK(out[j]==expect);}draws++;}
  }
  printf("PASS n=%u clip=%u\n",n,clips);fflush(stdout);glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
 }
 printf("PASS: maximum varying transport (%u draws)\n",draws);return 0;
}
