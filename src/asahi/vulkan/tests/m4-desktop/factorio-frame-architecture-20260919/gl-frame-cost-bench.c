/* SPDX-License-Identifier: MIT
 * Basic solid/fetch/sample/compute-store scaling with CPU-checked output; use the hardware guard.
 * cc -O2 gl-frame-cost-bench.c -lEGL -lGL -lm -o gl-frame-cost-bench
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); exit(1); } } while (0)
static GLuint shader(GLenum type, const char *text) {
   GLuint s = glCreateShader(type); glShaderSource(s, 1, &text, NULL);
   glCompileShader(s); GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log); fputs(log, stderr); }
   CHECK(ok); return s;
}
static double now(void) {
   struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv) {
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
   CHECK(eglInitialize(d, NULL, NULL) && eglBindAPI(EGL_OPENGL_API));
   EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   printf("Renderer: %s\n", glGetString(GL_RENDERER));
   GLuint p = glCreateProgram();
   GLuint vs = shader(GL_VERTEX_SHADER, "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}");
   unsigned mode = argc > 1 ? atoi(argv[1]) : 0;
   unsigned discard = argc > 2 ? atoi(argv[2]) : 0;
   unsigned wide = argc > 3 ? atoi(argv[3]) : 0;
   unsigned words = argc > 4 ? atoi(argv[4]) : 1;
   unsigned local = argc > 5 ? atoi(argv[5]) : 256;
   unsigned arithmetic = argc > 6 ? atoi(argv[6]) : 0;
   unsigned read_each = argc > 7 ? atoi(argv[7]) : 1;
   CHECK(read_each <= 1);
   CHECK(local == 32 || local == 64 || local == 128 || local == 256 || local == 512);
   CHECK(arithmetic <= 256 && (!arithmetic || mode == 0 || (mode == 3 && words == 4)));
   CHECK(words == 1 || words == 4 || words == 16);
   CHECK(mode <= 3);
   CHECK(discard <= 1);
   CHECK(wide <= 2);
   GLenum format = wide == 2 ? GL_RGBA32F : wide == 1 ? GL_RGBA16F : GL_RGBA8;
   const char *fs[] = {
      "#version 330 core\nout vec4 color;void main(){color=vec4(1,0,1,1);}",
      "#version 330 core\nuniform sampler2D source;out vec4 color;void main(){color=texelFetch(source,ivec2(gl_FragCoord.xy),0);}",
      "#version 330 core\nuniform sampler2D source;out vec4 color;void main(){color=texture(source,gl_FragCoord.xy/vec2(textureSize(source,0)));}"
   };
   if (mode == 3) {
      CHECK(wide == 0 && discard == 0);
      char compute[32768];
      if (arithmetic) {
         int len = snprintf(compute, sizeof(compute),
            "#version 430 core\nlayout(local_size_x=%u)in;layout(std430,binding=0)buffer B{vec4 data[];};"
            "uniform uint count;void main(){uint i=gl_GlobalInvocationID.x;if(i>=count/4u)return;"
            "vec4 a=vec4(float(i&255u)/256.0);", local);
         for (unsigned i=0;i<arithmetic;i++)
            len += snprintf(compute+len, sizeof(compute)-len,
               "a=fma(a,vec4(0.999,0.997,0.995,0.993),vec4(0.001));");
         snprintf(compute+len, sizeof(compute)-len, "data[i]=a;}");
      } else if (words == 1)
         snprintf(compute, sizeof(compute),
            "#version 430 core\nlayout(local_size_x=%u)in;layout(std430,binding=0)buffer B{uint data[];};"
            "uniform uint count;void main(){uint i=gl_GlobalInvocationID.x;if(i<count)data[i]=i^0xa55aa55au;}",local);
      else
         snprintf(compute, sizeof(compute),
            "#version 430 core\nlayout(local_size_x=%u)in;layout(std430,binding=0)buffer B{uvec4 data[];};"
            "uniform uint count;void main(){uint i=gl_GlobalInvocationID.x*%uu;"
            "for(uint j=0u;j<%uu;j++){uint v=i+j;if(v<count/4u)data[v]=(uvec4(v*4u)+uvec4(0,1,2,3))^uvec4(0xa55aa55au);}}",
            local,words/4,words/4);
      glAttachShader(p, shader(GL_COMPUTE_SHADER, compute));
   } else {
      glAttachShader(p, vs);
      char fragment[32768];
      if (arithmetic) {
         int len = snprintf(fragment, sizeof(fragment),
            "#version 430 core\nout vec4 color;void main(){"
            "vec4 a=vec4(float(uint(gl_FragCoord.x)&255u)/256.0);");
         for (unsigned i=0;i<arithmetic;i++)
            len += snprintf(fragment+len, sizeof(fragment)-len,
               "a=fma(a,vec4(0.999,0.997,0.995,0.993),vec4(0.001));");
         snprintf(fragment+len, sizeof(fragment)-len, "color=a;}");
      }
      glAttachShader(p, shader(GL_FRAGMENT_SHADER, arithmetic ? fragment : fs[mode]));
   }
   glLinkProgram(p); GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); CHECK(ok); glUseProgram(p);
   GLuint vao, tex, fb, source, source_fb;
   glGenVertexArrays(1,&vao);glBindVertexArray(vao);
   glGenFramebuffers(1,&fb);glGenFramebuffers(1,&source_fb);
   glGenTextures(1,&tex);glGenTextures(1,&source);
   const unsigned sizes[][2]={{512,512},{1920,1080},{2560,1664},{3840,2160}};
   for(unsigned k=0;k<4;k++) {
      unsigned w=sizes[k][0],h=sizes[k][1];
      if (mode == 3) {
         GLuint buffer; glGenBuffers(1, &buffer);
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
         size_t bytes = (size_t)w * h * 4;
         glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, NULL, GL_DYNAMIC_READ);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
         glUniform1ui(glGetUniformLocation(p, "count"), w * h);
         for (unsigned round = 0; round < 6; round++) {
            GLuint queries[2]; glGenQueries(2, queries);
            double start = now();
            glQueryCounter(queries[0], GL_TIMESTAMP);
            /* Separate submission keeps the opening timestamp ahead of the
             * dispatch even on implementations that batch timestamps. The
             * interval includes this queue boundary; it is not shader-only. */
            glFlush();
            glDispatchCompute((w*h/words + local-1)/local, 1, 1);
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glQueryCounter(queries[1], GL_TIMESTAMP);
            glFinish();
            double elapsed = (now() - start)*1000;
            GLuint64 ticks[2];
            glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &ticks[0]);
            glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &ticks[1]);
            CHECK(ticks[1] > ticks[0]);
            glDeleteQueries(2, queries);
            if (read_each || round == 5) {
            const unsigned *data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
            CHECK(data);
            if (arithmetic) {
               float expected[256][4];
               const float coefficients[4]={0.999f,0.997f,0.995f,0.993f};
               for(unsigned i=0;i<256;i++)for(unsigned c=0;c<4;c++) {
                  float value=(float)i/256.0f;
                  for(unsigned j=0;j<arithmetic;j++)value=fmaf(value,coefficients[c],0.001f);
                  expected[i][c]=value;
               }
               const float *values=(const float *)data;
               for(unsigned i=0;i<w*h;i++)CHECK(fabsf(values[i]-expected[(i/4)&255][i&3])<0.00001f);
            } else
               for (unsigned i = 0; i < w*h; i++) CHECK(data[i] == (i ^ 0xa55aa55au));
            CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
            }
            CHECK(glGetError()==GL_NO_ERROR);
            printf("FRAME_COST mode=%u words=%u local=%u arithmetic=%u read_each=%u width=%u height=%u round=%u wall_ms=%.6f gpu_query_ms=%.6f begin_ns=%llu end_ns=%llu\n", mode,words,local,arithmetic,read_each,w,h,round,elapsed,(ticks[1]-ticks[0])/1e6,(unsigned long long)ticks[0],(unsigned long long)ticks[1]);
            fflush(stdout);
         }
         glDeleteBuffers(1, &buffer);
         continue;
      }
      glBindTexture(GL_TEXTURE_2D,source);
      glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER,source_fb);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
      CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
      glViewport(0,0,w,h);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
      glBindTexture(GL_TEXTURE_2D,tex);
      glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
      glBindFramebuffer(GL_FRAMEBUFFER,fb);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
      CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
      glBindTexture(GL_TEXTURE_2D,source);
      for(unsigned round=0;round<6;round++) {
         glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
         if (discard) {
            const GLenum attachment = GL_COLOR_ATTACHMENT0;
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
         }
         double start=now();
         /* No query instrumentation that could alter the rendering workload.
          * Kernel pass timestamps supply GPU timings; this is CPU wall time. */
         glDrawArrays(GL_TRIANGLES,0,3);glFinish();
         double elapsed=(now()-start)*1000;
         unsigned char pixel[4];glReadPixels(w-1,h-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
         CHECK(glGetError()==GL_NO_ERROR);
         if (arithmetic) {
            const float coefficients[4]={0.999f,0.997f,0.995f,0.993f};
            for (unsigned c=0;c<4;c++) {
               float expected=(float)((w-1)&255)/256.0f;
               for(unsigned i=0;i<arithmetic;i++)expected=fmaf(expected,coefficients[c],0.001f);
               CHECK(abs((int)pixel[c]-(int)lroundf(fminf(fmaxf(expected,0),1)*255))<=1);
            }
         } else CHECK(pixel[0]==255&&pixel[1]==0&&pixel[2]==255&&pixel[3]==255);
         printf("FRAME_COST mode=%u discard=%u wide=%u arithmetic=%u width=%u height=%u round=%u wall_ms=%.6f\n",mode,discard,wide,arithmetic,w,h,round,elapsed);
         fflush(stdout);
      }
   }
   puts("PASS: basic frame cost and checked readback");
   eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
   eglDestroyContext(d,c);eglTerminate(d);
   return 0;
}
