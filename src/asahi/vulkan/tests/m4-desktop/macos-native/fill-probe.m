// SPDX-License-Identifier: MIT
// Native counterpart of m4_vk_first_submit.c. Never uses a blit fill.
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
enum { BYTES = 4194304, WORDS = BYTES / 4, PATTERN = 0x7b2da916 };
static unsigned snapshot(volatile uint32_t *data, uint32_t *copy,
                         unsigned buckets[16])
{
    unsigned bad = 0;
    memset(buckets, 0, 16 * sizeof(*buckets));
    for (unsigned i = 0; i < WORDS; ++i) {
        uint32_t value = data[i];
        copy[i] = value;
        if (value != PATTERN) {
            if (bad < 8) fprintf(stderr, "word=%u got=%08x expected=%08x\n", i, value, PATTERN);
            ++bad;
            ++buckets[i / (WORDS / 16)];
        }
    }
    return bad;
}
static void save(NSString *directory, unsigned run, const char *name, uint32_t *data)
{
    NSString *path = [directory stringByAppendingPathComponent:
        [NSString stringWithFormat:@"run-%03u-%s.bin", run, name]];
    FILE *f = fopen(path.fileSystemRepresentation, "wbx");
    CHECK(f && fwrite(data, 1, BYTES, f) == BYTES && fclose(f) == 0);
}
int main(int argc, const char **argv)
{
    @autoreleasepool {
        NSString *sourcePath = nil, *outputPath = nil, *pausePath = nil;
        unsigned repeats = 1, dispatches = 1;
        bool gate = false, shaderGateEnabled = false;
        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "--gate-submit")) { gate = true; continue; }
            if (!strcmp(argv[i], "--gate-shader")) { shaderGateEnabled = true; continue; }
            CHECK(i + 1 < argc);
            if (!strcmp(argv[i], "--source")) sourcePath = @(argv[++i]);
            else if (!strcmp(argv[i], "--output")) outputPath = @(argv[++i]);
            else if (!strcmp(argv[i], "--pause-dir")) pausePath = @(argv[++i]);
            else if (!strcmp(argv[i], "--workload")) CHECK(!strcmp(argv[++i], "compute-direct"));
            else if (!strcmp(argv[i], "--dispatches")) {
                char *end; unsigned long n = strtoul(argv[++i], &end, 10);
                CHECK(*argv[i] && !*end && (n == 1 || n == 2));
                dispatches = (unsigned)n;
            }
            else if (!strcmp(argv[i], "--repeat")) {
                char *end; unsigned long n = strtoul(argv[++i], &end, 10);
                CHECK(*argv[i] && !*end && n >= 1 && n <= 100);
                repeats = (unsigned)n;
            } else CHECK(!"unknown option");
        }
        CHECK(sourcePath && outputPath && (!shaderGateEnabled || (gate && pausePath)) && (!gate || pausePath) && (!pausePath || repeats == 1));
        NSError *error = nil;
        NSFileManager *files = [NSFileManager defaultManager];
        CHECK(![files fileExistsAtPath:outputPath]);
        CHECK([files createDirectoryAtPath:outputPath withIntermediateDirectories:YES attributes:nil error:&error]);
        if (pausePath) CHECK([files createDirectoryAtPath:pausePath withIntermediateDirectories:YES attributes:nil error:&error]);
        NSString *source = [NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&error];
        CHECK(source);
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        CHECK(device && [device.name isEqualToString:@"Apple M4"]);
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) fprintf(stderr, "%s\n", error.description.UTF8String);
        CHECK(library);
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:
            [library newFunctionWithName:shaderGateEnabled ? @"cs_gated" : @"cs"] error:&error];
        CHECK(pipeline && pipeline.maxTotalThreadsPerThreadgroup >= 64);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLSharedEvent> event = gate ? [device newSharedEvent] : nil;
        CHECK(queue && (!gate || event));
        id<MTLBuffer> shaderGate = shaderGateEnabled ? [device newBufferWithLength:12 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined] : nil;
        CHECK(!shaderGateEnabled || shaderGate);
        if (shaderGate) memset(shaderGate.contents, 0, 12);
        uint32_t *first = malloc(BYTES), *second = malloc(BYTES);
        CHECK(first && second);
        // Capture contract: output is the last GPU resource before "created".
        id<MTLBuffer> output = [device newBufferWithLength:BYTES options:MTLResourceStorageModeShared];
        CHECK(output && output.length == BYTES && output.contents);
        output.label = @"M4 native 4MiB fill output";
        printf("pid=%d device=%s registry_id=0x%016" PRIx64 " output_length=%u tpg=64 invocations=262144 gated=%d repeats=%u dispatches=%u\n",
               getpid(), device.name.UTF8String, device.registryID, BYTES, gate, repeats, dispatches);
        fflush(stdout);
        unsigned total = 0;
        for (unsigned run = 0; run < repeats; ++run) {
            memset(output.contents, 0xa5, BYTES);
            stage(pausePath, @"created", @"encode");
            id<MTLCommandBuffer> command = [queue commandBuffer];
            CHECK(command);
            if (gate) [command encodeWaitForEvent:event value:1];
            for (unsigned part = 0; part < dispatches; ++part) {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            CHECK(encoder);
            [encoder setComputePipelineState:pipeline];
            [encoder setBuffer:output offset:part * (BYTES / dispatches) atIndex:0];
            if (shaderGate) [encoder setBuffer:shaderGate offset:0 atIndex:1];
            [encoder dispatchThreads:MTLSizeMake(262144 / dispatches, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [encoder endEncoding];
            }
            stage(pausePath, @"encoded", @"submit");
            double submit = now();
            [command commit];
            stage(pausePath, @"submitted", @"execute");
            if (gate) event.signaledValue = 1;
            if (shaderGate) {
                uint32_t *words = shaderGate.contents;
                // The collector establishes execution from the live queue;
                // an in-flight GPU store is not a CPU-visible readiness signal.
                stage(pausePath, @"executing", @"release-shader");
                __atomic_store_n(words, 1, __ATOMIC_RELEASE);
            }
            [command waitUntilCompleted];
            double completed = now();
            CHECK(command.status == MTLCommandBufferStatusCompleted && !command.error);
            if (shaderGate) {
                fprintf(stderr, "gate release=%u entered=%u expired=%u submit=%.9f completion=%.9f gpu_start=%.9f gpu_end=%.9f\n", ((volatile uint32_t *)shaderGate.contents)[0], ((volatile uint32_t *)shaderGate.contents)[1], ((volatile uint32_t *)shaderGate.contents)[2], submit, completed, command.GPUStartTime, command.GPUEndTime);
                // Finite capture dwell may expire: CPU/GPU concurrent flag visibility
                // is not a completion contract. Pixel checks remain mandatory.
            }
            unsigned buckets[16];
            unsigned bad = snapshot(output.contents, first, buckets);
            double read = now();
            struct timespec delay = {.tv_nsec = 100000000};
            CHECK(nanosleep(&delay, NULL) == 0);
            unsigned laterBuckets[16];
            unsigned later = snapshot(output.contents, second, laterBuckets);
            double reread = now();
            printf("run=%u submit=%.9f completion=%.9f first_read=%.9f second_read=%.9f bad=%u/%u second_bad=%u/%u api_gpu_start_s=%.9f api_gpu_end_s=%.9f\n",
                   run, submit, completed, read, reread, bad, WORDS, later, WORDS, command.GPUStartTime, command.GPUEndTime);
            printf("first_bad_by_256KiB=");
            for (unsigned i = 0; i < 16; ++i) printf("%s%u", i ? "," : "", buckets[i]);
            puts(""); fflush(stdout);
            save(outputPath, run, "first", first);
            save(outputPath, run, "second", second);
            total += bad + later;
            stage(pausePath, @"complete", @"exit");
        }
        free(first); free(second);
        printf("%s: native compute fill, incorrect_words=%u\n", total ? "FAIL" : "PASS", total);
        return total ? 1 : 0;
    }
}
