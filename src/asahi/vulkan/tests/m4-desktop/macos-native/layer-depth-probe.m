// SPDX-License-Identifier: MIT
// Native layered depth witness; retain 1/4-layer commands and exact readback.
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
        unsigned layers=4, width=128, repeats=1;
        bool gate=false, capture=false;
        for (int i=1; i<argc; ++i) {
            if (!strcmp(argv[i],"--gate-submit")) { gate=true; continue; }
            if (!strcmp(argv[i],"--capture-render")) { capture=true; continue; }
            CHECK(i+1<argc);
            const char *key=argv[i++], *value=argv[i];
            if (!strcmp(key,"--source")) sourcePath=@(value);
            else if (!strcmp(key,"--output")) outputPath=@(value);
            else if (!strcmp(key,"--pause-dir")) pausePath=@(value);
            else if (!strcmp(key,"--workload")) CHECK(!strcmp(value,"render-draw"));
            else {
                char *end; unsigned long n=strtoul(value,&end,0);
                CHECK(*value && !*end && n<=UINT32_MAX);
                if (!strcmp(key,"--layers")) layers=(unsigned)n;
                else if (!strcmp(key,"--width")) width=(unsigned)n;
                else if (!strcmp(key,"--repeat")) repeats=(unsigned)n;
                else CHECK(!"unknown option");
            }
        }
        CHECK(sourcePath && outputPath && (!gate || pausePath));
        CHECK(!capture || (gate && pausePath));
        CHECK((layers==1 || layers==4) && repeats>=1 && repeats<=256);
        CHECK(width==128 || width==1024);
        NSError *error=nil;
        NSFileManager *files=[NSFileManager defaultManager];
        CHECK(![files fileExistsAtPath:outputPath]);
        CHECK([files createDirectoryAtPath:outputPath withIntermediateDirectories:YES attributes:nil error:&error]);
        if (pausePath) CHECK([files createDirectoryAtPath:pausePath withIntermediateDirectories:YES attributes:nil error:&error]);
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        CHECK(device && [device.name isEqualToString:@"Apple M4"]);
        NSString *source=[NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&error];
        CHECK(source);
        id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
        if (!library) fprintf(stderr,"%s\n",error.description.UTF8String);
        CHECK(library);
        MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];
        pd.vertexFunction=[library newFunctionWithName:@"vs"];
        pd.fragmentFunction=[library newFunctionWithName:@"fs"];
        pd.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
        id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!pipeline) fprintf(stderr,"%s\n",error.description.UTF8String);
        CHECK(pipeline);
        id<MTLCommandQueue> queue=[device newCommandQueue]; CHECK(queue);
        MTLDepthStencilDescriptor *ds=[MTLDepthStencilDescriptor new];
        ds.depthCompareFunction=MTLCompareFunctionAlways; ds.depthWriteEnabled=YES;
        id<MTLDepthStencilState> depthState=[device newDepthStencilStateWithDescriptor:ds];CHECK(depthState);
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:width height:width mipmapped:NO];
        td.textureType=MTLTextureType2DArray;td.arrayLength=layers;
        td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        id<MTLTexture> target=[device newTextureWithDescriptor:td]; CHECK(target);
        id<MTLSharedEvent> event=gate ? [device newSharedEvent] : nil;
        CHECK(!gate || event);
        size_t sliceBytes=(size_t)width*width*4, bytes=sliceBytes*layers;
        // Collector contract: the exact-size shared output is the last GPU allocation.
        id<MTLBuffer> output=[device newBufferWithLength:bytes options:MTLResourceStorageModeShared]; CHECK(output);
        memset(output.contents,0xa5,bytes);
        printf("pid=%d device=%s registry_id=0x%016" PRIx64 " layers=%u width=%u repeats=%u output_length=%zu\n",getpid(),device.name.UTF8String,device.registryID,layers,width,repeats,bytes);fflush(stdout);
        stage(pausePath,@"created",@"encode");
        id<MTLCommandBuffer> command=[queue commandBuffer]; CHECK(command);
        if (gate) [command encodeWaitForEvent:event value:1];
        MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.renderTargetArrayLength=layers;
        pass.depthAttachment.texture=target;
        pass.depthAttachment.loadAction=MTLLoadActionClear;
        pass.depthAttachment.clearDepth=1.;
        pass.depthAttachment.storeAction=MTLStoreActionStore;
        for(unsigned frame=0;frame<repeats;frame++) {
            id<MTLRenderCommandEncoder> enc=[command renderCommandEncoderWithDescriptor:pass];CHECK(enc);
            enc.label=[NSString stringWithFormat:@"M4 depth layers%u frame%u",layers,frame];
            [enc setRenderPipelineState:pipeline];[enc setDepthStencilState:depthState];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3 instanceCount:layers];
            [enc endEncoding];
        }
        id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];CHECK(blit);
        for(unsigned layer=0;layer<layers;layer++)
            [blit copyFromTexture:target sourceSlice:layer sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,width,1) toBuffer:output destinationOffset:layer*sliceBytes destinationBytesPerRow:width*4 destinationBytesPerImage:sliceBytes];
        [blit endEncoding];
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
        unsigned total=0;
        for(unsigned read=0;read<2;read++) {
            const volatile float *pixels=output.contents;unsigned bad=0;
            for(unsigned layer=0;layer<layers;layer++) for(unsigned i=0;i<width*width;i++) {
                float value=pixels[layer*width*width+i];
                if(value!=layer*.25f) {
                    if(bad<4) fprintf(stderr,"layer%u pixel%u=%g expected%g\n",layer,i,value,layer*.25f);
                    bad++;
                }
            }
            printf("read=%u bad=%u/%u\n",read,bad,layers*width*width);total+=bad;
            NSString *path=[outputPath stringByAppendingPathComponent:read?@"second.depth32f":@"first.depth32f"];
            FILE *f=fopen(path.fileSystemRepresentation,"wbx");CHECK(f&&fwrite(output.contents,1,bytes,f)==bytes&&fclose(f)==0);
            if(!read)usleep(100000);
        }
        printf("submit=%.9f completion=%.9f api_gpu_start_s=%.9f api_gpu_end_s=%.9f\n",submit,complete,command.GPUStartTime,command.GPUEndTime);
        printf("%s: native layered depth layers=%u incorrect=%u\n",total?"FAIL":"PASS",layers,total); fflush(stdout);
        stage(pausePath,@"complete",@"exit");
        return total ? 1 : 0;
    }
}
