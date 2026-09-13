/* SPDX-License-Identifier: MIT
 * Eevee image-operation oracle through Zink: exact 3D texel fetch, typed
 * 2D/3D/array stores. Run only through gpu-experiment.py on hardware.
 * cc -O2 -Wall -Wextra eevee-image-probe.c -o eevee-image-probe -lEGL -lGL
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
 CHECK(argc==2);
 int fetch=!strcmp(argv[1],"fetch3d"), array=!strcmp(argv[1],"array");
 int volume=fetch||!strcmp(argv[1],"3d");
 int integer=!strcmp(argv[1],"uint");
 CHECK(fetch||array||volume||integer||!strcmp(argv[1],"float"));
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 const unsigned count=8*8*4,bytes=count*4*sizeof(float);
 float reference[count*4],actual[count*4];unsigned expected_uint[count*4];
 for(unsigned i=0;i<count*4;i++){reference[i]=(float)i*.25f-16.f;expected_uint[i]=0x81000301u+i*65537u;}
 GLuint tex,buf;glGenTextures(1,&tex);glGenBuffers(1,&buf);
 GLenum target=volume?GL_TEXTURE_3D:array?GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D;
 GLenum format=integer?GL_RGBA32UI:GL_RGBA32F;
 glBindTexture(target,tex);
 if(volume||array)glTexImage3D(target,0,format,8,8,4,0,GL_RGBA,GL_FLOAT,fetch?reference:NULL);
 else glTexImage2D(target,0,format,8,32,0,integer?GL_RGBA_INTEGER:GL_RGBA,integer?GL_UNSIGNED_INT:GL_FLOAT,NULL);
 glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,integer?(void*)expected_uint:fetch?NULL:(void*)reference,GL_DYNAMIC_COPY);
 glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
 char src[2048];snprintf(src,sizeof(src),"#version 330 core\n#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shader_storage_buffer_object: require\n#extension GL_ARB_shader_image_load_store: require\n#extension GL_ARB_shading_language_420pack: require\nlayout(local_size_x=4,local_size_y=4,local_size_z=1) in;\nlayout(std430,binding=0) buffer Data{%svec4 values[];};\n%s\nvoid main(){uvec3 p=gl_GlobalInvocationID;uint i=(p.z*%uu+p.y)*8u+p.x;%s;}\n",integer?"u":"",fetch?"uniform sampler3D src;":volume?"layout(rgba32f,binding=0) uniform writeonly image3D dst;":array?"layout(rgba32f,binding=0) uniform writeonly image2DArray dst;":integer?"layout(rgba32ui,binding=0) uniform writeonly uimage2D dst;":"layout(rgba32f,binding=0) uniform writeonly image2D dst;",volume||array?8:32,fetch?"values[i]=texelFetch(src,ivec3(p),0)":volume||array?"imageStore(dst,ivec3(p),values[i])":"imageStore(dst,ivec2(p.xy),values[i])");
 GLuint program=glCreateProgram();glAttachShader(program,compile(GL_COMPUTE_SHADER,src));glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 if(fetch)glUniform1i(glGetUniformLocation(program,"src"),0);
 else glBindImageTexture(0,tex,0,volume||array,0,GL_WRITE_ONLY,format);
 CHECK(glGetError()==GL_NO_ERROR);glDispatchCompute(2,volume||array?2:8,volume||array?4:1);
 glMemoryBarrier(fetch?GL_BUFFER_UPDATE_BARRIER_BIT:GL_TEXTURE_UPDATE_BARRIER_BIT);
 if(fetch)glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,bytes,actual);
 else glGetTexImage(target,0,integer?GL_RGBA_INTEGER:GL_RGBA,integer?GL_UNSIGNED_INT:GL_FLOAT,actual);
 CHECK(glGetError()==GL_NO_ERROR);
 unsigned bad=0;const void *expected=integer?(void*)expected_uint:(void*)reference;
 for(unsigned i=0;i<count*4;i++)if(memcmp((char*)expected+4*i,(char*)actual+4*i,4)){if(bad<4)fprintf(stderr,"word %u: actual=%g expected=%g\n",i,actual[i],reference[i]);bad++;}
 printf("%s: %s exact %u texels (%u mismatched words)\n",bad?"FAIL":"PASS",argv[1],count,bad);
 glDeleteProgram(program);glDeleteTextures(1,&tex);glDeleteBuffers(1,&buf);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return !!bad;
}
