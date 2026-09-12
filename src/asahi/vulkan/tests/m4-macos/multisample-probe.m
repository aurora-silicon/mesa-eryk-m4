// SPDX-License-Identifier: MIT
// Native counterpart of the exact-output Vulkan multisample fragment probe.
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

int main(int argc, const char **argv)
{
    @autoreleasepool {
        NSString *sourcePath=nil, *outputPath=nil, *pausePath=nil;
        unsigned samples=2, width=128, mask=1, repeats=1;
        bool gate=false, capture=false, computeResolve=false;
        for (int i=1; i<argc; ++i) {
            if (!strcmp(argv[i],"--gate-submit")) { gate=true; continue; }
            if (!strcmp(argv[i],"--capture-render")) { capture=true; continue; }
            if (!strcmp(argv[i],"--resolve-compute")) { computeResolve=true; continue; }
            CHECK(i+1<argc);
            const char *key=argv[i++], *value=argv[i];
            if (!strcmp(key,"--source")) sourcePath=@(value);
            else if (!strcmp(key,"--output")) outputPath=@(value);
            else if (!strcmp(key,"--pause-dir")) pausePath=@(value);
            else if (!strcmp(key,"--workload")) CHECK(!strcmp(value,"render-draw"));
            else {
                char *end; unsigned long n=strtoul(value,&end,0);
                CHECK(*value && !*end && n<=UINT32_MAX);
                if (!strcmp(key,"--samples")) samples=(unsigned)n;
                else if (!strcmp(key,"--mask")) mask=(unsigned)n;
                else if (!strcmp(key,"--width")) width=(unsigned)n;
                else if (!strcmp(key,"--repeat")) repeats=(unsigned)n;
                else CHECK(!"unknown option");
            }
        }
        CHECK(sourcePath && outputPath && (!gate || pausePath));
        CHECK(!capture || (gate && pausePath));
        CHECK(samples==1 || samples==2 || samples==4);
        CHECK(mask < (1u<<samples) && repeats>=1 && repeats<=256);
        CHECK(width==32 || width==128 || width==1024);
        NSError *error=nil;
        NSFileManager *files=[NSFileManager defaultManager];
        CHECK(![files fileExistsAtPath:outputPath]);
        CHECK([files createDirectoryAtPath:outputPath withIntermediateDirectories:YES attributes:nil error:&error]);
        if (pausePath) CHECK([files createDirectoryAtPath:pausePath withIntermediateDirectories:YES attributes:nil error:&error]);
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        CHECK(device && [device.name isEqualToString:@"Apple M4"]);
        CHECK([device supportsTextureSampleCount:samples]);
        NSString *source=[NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&error];
        CHECK(source);
        id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
        if (!library) fprintf(stderr,"%s\n",error.description.UTF8String);
        CHECK(library);
        MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];
        pd.vertexFunction=[library newFunctionWithName:@"vs"];
        pd.fragmentFunction=[library newFunctionWithName:@"fs"];
        pd.rasterSampleCount=samples;
        pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!pipeline) fprintf(stderr,"%s\n",error.description.UTF8String);
        CHECK(pipeline);
        id<MTLCommandQueue> queue=[device newCommandQueue]; CHECK(queue);
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width height:width mipmapped:NO];
        td.storageMode=MTLStorageModePrivate; td.usage=MTLTextureUsageRenderTarget | (computeResolve ? MTLTextureUsageShaderRead : 0);
        id<MTLTexture> resolved=[device newTextureWithDescriptor:td]; CHECK(resolved);
        id<MTLTexture> target=resolved;
        if (samples>1) {
            td.textureType=MTLTextureType2DMultisample; td.sampleCount=samples;
            target=[device newTextureWithDescriptor:td]; CHECK(target);
        }
        struct { uint32_t mask, width, samples; } params={mask,width,samples};
        id<MTLBuffer> uniforms=[device newBufferWithBytes:&params length:sizeof(params) options:MTLResourceStorageModeShared];
        id<MTLComputePipelineState> resolver=computeResolve ? [device newComputePipelineStateWithFunction:[library newFunctionWithName:samples>1?@"resolve_msaa":@"resolve_single"] error:&error] : nil;
        CHECK(!computeResolve || resolver);
        id<MTLSharedEvent> event=gate ? [device newSharedEvent] : nil;
        CHECK(uniforms && (!gate || event));
        size_t bytes=(size_t)width*width*4;
        // Collector contract: the exact-size shared output is the last GPU allocation.
        id<MTLBuffer> output=[device newBufferWithLength:bytes options:MTLResourceStorageModeShared]; CHECK(output);
        memset(output.contents,0xa5,bytes);
        printf("pid=%d device=%s registry_id=0x%016" PRIx64 " samples=%u mask=%#x width=%u repeats=%u output_length=%zu\n",getpid(),device.name.UTF8String,device.registryID,samples,mask,width,repeats,bytes); fflush(stdout);
        printf("resolve=%s\n",computeResolve?"separate-compute":"native-render-resolve"); fflush(stdout);
        stage(pausePath,@"created",@"encode");
        id<MTLCommandBuffer> command=[queue commandBuffer]; CHECK(command);
        if (gate) [command encodeWaitForEvent:event value:1];
        MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=target;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
        pass.colorAttachments[0].storeAction=samples>1 && !computeResolve ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionStore;
        if (samples>1 && !computeResolve) pass.colorAttachments[0].resolveTexture=resolved;
        for (unsigned frame=0; frame<repeats; ++frame) {
            id<MTLRenderCommandEncoder> enc=[command renderCommandEncoderWithDescriptor:pass]; CHECK(enc);
            enc.label=[NSString stringWithFormat:@"M4 samples%u mask%x frame%u",samples,mask,frame];
            [enc setRenderPipelineState:pipeline];
            [enc setFragmentBuffer:uniforms offset:0 atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
        }
        if (computeResolve) {
            id<MTLComputeCommandEncoder> enc=[command computeCommandEncoder]; CHECK(enc);
            [enc setComputePipelineState:resolver]; [enc setTexture:target atIndex:0];
            [enc setBuffer:output offset:0 atIndex:0]; [enc setBuffer:uniforms offset:0 atIndex:1];
            [enc dispatchThreads:MTLSizeMake(width,width,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
            [enc endEncoding];
        } else {
        id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder]; CHECK(blit);
        [blit copyFromTexture:resolved sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,width,1) toBuffer:output destinationOffset:0 destinationBytesPerRow:width*4 destinationBytesPerImage:bytes];
        [blit endEncoding];
        }
        stage(pausePath,@"encoded",@"submit");
        double submit=now(); [command commit]; stage(pausePath,@"submitted",@"execute");
        if (gate) event.signaledValue=1;
        if (capture) {
            printf("render_capture_begin=%.9f command_status=%lu\n",now(),(unsigned long)command.status); fflush(stdout);
            stage(pausePath,@"executing",@"release-shader");
            printf("render_capture_end=%.9f command_status=%lu\n",now(),(unsigned long)command.status); fflush(stdout);
        }
        [command waitUntilCompleted]; double complete=now();
        CHECK(command.status==MTLCommandBufferStatusCompleted && !command.error);
        unsigned red=(255u*__builtin_popcount(mask)+samples/2)/samples;
        unsigned total=0;
        for (unsigned read=0; read<2; ++read) {
            const volatile unsigned char *pixels=output.contents;
            unsigned bad=0;
            for (size_t i=0; i<bytes; i+=4) {
                if (pixels[i]!=red || pixels[i+1]!=0 || pixels[i+2]!=0 || pixels[i+3]!=255) {
                    if (bad<4) fprintf(stderr,"pixel=%zu got=%u,%u,%u,%u expected=%u,0,0,255\n",i/4,pixels[i],pixels[i+1],pixels[i+2],pixels[i+3],red);
                    ++bad;
                }
            }
            printf("read=%u bad=%u/%u\n",read,bad,width*width); total+=bad;
            NSString *path=[outputPath stringByAppendingPathComponent:read?@"second.rgba":@"first.rgba"];
            FILE *f=fopen(path.fileSystemRepresentation,"wbx"); CHECK(f && fwrite(output.contents,1,bytes,f)==bytes && fclose(f)==0);
            if (!read) usleep(100000);
        }
        printf("submit=%.9f completion=%.9f api_gpu_start_s=%.9f api_gpu_end_s=%.9f\n",submit,complete,command.GPUStartTime,command.GPUEndTime);
        printf("%s: native multisample samples=%u mask=%#x incorrect=%u\n",total?"FAIL":"PASS",samples,mask,total); fflush(stdout);
        stage(pausePath,@"complete",@"exit");
        return total ? 1 : 0;
    }
}
