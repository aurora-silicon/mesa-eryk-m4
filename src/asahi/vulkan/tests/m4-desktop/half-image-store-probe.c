/* SPDX-License-Identifier: MIT
 * Exact half-float imageStore/readback via Zink, compute and fragment stages.
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
int main(int argc,char **argv) {
 int fragment=argc==2 && !strcmp(argv[1],"fragment");CHECK(argc==1 || fragment);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 GLuint program=glCreateProgram();char src[2048];
 snprintf(src,sizeof(src),"#version 330 core\n#extension GL_ARB_shader_image_load_store: require\n%s\nlayout(rgba16f,binding=0) uniform writeonly image2D dst;\nvoid main(){ivec2 p=%s;imageStore(dst,p,vec4(float(p.x)*.25-4.,float(p.y)*.5,64.,.125));%s}\n",
   fragment?"#extension GL_ARB_shading_language_420pack: require\nout vec4 color;":"#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shading_language_420pack: require\nlayout(local_size_x=8,local_size_y=8) in;",
   fragment?"ivec2(gl_FragCoord.xy)":"ivec2(gl_GlobalInvocationID.xy)",fragment?"color=vec4(1,0,0,1);":"");
 if(fragment)glAttachShader(program,compile(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"));
 glAttachShader(program,compile(fragment?GL_FRAGMENT_SHADER:GL_COMPUTE_SHADER,src));
 glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
 float pixels[32*32*4];for(unsigned i=0;i<32*32*4;i++)pixels[i]=-16.f;
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,32,32,0,GL_RGBA,GL_FLOAT,pixels);
 glBindImageTexture(0,tex,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA16F);CHECK(glGetError()==GL_NO_ERROR);
 GLuint vao=0,fb=0,rb=0;
 if(fragment){glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,32,32);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,32,32);glDrawArrays(GL_TRIANGLES,0,3);}
 else glDispatchCompute(4,4,1);
 glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels);CHECK(glGetError()==GL_NO_ERROR);
 unsigned bad=0;for(unsigned i=0;i<1024;i++){
  float expected[]={((int)(i%32))*.25f-4.f,(i/32)*.5f,64.f,.125f};
  if(memcmp(pixels+4*i,expected,16)){if(bad<3)fprintf(stderr,"pixel %u: %g,%g,%g,%g\n",i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3]);bad++;}
 }
 printf("%s: %s RGBA16F imageStore exact 1024 pixels (%u mismatches)\n",bad?"FAIL":"PASS",fragment?"fragment":"compute",bad);
 glDeleteProgram(program);glDeleteTextures(1,&tex);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fb);glDeleteVertexArrays(1,&vao);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return bad?1:0;
}
