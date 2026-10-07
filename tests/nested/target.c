// target: xdg_toplevel that logs wl_keyboard keys and enables text-input-v3 (to trigger IM activate)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client.h"
#include "text-input-unstable-v3-client.h"

static struct wl_compositor *comp; static struct wl_shm *shm; static struct wl_seat *seat;
static struct xdg_wm_base *wm; static struct zwp_text_input_manager_v3 *tim;
static struct wl_surface *surf; static struct zwp_text_input_v3 *ti;
static struct xkb_context *xc; static struct xkb_keymap *km; static struct xkb_state *xs;
static int ti_on_enter = 1;

static void reg(void *d, struct wl_registry *r, uint32_t n, const char *i, uint32_t v)
{
	(void)d;(void)v;
	if (!strcmp(i, wl_compositor_interface.name)) comp = wl_registry_bind(r, n, &wl_compositor_interface, 4);
	else if (!strcmp(i, wl_shm_interface.name)) shm = wl_registry_bind(r, n, &wl_shm_interface, 1);
	else if (!strcmp(i, wl_seat_interface.name)) seat = wl_registry_bind(r, n, &wl_seat_interface, 5);
	else if (!strcmp(i, xdg_wm_base_interface.name)) wm = wl_registry_bind(r, n, &xdg_wm_base_interface, 1);
	else if (!strcmp(i, zwp_text_input_manager_v3_interface.name)) tim = wl_registry_bind(r, n, &zwp_text_input_manager_v3_interface, 1);
}
static void unreg(void *d, struct wl_registry *r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener rl = { reg, unreg };
static void ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wml = { ping };

static void draw(int w, int h)
{
	int stride = w*4, size = stride*h; int fd = memfd_create("t", MFD_CLOEXEC); if (ftruncate(fd, size) < 0) return;
	uint32_t *px = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0); for (int k=0;k<w*h;k++) px[k]=0xffe0e0e0; munmap(px,size);
	struct wl_shm_pool *p = wl_shm_create_pool(shm, fd, size); struct wl_buffer *b = wl_shm_pool_create_buffer(p,0,w,h,stride,WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(p); close(fd); wl_surface_attach(surf,b,0,0); wl_surface_damage_buffer(surf,0,0,w,h); wl_surface_commit(surf);
}
static void xs_conf(void *d, struct xdg_surface *x, uint32_t s) { (void)d; xdg_surface_ack_configure(x, s); draw(400, 300); }
static const struct xdg_surface_listener xsl = { xs_conf };
static void tl_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) { (void)d;(void)t;(void)w;(void)h;(void)s; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d;(void)t; exit(0); }
static const struct xdg_toplevel_listener tll = { tl_conf, tl_close, NULL, NULL };

static void ti_enable(int on)
{
	if (!ti) return;
	if (on) { zwp_text_input_v3_enable(ti); zwp_text_input_v3_set_content_type(ti, 0, getenv("TARGET_PURPOSE") ? (uint32_t)atoi(getenv("TARGET_PURPOSE")) : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL); }
	else zwp_text_input_v3_disable(ti);
	zwp_text_input_v3_commit(ti); printf("text_input %s\n", on ? "enable" : "disable"); fflush(stdout);
}
static void t_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s) { (void)d;(void)t;(void)s; printf("text_input enter\n"); if (ti_on_enter) ti_enable(1); }
static void t_leave(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s) { (void)d;(void)t;(void)s; printf("text_input leave\n"); ti_enable(0); }
static void t_pre(void *d, struct zwp_text_input_v3 *t, const char *x, int32_t a, int32_t b) { (void)d;(void)t;(void)x;(void)a;(void)b; }
static void t_commit(void *d, struct zwp_text_input_v3 *t, const char *x) { (void)d;(void)t; printf("text_input commit_string '%s'\n", x ? x : ""); fflush(stdout); }
static void t_del(void *d, struct zwp_text_input_v3 *t, uint32_t a, uint32_t b) { (void)d;(void)t;(void)a;(void)b; }
static void t_done(void *d, struct zwp_text_input_v3 *t, uint32_t s) { (void)d;(void)t;(void)s; }
static const struct zwp_text_input_v3_listener til = { t_enter, t_leave, t_pre, t_commit, t_del, t_done };

static void k_map(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t sz)
{
	(void)d;(void)k; char *m = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0); close(fd); if (m == MAP_FAILED) return;
	if (xs) xkb_state_unref(xs); if (km) xkb_keymap_unref(km);
	km = xkb_keymap_new_from_string(xc, m, f == 1 ? XKB_KEYMAP_FORMAT_TEXT_V1 : 0, 0); munmap(m, sz); xs = km ? xkb_state_new(km) : NULL;
	printf("keyboard keymap received (%u bytes)\n", sz);
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *x, struct wl_array *a) { (void)d;(void)k;(void)s;(void)x;(void)a; printf("keyboard enter\n"); fflush(stdout); }
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *x) { (void)d;(void)k;(void)s;(void)x; printf("keyboard leave\n"); }
static void k_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st)
{
	(void)d;(void)k;(void)s;(void)t; char name[64] = "?";
	if (xs) xkb_keysym_get_name(xkb_state_key_get_one_sym(xs, key + 8), name, sizeof name);
	printf("key %u (%s) %s\n", key, name, st ? "down" : "up"); fflush(stdout);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g)
{ (void)d;(void)k;(void)s; if (xs) xkb_state_update_mask(xs, a, b, c, 0, 0, g); printf("modifiers dep=%u lat=%u lock=%u\n", a, b, c); fflush(stdout); }
static void k_rep(void *d, struct wl_keyboard *k, int32_t r, int32_t dl) { (void)d;(void)k;(void)r;(void)dl; }
static const struct wl_keyboard_listener kl = { k_map, k_enter, k_leave, k_key, k_mods, k_rep };

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 15; if (getenv("TARGET_NO_TI")) ti_on_enter = 0;
	int toggle = getenv("TARGET_TOGGLE_TI") ? atoi(getenv("TARGET_TOGGLE_TI")) : 0;
	/* re-send enable while already enabled (what GTK/Qt do to request the OSK on a tap) */
	int reenable = getenv("TARGET_REENABLE_TI") ? atoi(getenv("TARGET_REENABLE_TI")) : 0;
	xc = xkb_context_new(0);
	struct wl_display *dpy = wl_display_connect(NULL); if (!dpy) { perror("connect"); return 1; }
	struct wl_registry *r = wl_display_get_registry(dpy); wl_registry_add_listener(r, &rl, NULL); wl_display_roundtrip(dpy);
	if (!comp || !wm || !seat) return 1;
	xdg_wm_base_add_listener(wm, &wml, NULL);
	surf = wl_compositor_create_surface(comp);
	struct xdg_surface *x = xdg_wm_base_get_xdg_surface(wm, surf); xdg_surface_add_listener(x, &xsl, NULL);
	struct xdg_toplevel *t = xdg_surface_get_toplevel(x); xdg_toplevel_add_listener(t, &tll, NULL); xdg_toplevel_set_title(t, "slatekbd-target");
	xdg_toplevel_set_app_id(t, "slatekbd-target");
	struct wl_keyboard *kb = wl_seat_get_keyboard(seat); wl_keyboard_add_listener(kb, &kl, NULL);
	if (tim) { ti = zwp_text_input_manager_v3_get_text_input(tim, seat); zwp_text_input_v3_add_listener(ti, &til, NULL); }
	wl_surface_commit(surf);
	time_t start = time(NULL), end = start + secs; int toggled = 0, reenabled = 0;
	while (time(NULL) < end) {
		wl_display_flush(dpy); struct pollfd p = { wl_display_get_fd(dpy), POLLIN, 0 };
		if (poll(&p, 1, 200) > 0 && wl_display_dispatch(dpy) < 0) return 1;
		if (toggle && !toggled && time(NULL) >= start + toggle) { ti_enable(0); toggled = 1; }
		if (reenable && !reenabled && time(NULL) >= start + reenable) { ti_enable(1); reenabled = 1; }
	}
	return 0;
}
