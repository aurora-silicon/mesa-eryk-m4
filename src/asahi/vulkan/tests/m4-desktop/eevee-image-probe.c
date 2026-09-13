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
 int atomic=!strcmp(argv[1],"atomic")||!strcmp(argv[1],"atomic-array");
 int oob=!strcmp(argv[1],"array-oob");
 int fetch=!strcmp(argv[1],"fetch3d"), array=oob||!strcmp(argv[1],"array")||!strcmp(argv[1],"atomic-array");
 int hdr=!strcmp(argv[1],"hdr")||!strcmp(argv[1],"hdr3d");
 int volume=fetch||!strcmp(argv[1],"3d")||!strcmp(argv[1],"hdr3d");
 int quad=getenv("M4_TEST_QUAD_WRITES")!=NULL; CHECK(!quad||!strcmp(argv[1],"float"));
 int array_half=getenv("M4_TEST_ARRAY_RGBA16F")!=NULL; CHECK(!array_half||(array&&!atomic&&!oob));
 int coord_colors=getenv("M4_TEST_COORD_COLORS")!=NULL; CHECK(!coord_colors||(!strcmp(argv[1],"array")));
 int layer_views=array && getenv("M4_TEST_2D_LAYER_VIEWS"); CHECK(!layer_views||!oob);
 int integer=atomic||!strcmp(argv[1],"uint"), half=!strcmp(argv[1],"half");
 CHECK(fetch||array||volume||integer||half||hdr||!strcmp(argv[1],"float"));
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
 CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 const char *renderer=(const char*)glGetString(GL_RENDERER);printf("Renderer: %s\n",renderer);fflush(stdout);
 CHECK((strstr(renderer,"zink")&&strstr(renderer,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(renderer,"llvmpipe")));
 const unsigned width=getenv("M4_TEST_WIDTH")?atoi(getenv("M4_TEST_WIDTH")):8; CHECK(width==8||width==32);
 CHECK(width==8 || (array && !getenv("M4_TEST_SAMPLE_READBACK")));
 const unsigned layers=getenv("M4_TEST_LAYERS")?atoi(getenv("M4_TEST_LAYERS")):4; CHECK(layers==1||layers==4||layers==16); CHECK(layers==4||array); CHECK(layers==4||(!oob&&!layer_views&&!getenv("M4_TEST_SAMPLE_READBACK")));
 CHECK(!layer_views||(!getenv("M4_TEST_UPLOAD_ONLY")&&!coord_colors));
 const unsigned count=width*width*layers,bytes=count*4*sizeof(float);
 float reference[count*4],actual[count*4];unsigned expected_uint[count*4];
 for(unsigned i=0;i<count*4;i++){reference[i]=(float)i*.25f-16.f;expected_uint[i]=0x81000301u+i*65537u;}
 if(hdr)for(unsigned i=0;i<count*4;i++)reference[i]=(i%33)*.25f;
 if(coord_colors)for(unsigned i=0;i<count;i++){
  reference[4*i]=i%width;reference[4*i+1]=(i/width)%width;
  reference[4*i+2]=i/(width*width);reference[4*i+3]=1;
 }
 GLuint tex,buf;glGenTextures(1,&tex);glGenBuffers(1,&buf);
 GLenum target=volume?GL_TEXTURE_3D:array?GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D;
 memset(actual,0,bytes);
 GLenum format=atomic?GL_R32UI:hdr?GL_R11F_G11F_B10F:integer?GL_RGBA32UI:half?GL_R16F:array_half?GL_RGBA16F:GL_RGBA32F;
 glBindTexture(target,tex);
 if(volume||array)glTexImage3D(target,0,format,width,width,layers,0,integer?GL_RGBA_INTEGER:GL_RGBA,integer?GL_UNSIGNED_INT:GL_FLOAT,atomic?actual:(fetch||getenv("M4_TEST_UPLOAD_ONLY"))?reference:NULL);
 else glTexImage2D(target,0,format,8,32,0,integer?GL_RGBA_INTEGER:GL_RGBA,integer?GL_UNSIGNED_INT:GL_FLOAT,atomic?actual:NULL);
 glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,integer?(void*)expected_uint:fetch?NULL:(void*)reference,GL_DYNAMIC_COPY);
 glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
 char src[2048];snprintf(src,sizeof(src),"#version 330 core\n#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shader_storage_buffer_object: require\n#extension GL_ARB_shader_image_load_store: require\n#extension GL_ARB_shading_language_420pack: require\nlayout(local_size_x=4,local_size_y=4,local_size_z=1) in;\nlayout(std430,binding=0) buffer Data{%svec4 values[];};\n%s\nvoid main(){uvec3 p=gl_GlobalInvocationID;uint i=(p.z*%uu+p.y )*%uu+p.x;%s;}\n",integer?"u":"",atomic?(array?"layout(r32ui,binding=0) uniform uimage2DArray dst;":"layout(r32ui,binding=0) uniform uimage2D dst;"):fetch?"uniform sampler3D src;":volume?(hdr?"layout(r11f_g11f_b10f,binding=0) uniform writeonly image3D dst;":"layout(rgba32f,binding=0) uniform writeonly image3D dst;"):(array&&!layer_views)?(oob?"layout(rgba32f,binding=0) uniform writeonly image2DArray dst; uniform uint layer_offset;":(array_half?"layout(rgba16f,binding=0) uniform writeonly image2DArray dst;":"layout(rgba32f,binding=0) uniform writeonly image2DArray dst;")):hdr?"layout(r11f_g11f_b10f,binding=0) uniform writeonly image2D dst;":half?"layout(r16f,binding=0) uniform writeonly image2D dst;":integer?"layout(rgba32ui,binding=0) uniform writeonly uimage2D dst;":"layout(rgba32f,binding=0) uniform writeonly image2D dst;",volume||array?width:32,width,atomic?(array?"imageAtomicAdd(dst,ivec3(p),values[i].x);imageAtomicAdd(dst,ivec3(p),1u)":"imageAtomicAdd(dst,ivec2(p.xy),values[i].x);imageAtomicAdd(dst,ivec2(p.xy),1u)"):fetch?"values[i]=texelFetch(src,ivec3(p),0)":(array&&!layer_views)?(oob?"imageStore(dst,ivec3(p.xy,p.z+layer_offset),values[i]+vec4(layer_offset==0u?0.:4096.))":coord_colors?"imageStore(dst,ivec3(p),vec4(p,1))":"imageStore(dst,ivec3(p),values[i])"):volume?"imageStore(dst,ivec3(p),values[i])":quad?"uint j=p.y*16u+p.x*2u;ivec2 q=ivec2(p.xy)*2;imageStore(dst,q,values[j]);imageStore(dst,q+ivec2(1,0),values[j+1u]);imageStore(dst,q+ivec2(0,1),values[j+8u]);imageStore(dst,q+ivec2(1,1),values[j+9u])":"imageStore(dst,ivec2(p.xy),values[i])");
 GLuint program=glCreateProgram();glAttachShader(program,compile(GL_COMPUTE_SHADER,src));glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
 if(fetch)glUniform1i(glGetUniformLocation(program,"src"),0);
 else glBindImageTexture(0,tex,0,volume||array,0,atomic?GL_READ_WRITE:GL_WRITE_ONLY,format);
 CHECK(glGetError()==GL_NO_ERROR);
 if(layer_views){
  for(unsigned layer=0;layer<4;layer++){
   glBindImageTexture(0,tex,0,GL_FALSE,layer,GL_WRITE_ONLY,format);
   glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,buf,layer*bytes/4,bytes/4);
   glDispatchCompute(width/4,width/4,1);
  }
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
  printf("Write path: separate 2D layer views\n");
 }else if(quad)glDispatchCompute(1,4,1);
 else if(!getenv("M4_TEST_UPLOAD_ONLY"))glDispatchCompute(width/4,volume||array?width/4:8,volume||array?layers:1);
 if(oob){
  const unsigned offsets[]={4,65536,0xfffffffc};
  for(unsigned k=0;k<3;k++){
   glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
   glUniform1ui(glGetUniformLocation(program,"layer_offset"),offsets[k]);
   glDispatchCompute(width/4,width/4,4);
  }
 }
 glMemoryBarrier(fetch?GL_BUFFER_UPDATE_BARRIER_BIT:GL_TEXTURE_UPDATE_BARRIER_BIT);
 if(array && getenv("M4_TEST_SAMPLE_READBACK")) {
  const char *read_src="#version 330 core\n#extension GL_ARB_compute_shader: require\n#extension GL_ARB_shader_storage_buffer_object: require\n#extension GL_ARB_shading_language_420pack: require\nlayout(local_size_x=4,local_size_y=4,local_size_z=1) in; layout(std430,binding=0) buffer Data{vec4 values[];}; uniform sampler2DArray src; void main(){uvec3 p=gl_GlobalInvocationID; values[(p.z*8u+p.y)*8u+p.x]=texelFetch(src,ivec3(p),0);}";
  GLuint read=glCreateProgram();glAttachShader(read,compile(GL_COMPUTE_SHADER,read_src));glLinkProgram(read);glGetProgramiv(read,GL_LINK_STATUS,&ok);CHECK(ok);
  glUseProgram(read);glUniform1i(glGetUniformLocation(read,"src"),0);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
  glDispatchCompute(2,2,4);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,bytes,actual);glDeleteProgram(read);
  printf("Readback path: texture fetch to SSBO\n");
 }
 else if(fetch)glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,bytes,actual);
 else if(hdr)glGetTexImage(target,0,GL_RGB,GL_UNSIGNED_INT_10F_11F_11F_REV,actual);
 else glGetTexImage(target,0,integer?GL_RGBA_INTEGER:GL_RGBA,integer?GL_UNSIGNED_INT:GL_FLOAT,actual);
 CHECK(glGetError()==GL_NO_ERROR);
 if(atomic)for(unsigned i=0;i<count;i++){expected_uint[4*i]++;expected_uint[4*i+1]=0;expected_uint[4*i+2]=0;expected_uint[4*i+3]=1;}
 /* Read HDR bits directly: RGBA float readback inserts a format-converting
  * 3D Vulkan blit, which is a separate operation from this storage probe.
  * Inputs are exact nonnegative quarter-integers; no rounding is required. */
 if(hdr)for(unsigned i=0;i<count;i++){
  unsigned packed=0;
  for(unsigned c=0;c<3;c++){
   unsigned bits;memcpy(&bits,&reference[4*i+c],4);
   unsigned mant=c==2?5:6;
   unsigned value=bits?((((bits>>23)&255)-127+15)<<mant)|((bits&0x7fffff)>>(23-mant)):0;
   packed|=value<<(c==0?0:c==1?11:22);
  }
  expected_uint[i]=packed;
 }
 if(half)for(unsigned i=0;i<count;i++){reference[4*i+1]=0;reference[4*i+2]=0;reference[4*i+3]=1;}
 if(getenv("M4_PROBE_OUTPUT") && !getenv("M4_TEST_PACK_READBACK")) {
  const void *expected=(integer||hdr)?(void*)expected_uint:(void*)reference;
  const unsigned dump_bytes=hdr?count*4:bytes;
  const char *names[]={"actual.bin","expected.bin"}; const void *data[]={actual,expected};
  for(unsigned k=0;k<2;k++) {
   char path[1024];CHECK(snprintf(path,sizeof(path),"%s/%s",getenv("M4_PROBE_OUTPUT"),names[k])<(int)sizeof(path));
   FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data[k],dump_bytes,1,f)==1);CHECK(fclose(f)==0);
  }
 }
 unsigned bad=0;
 if(getenv("M4_TEST_PACK_READBACK")) {
  CHECK(array && !integer && layers==1 && getenv("M4_PROBE_OUTPUT"));
  char path[1024];snprintf(path,sizeof(path),"%s/pack-payload.bin",getenv("M4_PROBE_OUTPUT"));
  FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(actual,bytes,1,f)==1);CHECK(fclose(f)==0);
  for(unsigned i=0;i<count*4;i++)if(i%4>=2 && memcmp(&actual[i],&reference[i],4))bad++;
  printf("%s: PACK payload captured; control mismatches=%u\n",bad?"FAIL":"DIAGNOSTIC",bad);
 } else {
 const void *expected=(integer||hdr)?(void*)expected_uint:(void*)reference;
 for(unsigned i=0;i<count*(hdr?1:4);i++)if(memcmp((char*)expected+4*i,(char*)actual+4*i,4)){if(bad<4)fprintf(stderr,"word %u: actual=%g expected=%g\n",i,actual[i],reference[i]);bad++;}
 printf("%s: %s exact %u texels (%u mismatched words)\n",bad?"FAIL":"PASS",argv[1],count,bad);
 }
 glDeleteProgram(program);glDeleteTextures(1,&tex);glDeleteBuffers(1,&buf);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);return !!bad;
}
