// SPDX-License-Identifier: MIT
// Native counterpart of eevee-image-probe.c array mode. Exact layered texels.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "native-common.h"
enum { BYTES=4194304 };
int main(int argc, const char **argv) {
 @autoreleasepool {
  NSString *sourcePath=nil,*outputPath=nil,*pausePath=nil;
  bool gate=false; unsigned width=8;
  for(int i=1;i<argc;i++) {
   if(!strcmp(argv[i],"--gate-submit")){gate=true;continue;}
   CHECK(i+1<argc);
   if(!strcmp(argv[i],"--source"))sourcePath=@(argv[++i]);
   else if(!strcmp(argv[i],"--output"))outputPath=@(argv[++i]);
   else if(!strcmp(argv[i],"--pause-dir"))pausePath=@(argv[++i]);
   else if(!strcmp(argv[i],"--width"))width=(unsigned)strtoul(argv[++i],NULL,10);
   else if(!strcmp(argv[i],"--workload"))CHECK(!strcmp(argv[++i],"compute-direct"));
   else CHECK(!"unknown argument");
  }
  CHECK(sourcePath&&outputPath&&(!gate||pausePath));
  CHECK(width==8||width==32);const unsigned imageBytes=width*width*4*16;
  NSError *error=nil;NSFileManager *files=[NSFileManager defaultManager];
  CHECK(![files fileExistsAtPath:outputPath]);
  CHECK([files createDirectoryAtPath:outputPath withIntermediateDirectories:YES attributes:nil error:&error]);
  if(pausePath)CHECK([files createDirectoryAtPath:pausePath withIntermediateDirectories:YES attributes:nil error:&error]);
  NSString *source=[NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&error];CHECK(source);
  id<MTLDevice> device=MTLCreateSystemDefaultDevice();CHECK(device&&[device.name isEqualToString:@"Apple M4"]);
  id<MTLLibrary> lib=[device newLibraryWithSource:source options:nil error:&error];
  if(!lib)fprintf(stderr,"%s\n",error.description.UTF8String);CHECK(lib);
  id<MTLFunction> function=[lib newFunctionWithName:@"cs"];
  id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:function error:&error];CHECK(pipeline);
  id<MTLArgumentEncoder> args=[function newArgumentEncoderWithBufferIndex:0];CHECK(args);
  id<MTLBuffer> argument=[device newBufferWithLength:args.encodedLength options:MTLResourceStorageModeShared];CHECK(argument);
  MTLTextureDescriptor *desc=[[MTLTextureDescriptor alloc]init];
  desc.textureType=MTLTextureType2DArray;desc.pixelFormat=MTLPixelFormatRGBA32Float;
  desc.width=width;desc.height=width;desc.depth=1;desc.arrayLength=4;
  desc.mipmapLevelCount=1;desc.storageMode=MTLStorageModePrivate;
  desc.usage=MTLTextureUsageShaderWrite|MTLTextureUsageShaderRead;
  id<MTLTexture> tex=[device newTextureWithDescriptor:desc];CHECK(tex);
  tex.label=@"M4 Eevee native RGBA32Float array";
  id<MTLBuffer> input=[device newBufferWithLength:imageBytes options:MTLResourceStorageModeShared];CHECK(input);
  for(unsigned i=0;i<imageBytes/4;i++)((float*)input.contents)[i]=(float)i*.25f-16.f;
  [args setArgumentBuffer:argument offset:0];[args setTexture:tex atIndex:0];
  id<MTLCommandQueue> queue=[device newCommandQueue];CHECK(queue);
  id<MTLSharedEvent> event=gate?[device newSharedEvent]:nil;CHECK(!gate||event);
  // Keep the existing collector's last-allocation, 4 MiB output contract.
  id<MTLBuffer> output=[device newBufferWithLength:BYTES options:MTLResourceStorageModeShared];CHECK(output&&output.contents);
  memset(output.contents,0xa5,BYTES);output.label=@"M4 native array readback output";
  printf("pid=%d device=%s registry_id=0x%016" PRIx64 " output_length=%u image_bytes=%u argument_bytes=%lu texture_resource=%" PRIu64 "\n",getpid(),device.name.UTF8String,device.registryID,BYTES,imageBytes,(unsigned long)args.encodedLength,tex.gpuResourceID._impl);fflush(stdout);
  stage(pausePath,@"created",@"encode");
  id<MTLCommandBuffer> command=[queue commandBuffer];CHECK(command);
  if(gate)[command encodeWaitForEvent:event value:1];
  id<MTLComputeCommandEncoder> compute=[command computeCommandEncoder];CHECK(compute);
  [compute setComputePipelineState:pipeline];[compute setBuffer:argument offset:0 atIndex:0];
  [compute setBuffer:input offset:0 atIndex:1];[compute useResource:tex usage:MTLResourceUsageWrite];
  [compute dispatchThreads:MTLSizeMake(width,width,4) threadsPerThreadgroup:MTLSizeMake(4,4,1)];[compute endEncoding];
  id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];CHECK(blit);
  for(unsigned layer=0;layer<4;layer++)[blit copyFromTexture:tex sourceSlice:layer sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,width,1) toBuffer:output destinationOffset:layer*width*width*16 destinationBytesPerRow:width*16 destinationBytesPerImage:width*width*16];
  [blit endEncoding];stage(pausePath,@"encoded",@"submit");[command commit];
  stage(pausePath,@"submitted",@"execute");if(gate)event.signaledValue=1;
  [command waitUntilCompleted];CHECK(command.status==MTLCommandBufferStatusCompleted&&!command.error);
  unsigned bad=0;for(unsigned i=0;i<imageBytes/4;i++)if(memcmp((char*)input.contents+4*i,(char*)output.contents+4*i,4))bad++;
  for(unsigned i=imageBytes;i<BYTES;i++)if(((unsigned char*)output.contents)[i]!=0xa5)bad++;
  NSString *path=[outputPath stringByAppendingPathComponent:@"readback.bin"];
  CHECK([[NSData dataWithBytes:output.contents length:BYTES]writeToFile:path atomically:YES]);
  printf("%s: native array exact texels and untouched padding (%u mismatches) GPU seconds=%.9f\n",bad?"FAIL":"PASS",bad,command.GPUEndTime-command.GPUStartTime);fflush(stdout);
  stage(pausePath,@"complete",@"exit");return !!bad;
 }
}
