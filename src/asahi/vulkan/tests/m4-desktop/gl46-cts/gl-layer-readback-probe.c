/* SPDX-License-Identifier: MIT
 * Diagnose layered 3D clears and readback. Run through gpu-experiment.py.
 * Build: cc -O2 gl-layer-readback-probe.c -lEGL -lGL -o gl-layer-readback-probe
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError());return 1;}}while(0)
static GLuint program(void) {
 const char *texts[]={"#version 460 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}","#version 460 core\nlayout(binding=0) uniform sampler3D src;uniform float z;out vec4 color;void main(){color=textureLod(src,vec3(.25,.25,z),0);}"};
 GLuint p=glCreateProgram();for(unsigned i=0;i<2;i++){GLuint sh=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);glShaderSource(sh,1,&texts[i],NULL);glCompileShader(sh);glAttachShader(p,sh);}glLinkProgram(p);return p;
}
int main(void) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,6,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 GLuint fb;glCreateFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
 GLuint prog=program(),vao,sampletex,samplefb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glCreateTextures(GL_TEXTURE_2D,1,&sampletex);glTextureStorage2D(sampletex,1,GL_RGBA8,1,1);glCreateFramebuffers(1,&samplefb);glNamedFramebufferTexture(samplefb,GL_COLOR_ATTACHMENT0,sampletex,0);unsigned bad=0;const unsigned depths[]={192,2048,8192,16384};
 GLint max_depth;glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE,&max_depth);printf("GL_MAX_3D_TEXTURE_SIZE=%d\n",max_depth);
 const unsigned layers[]={0,1,1,2,3,5,8,13,21,34,55,89,144};
 for(unsigned t=0;t<4;t++){
  if(depths[t]>(unsigned)max_depth&&!getenv("PROBE_OVER_LIMIT"))continue;
  GLuint tex;glCreateTextures(GL_TEXTURE_3D,1,&tex);glTextureStorage3D(tex,1,GL_RGBA8,2,2,depths[t]);CHECK(glGetError()==0);
  if(getenv("PROBE_TAGS")){glBindFramebuffer(GL_FRAMEBUFFER,fb);for(unsigned z=0;z<146;z++){glNamedFramebufferTextureLayer(fb,GL_COLOR_ATTACHMENT0,tex,0,z);glClearColor((z+1)/255.f,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);}}
  for(unsigned k=0;k<sizeof(layers)/sizeof(layers[0]);k++){
   glBindFramebuffer(GL_FRAMEBUFFER,fb);glNamedFramebufferTextureLayer(fb,GL_COLOR_ATTACHMENT0,tex,0,layers[k]);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glClearColor(getenv("PROBE_TAGS")?(layers[k]+1)/255.f:.25,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);
   unsigned char a[4]={0},b[4]={0};if(getenv("PROBE_FLOAT")){float v[4];glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,v);for(unsigned j=0;j<4;j++)a[j]=(unsigned char)(v[j]*255+0.5f);}else glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,a);
   if(!getenv("PROBE_READ_ONLY"))glGetTextureSubImage(tex,0,0,0,layers[k],1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,4,b);else for(unsigned j=0;j<4;j++)b[j]=a[j];CHECK(glGetError()==0);
   if(getenv("PROBE_SAMPLE")){glBindFramebuffer(GL_FRAMEBUFFER,samplefb);glUseProgram(prog);glBindTextureUnit(0,tex);glTextureParameteri(tex,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTextureParameteri(tex,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glUniform1f(glGetUniformLocation(prog,"z"),(layers[k]+.5f)/depths[t]);glViewport(0,0,1,1);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,a);CHECK(glGetError()==0);}
   printf("depth=%u layer=%u read=%u,%u,%u,%u get=%u,%u,%u,%u\n",depths[t],layers[k],a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]);
   if(a[0]!=(getenv("PROBE_TAGS")?layers[k]+1:64)||a[1]!=128||a[2]!=191||a[3]!=255||b[0]!=(getenv("PROBE_TAGS")?layers[k]+1:64)||b[1]!=128||b[2]!=191||b[3]!=255)bad++;glBindFramebuffer(GL_FRAMEBUFFER,0);
  }glDeleteTextures(1,&tex);
 }
 printf("%s: layer readback (%u failures)\n",bad?"FAIL":"PASS",bad);return bad!=0;
}
