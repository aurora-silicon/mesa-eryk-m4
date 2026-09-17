/* A bounded, stdin-controlled layer surface for compositor occlusion checks.
 * Commands: r/red, b/blue, q/quit. Run only under the GPU experiment guard.
 * Generate wlr-layer-shell-unstable-v1 client code before linking libwayland-client.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_output *output;
static struct zwlr_layer_shell_v1 *shell;
static struct wl_surface *surface;
static int configured;

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(interface, "wl_compositor"))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, "wl_shm"))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, "wl_output") && !output)
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    else if (!strcmp(interface, "zwlr_layer_shell_v1"))
        shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
}

static void removed(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t width, uint32_t height) {
    (void)data; (void)width; (void)height;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    configured = 1;
}

static void closed(void *data, struct zwlr_layer_surface_v1 *layer) {
    (void)data; (void)layer;
    exit(2);
}

static void paint(uint32_t color) {
    const int width = 64, height = 32, bytes = width * height * 4;
    int fd = memfd_create("layer-occlusion-test", MFD_CLOEXEC);
    assert(fd >= 0 && ftruncate(fd, bytes) == 0);
    uint32_t *pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(pixels != MAP_FAILED);
    for (int i = 0; i < width * height; ++i)
        pixels[i] = color;
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, bytes);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(pixels, bytes);
    close(fd);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
    /* The storage is immutable; destroying the client proxy cannot recycle it
     * while the compositor still references the committed buffer. */
    wl_buffer_destroy(buffer);
    assert(wl_display_roundtrip(display) >= 0);
    puts("committed");
    fflush(stdout);
}

int main(void) {
    alarm(35);
    display = wl_display_connect(NULL);
    assert(display);
    struct wl_registry *registry = wl_display_get_registry(display);
    static const struct wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(registry, &listener, NULL);
    assert(wl_display_roundtrip(display) >= 0);
    assert(compositor && shm && output && shell);
    surface = wl_compositor_create_surface(compositor);
    struct zwlr_layer_surface_v1 *layer = zwlr_layer_shell_v1_get_layer_surface(
        shell, surface, output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "m4-occlusion-test");
    static const struct zwlr_layer_surface_v1_listener layer_listener = {configure, closed};
    zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
    zwlr_layer_surface_v1_set_size(layer, 64, 32);
    zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_margin(layer, 96, 0, 0, 96);
    zwlr_layer_surface_v1_set_exclusive_zone(layer, -1);
    struct wl_region *opaque = wl_compositor_create_region(compositor);
    wl_region_add(opaque, 0, 0, 64, 32);
    wl_surface_set_opaque_region(surface, opaque);
    wl_region_destroy(opaque);
    wl_surface_commit(surface);
    while (!configured)
        assert(wl_display_dispatch(display) >= 0);
    paint(0xffff0000);
    for (int i = 0; i < 128; ++i) {
        int command = getchar();
        if (command == 'q' || command == EOF)
            break;
        if (command == 'r' || command == 'b')
            paint(command == 'r' ? 0xffff0000 : 0xff0000ff);
    }
    zwlr_layer_surface_v1_destroy(layer);
    wl_surface_destroy(surface);
    wl_display_flush(display);
    wl_display_disconnect(display);
    return 0;
}
