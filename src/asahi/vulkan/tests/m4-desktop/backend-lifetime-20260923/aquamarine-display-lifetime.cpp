// SPDX-License-Identifier: MIT
// Run under gpu-experiment.py against the old and repaired Aquamarine libraries.
// Uses only a render node; never acquires the desktop's display or input seat.
#include "backend/drm/Renderer.hpp"
#include <aquamarine/allocator/GBM.hpp>
#include <aquamarine/backend/Backend.hpp>
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

using namespace Aquamarine;

static bool pixel(const CDRMRenderer& renderer) {
    if (!eglMakeCurrent(renderer.egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, renderer.egl.context))
        return false;
    GLuint texture = 0, framebuffer = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char rgba[4] = {};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    return glGetError() == GL_NO_ERROR && rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 255 && rgba[3] == 255;
}

int main(int argc, char**) {
    const bool expectBroken = argc > 1;
    SBackendImplementationOptions implementation;
    implementation.backendType = AQ_BACKEND_NULL;
    SBackendOptions options;
    options.logFunction = [](eBackendLogLevel level, std::string message) {
        if (level >= AQ_LOG_WARNING)
            fprintf(stderr, "%s\n", message.c_str());
    };
    auto backend = CBackend::create({implementation}, options);
    assert(backend);
    auto allocator = CGBMAllocator::create(open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC), backend);
    assert(allocator);
    auto primary = CDRMRenderer::attempt(backend, allocator->drmFD());
    if (!primary)
        primary = CDRMRenderer::attempt(backend, allocator);
    assert(primary && pixel(*primary));
    for (int i = 0; i < 3; ++i) {
        auto secondaryAllocator = CGBMAllocator::create(open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC), backend);
        assert(secondaryAllocator);
        auto secondary = CDRMRenderer::attempt(backend, secondaryAllocator->drmFD());
        if (!secondary)
            secondary = CDRMRenderer::attempt(backend, secondaryAllocator);
        assert(secondary && pixel(*secondary));
        printf("cycle=%d shared_display=%d\n", i, primary->egl.display == secondary->egl.display);
        assert(pixel(*primary));
        secondary.reset();
        secondaryAllocator.reset();
        if (!pixel(*primary)) {
            if (expectBroken) {
                puts("PASS: original shared-display termination reproduced in isolated process");
                fflush(stdout);
                // The intentionally invalidated renderer cannot be cleaned up safely.
                _exit(0);
            }
            puts("FAIL: destroying secondary renderer invalidated primary rendering");
            return 1;
        }
    }
    assert(!expectBroken);
    primary.reset();
    allocator.reset();
    puts("PASS: primary pixel readback survives three real secondary renderer lifetimes");
}
