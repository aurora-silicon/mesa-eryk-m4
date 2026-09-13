/* SPDX-License-Identifier: MIT
 * Geometry shader exporting distinct colors to four render-target layers.
 * Hardware execution requires gpu-experiment.py with watchdog/thermal guards.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError()); exit(1); } } while(0)
static GLuint compile(GLenum stage, const char *s) {
 GLuint shader=glCreateShader(stage);glShaderSource(shader,1,&s,NULL);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return shader;
}

int main(int argc, char **argv) {
 const int depth=argc==2 && !strcmp(argv[1],"depth");CHECK(argc==1||depth);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint tex,fb,vao;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D_ARRAY,tex);
 glTexImage3D(GL_TEXTURE_2D_ARRAY,0,depth?GL_DEPTH_COMPONENT32F:GL_RGBA32F,8,8,4,0,depth?GL_DEPTH_COMPONENT:GL_RGBA,GL_FLOAT,NULL);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture(GL_FRAMEBUFFER,depth?GL_DEPTH_ATTACHMENT:GL_COLOR_ATTACHMENT0,tex,0);if(depth){glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);}CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 GLuint p=glCreateProgram();
 glAttachShader(p,compile(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
 glAttachShader(p,compile(GL_GEOMETRY_SHADER,"#version 330 core\nlayout(triangles) in;layout(triangle_strip,max_vertices=12) out;flat out vec4 color;void main(){for(int layer=0;layer<4;layer++){for(int v=0;v<3;v++){gl_Position=gl_in[v].gl_Position;gl_Position.z=float(layer)*.5-1.;gl_Layer=layer;color=vec4(float(layer+1)*.25,.5,.75,1);EmitVertex();}EndPrimitive();}}"));
 glAttachShader(p,compile(GL_FRAGMENT_SHADER,depth?"#version 330 core\nvoid main(){}":"#version 330 core\nflat in vec4 color;out vec4 outcolor;void main(){outcolor=color;}"));
 glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,8,8);glClearColor(0,0,0,0);glClear(depth?GL_DEPTH_BUFFER_BIT:GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
 float actual[8*8*4*4];glGetTexImage(GL_TEXTURE_2D_ARRAY,0,depth?GL_DEPTH_COMPONENT:GL_RGBA,GL_FLOAT,actual);CHECK(glGetError()==GL_NO_ERROR);
 unsigned bad=0;for(unsigned layer=0;layer<4;layer++)for(unsigned i=0;i<64;i++)for(unsigned c=0;c<(depth?1:4);c++){
  float expected=depth?layer*.25f:c? (c+1)*.25f:(layer+1)*.25f;unsigned idx=(layer*64+i)*(depth?1:4)+c;
  if(actual[idx]!=expected){if(bad<4)fprintf(stderr,"layer%u pixel%u channel%u=%g expected%g\n",layer,i,c,actual[idx],expected);bad++;}
 }
 printf("%s: layered geometry exact 256 pixels, bad=%u\n",bad?"FAIL":"PASS",bad);
 glDeleteProgram(p);glDeleteTextures(1,&tex);glDeleteFramebuffers(1,&fb);glDeleteVertexArrays(1,&vao);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,ctx);eglTerminate(d);return !!bad;
}
