/* SPDX-License-Identifier: MIT
 * Private comparison helper: select the 1080p external SDL display and report
 * actual logical/drawable size. Optional Metal archive capture is diagnostic.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { int x, y, w, h; } STKSDLRect;
extern int SDL_GetNumVideoDisplays(void);
extern int SDL_GetDisplayBounds(int, STKSDLRect *);
extern const char *SDL_GetDisplayName(int);
extern void *SDL_CreateWindow(const char *, int, int, int, int, unsigned int);
extern void SDL_GetWindowSize(void *, int *, int *);
extern void SDL_Vulkan_GetDrawableSize(void *, int *, int *);
static void *create_window(const char *title, int x, int y, int w, int h, unsigned int flags)
{
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        STKSDLRect b;
        if (SDL_GetDisplayBounds(i, &b) == 0 && b.w == 1920 && b.h == 1080) {
            /* Borderless window filling the external display avoids macOS
             * fullscreen Spaces/mode animation changing the render size. */
            x = b.x; y = b.y; w = b.w; h = b.h;
            flags = (flags & ~0x1001u) | 0x10u;
            fprintf(stderr, "STK comparison display: index=%d name=%s bounds=%d,%d,%d,%d\n",
                    i, SDL_GetDisplayName(i), b.x, b.y, b.w, b.h);
            break;
        }
    }
    void *window = SDL_CreateWindow(title, x, y, w, h, flags);
    if (window) {
        int dw = 0, dh = 0;
        SDL_GetWindowSize(window, &w, &h);
        SDL_Vulkan_GetDrawableSize(window, &dw, &dh);
        fprintf(stderr, "STK comparison window: logical=%dx%d drawable=%dx%d flags=%x\n", w,h,dw,dh,flags);
    }
    return window;
}
__attribute__((used)) static struct { const void *replacement, *original; } interpose[]
    __attribute__((section("__DATA,__interpose"))) = {
        { create_window, SDL_CreateWindow } };

static void archive_pipeline(id<MTLDevice> device, MTLRenderPipelineDescriptor *descriptor)
{
    const char *directory = getenv("STK_METAL_ARCHIVES");
    if (!directory) return;
    static unsigned int serial;
    @synchronized(device) {
        @autoreleasepool {
            unsigned int index = serial++;
            if (index >= 512) return; /* Hard bound for diagnostic disk use. */
            NSError *error = nil;
            MTLBinaryArchiveDescriptor *archive_desc = [MTLBinaryArchiveDescriptor new];
            id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:archive_desc error:&error];
            [archive_desc release];
            NSString *path = [NSString stringWithFormat:@"%s/pipeline-%04u.metalar", directory, index];
            BOOL ok = archive && [archive addRenderPipelineFunctionsWithDescriptor:descriptor error:&error]
                && [archive serializeToURL:[NSURL fileURLWithPath:path] error:&error];
            fprintf(stderr, "STK Metal archive: index=%u ok=%d vertex=%s fragment=%s error=%s\n", index, ok,
                    descriptor.vertexFunction.name.UTF8String ?: "none",
                    descriptor.fragmentFunction.name.UTF8String ?: "none", error.description.UTF8String ?: "none");
            [archive release];
        }
    }
}

static IMP original_sync, original_async;
static IMP original_texture;
static id texture_new(id self, SEL sel, MTLTextureDescriptor *desc)
{
    if (desc.width >= 1000 && (desc.usage & MTLTextureUsageRenderTarget))
        fprintf(stderr, "STK Metal image: %lux%lu format=%lu usage=%lu\n",
            (unsigned long)desc.width, (unsigned long)desc.height,
            (unsigned long)desc.pixelFormat, (unsigned long)desc.usage);
    return ((id (*)(id,SEL,id))original_texture)(self,sel,desc);
}
static id pipeline_sync(id self, SEL sel, MTLRenderPipelineDescriptor *desc, NSError **error)
{
    id result = ((id (*)(id,SEL,id,NSError **))original_sync)(self,sel,desc,error);
    if (result) archive_pipeline(self, desc);
    return result;
}
static void pipeline_async(id self, SEL sel, MTLRenderPipelineDescriptor *desc,
                          void (^handler)(id<MTLRenderPipelineState>, NSError *))
{
    ((void (*)(id,SEL,id,void (^)(id<MTLRenderPipelineState>,NSError *)))original_async)(self,sel,desc,
        ^(id<MTLRenderPipelineState> result, NSError *error) {
            if (result) archive_pipeline(self, desc);
            handler(result, error);
        });
}
__attribute__((constructor)) static void install_archive_hooks(void)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        Class cls = object_getClass(device);
        fprintf(stderr, "STK default Metal device class: %s\n", class_getName(cls));
        NSArray *devices = MTLCopyAllDevices();
        for (id d in devices)
            fprintf(stderr, "STK enumerated Metal device class: %s\n", class_getName(object_getClass(d)));
        [devices release];
        Method texture = class_getInstanceMethod(cls, @selector(newTextureWithDescriptor:));
        if (texture) original_texture = method_setImplementation(texture, (IMP)texture_new);
        if (!getenv("STK_METAL_ARCHIVES")) { [device release]; return; }
        Method sync = class_getInstanceMethod(cls, @selector(newRenderPipelineStateWithDescriptor:error:));
        Method async = class_getInstanceMethod(cls, @selector(newRenderPipelineStateWithDescriptor:completionHandler:));
        if (sync) original_sync = method_setImplementation(sync, (IMP)pipeline_sync);
        if (async) original_async = method_setImplementation(async, (IMP)pipeline_async);
        fprintf(stderr, "STK Metal archive hooks: sync=%d async=%d\n", sync != NULL, async != NULL);
        [device release];
    }
}
