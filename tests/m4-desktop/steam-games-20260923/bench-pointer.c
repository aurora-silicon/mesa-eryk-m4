#define _POSIX_C_SOURCE 200809L
#include <wayland-client.h>
#include "virtual-pointer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data,struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version) {
 (void)data;
 if (!strcmp(interface,zwlr_virtual_pointer_manager_v1_interface.name))
  manager=wl_registry_bind(registry,name,&zwlr_virtual_pointer_manager_v1_interface,version>=2?2:1);
}
static void gone(void *data,struct wl_registry *r,uint32_t name) {(void)data;(void)r;(void)name;}
static uint32_t now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);}
int main(int argc,char **argv) {
 if(argc!=5&&argc!=6){fprintf(stderr,"usage: bench-pointer X Y EXTENT_WIDTH EXTENT_HEIGHT [click]\n");return 2;}
 unsigned x=strtoul(argv[1],0,10),y=strtoul(argv[2],0,10),w=strtoul(argv[3],0,10),h=strtoul(argv[4],0,10);
 if(!w||!h||x>w||y>h)return 2;
 struct wl_display *d=wl_display_connect(NULL);if(!d)return 3;
 struct wl_registry *r=wl_display_get_registry(d);
 static const struct wl_registry_listener l={global,gone};wl_registry_add_listener(r,&l,NULL);wl_display_roundtrip(d);
 if(!manager){fprintf(stderr,"virtual-pointer protocol unavailable\n");return 4;}
 struct zwlr_virtual_pointer_v1 *p=zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager,NULL);
 zwlr_virtual_pointer_v1_motion_absolute(p,now(),x,y,w,h);zwlr_virtual_pointer_v1_frame(p);wl_display_roundtrip(d);
 if(argc==6){struct timespec pause={2,0};nanosleep(&pause,NULL);zwlr_virtual_pointer_v1_button(p,now(),272,WL_POINTER_BUTTON_STATE_PRESSED);zwlr_virtual_pointer_v1_frame(p);wl_display_roundtrip(d);nanosleep(&pause,NULL);zwlr_virtual_pointer_v1_button(p,now(),272,WL_POINTER_BUTTON_STATE_RELEASED);zwlr_virtual_pointer_v1_frame(p);wl_display_roundtrip(d);}
 zwlr_virtual_pointer_v1_destroy(p);wl_display_roundtrip(d);wl_display_disconnect(d);return 0;
}
