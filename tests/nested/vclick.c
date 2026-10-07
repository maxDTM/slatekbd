// vclick X Y W H: absolute-move a virtual pointer to (X,Y) within a WxH logical layout extent and left-click.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include "wlr-virtual-pointer-unstable-v1-client.h"
static struct wl_seat *seat; static struct zwlr_virtual_pointer_manager_v1 *m;
static void reg(void *d, struct wl_registry *r, uint32_t n, const char *i, uint32_t v)
{ (void)d;(void)v; if (!strcmp(i,"wl_seat")) seat = wl_registry_bind(r,n,&wl_seat_interface,1);
  else if (!strcmp(i,zwlr_virtual_pointer_manager_v1_interface.name)) m = wl_registry_bind(r,n,&zwlr_virtual_pointer_manager_v1_interface,1); }
static void unreg(void *d, struct wl_registry *r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener rl = { reg, unreg };
int main(int c, char **v)
{
	if (c < 5) { fprintf(stderr, "usage: vclick X Y W H [hold_ms]\n"); return 2; }
	struct wl_display *d = wl_display_connect(NULL); if (!d) return 1;
	wl_registry_add_listener(wl_display_get_registry(d), &rl, NULL); wl_display_roundtrip(d);
	if (!m) { fprintf(stderr, "no virtual pointer manager\n"); return 1; }
	struct zwlr_virtual_pointer_v1 *p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(m, seat);
	zwlr_virtual_pointer_v1_motion_absolute(p, 0, atoi(v[1]), atoi(v[2]), atoi(v[3]), atoi(v[4])); zwlr_virtual_pointer_v1_frame(p);
	wl_display_roundtrip(d); usleep(50000);
	zwlr_virtual_pointer_v1_button(p, 1, 0x110, 1); zwlr_virtual_pointer_v1_frame(p); wl_display_roundtrip(d);
	usleep((c > 5 ? atoi(v[5]) : 60) * 1000);
	zwlr_virtual_pointer_v1_button(p, 2, 0x110, 0); zwlr_virtual_pointer_v1_frame(p); wl_display_roundtrip(d);
	zwlr_virtual_pointer_v1_destroy(p); wl_display_roundtrip(d); return 0;
}
