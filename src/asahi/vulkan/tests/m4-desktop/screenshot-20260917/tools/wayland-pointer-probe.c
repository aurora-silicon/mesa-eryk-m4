/* Bounded probe input: stdin lines "m dx dy" or "b state". EOF releases.
 * Build with wayland-scanner client-header/private-code for the Hyprland
 * wlr-virtual-pointer-unstable-v1.xml and cc ... -lwayland-client.
 * Run only under the graphics experiment guard, with the desktop environment.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include "virtual-pointer.h"
static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    if (!strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {}
int main(void) {
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) return 1;
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(registry, &listener, NULL);
    if (wl_display_roundtrip(display) < 0 || !manager) return 2;
    struct zwlr_virtual_pointer_v1 *pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, NULL);
    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
        int x = 0, y = 0;
        if (sscanf(line, "m %d %d", &x, &y) == 2)
            zwlr_virtual_pointer_v1_motion(pointer, ms, wl_fixed_from_int(x), wl_fixed_from_int(y));
        else if (sscanf(line, "b %d", &x) == 1)
            zwlr_virtual_pointer_v1_button(pointer, ms, 272, x ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(pointer);
        if (wl_display_flush(display) < 0) break;
    }
    zwlr_virtual_pointer_v1_button(pointer, 0, 272, WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(pointer);
    zwlr_virtual_pointer_v1_destroy(pointer);
    zwlr_virtual_pointer_manager_v1_destroy(manager);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);
    return 0;
}
