// probe: layer-shell + wl_output + fractional-scale + input-method-v2 + virtual-keyboard smoke test
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
#include "wlr-layer-shell-unstable-v1-client.h"
#include "virtual-keyboard-unstable-v1-client.h"
#include "input-method-unstable-v2-client.h"
#include "fractional-scale-v1-client.h"
#include "viewporter-client.h"
#include "xdg-output-unstable-v1-client.h"
static struct zxdg_output_manager_v1 *xom;

static struct wl_compositor *comp; static struct wl_shm *shm; static struct wl_seat *seat;
static struct zwlr_layer_shell_v1 *ls; static struct wp_fractional_scale_manager_v1 *fsm;
static struct wp_viewporter *vp; static struct zwp_input_method_manager_v2 *imm;
static struct zwp_virtual_keyboard_manager_v1 *vkm;
static struct wl_output *outs[8]; static int nout;
static const struct wl_pointer_listener pl;
static struct wl_surface *surf; static struct zwlr_layer_surface_v1 *lsurf;
static struct zwp_virtual_keyboard_v1 *vk; static struct zwp_input_method_v2 *im;
static int im_active, im_pending, im_serial, did_send; static int H = 200; static double fscale = 0;

static void o_geom(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp, const char *mk, const char *md, int32_t tr)
{ (void)o;(void)sp;(void)mk;(void)md; printf("output[%ld] geometry x=%d y=%d phys=%dx%d transform=%d\n",(long)(intptr_t)d,x,y,pw,ph,tr); }
static void o_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r)
{ (void)o; printf("output[%ld] flags=%u mode %dx%d@%d (physical, untransformed)\n",(long)(intptr_t)d,f,w,h,r); }
static void o_done(void *d, struct wl_output *o) { (void)o; printf("output[%ld] done\n",(long)(intptr_t)d); fflush(stdout); }
static void o_scale(void *d, struct wl_output *o, int32_t s) { (void)o; printf("output[%ld] scale %d\n",(long)(intptr_t)d,s); }
static void o_name(void *d, struct wl_output *o, const char *n) { (void)o; printf("output[%ld] name %s\n",(long)(intptr_t)d,n); }
static void o_desc(void *d, struct wl_output *o, const char *n) { (void)d;(void)o;(void)n; }
static const struct wl_output_listener ol = { o_geom, o_mode, o_done, o_scale, o_name, o_desc };

static void reg(void *d, struct wl_registry *r, uint32_t n, const char *i, uint32_t v)
{
	(void)d;
	if (!strcmp(i, wl_compositor_interface.name)) comp = wl_registry_bind(r, n, &wl_compositor_interface, v < 6 ? v : 6);
	else if (!strcmp(i, wl_shm_interface.name)) shm = wl_registry_bind(r, n, &wl_shm_interface, 1);
	else if (!strcmp(i, wl_seat_interface.name)) { seat = wl_registry_bind(r, n, &wl_seat_interface, 1); wl_pointer_add_listener(wl_seat_get_pointer(seat), &pl, NULL); }
	else if (!strcmp(i, wl_output_interface.name) && nout < 8) { outs[nout] = wl_registry_bind(r, n, &wl_output_interface, v < 4 ? v : 4); wl_output_add_listener(outs[nout], &ol, (void*)(intptr_t)nout); nout++; }
	else if (!strcmp(i, zwlr_layer_shell_v1_interface.name)) ls = wl_registry_bind(r, n, &zwlr_layer_shell_v1_interface, v < 4 ? v : 4);
	else if (!strcmp(i, wp_fractional_scale_manager_v1_interface.name)) fsm = wl_registry_bind(r, n, &wp_fractional_scale_manager_v1_interface, 1);
	else if (!strcmp(i, zxdg_output_manager_v1_interface.name)) xom = wl_registry_bind(r, n, &zxdg_output_manager_v1_interface, 3);
	else if (!strcmp(i, wp_viewporter_interface.name)) vp = wl_registry_bind(r, n, &wp_viewporter_interface, 1);
	else if (!strcmp(i, zwp_input_method_manager_v2_interface.name)) imm = wl_registry_bind(r, n, &zwp_input_method_manager_v2_interface, 1);
	else if (!strcmp(i, zwp_virtual_keyboard_manager_v1_interface.name)) vkm = wl_registry_bind(r, n, &zwp_virtual_keyboard_manager_v1_interface, 1);
}
static void unreg(void *d, struct wl_registry *r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener rl = { reg, unreg };

static void xo_pos(void *d, struct zxdg_output_v1 *o, int32_t x, int32_t y) { (void)d;(void)o;(void)x;(void)y; }
static void xo_size(void *d, struct zxdg_output_v1 *o, int32_t w, int32_t h) { (void)o; printf("xdg_output[%ld] logical_size %dx%d\n",(long)(intptr_t)d,w,h); fflush(stdout); }
static void xo_done(void *d, struct zxdg_output_v1 *o) { (void)d;(void)o; }
static void xo_name(void *d, struct zxdg_output_v1 *o, const char *n) { (void)d;(void)o;(void)n; }
static const struct zxdg_output_v1_listener xol = { xo_pos, xo_size, xo_done, xo_name, xo_name };
static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *x, wl_fixed_t sx, wl_fixed_t sy) { (void)d;(void)p;(void)s;(void)x; printf("pointer enter %.1f,%.1f\n", wl_fixed_to_double(sx), wl_fixed_to_double(sy)); fflush(stdout); }
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *x) { (void)d;(void)p;(void)s;(void)x; }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t sx, wl_fixed_t sy) { (void)d;(void)p;(void)t; printf("pointer motion %.1f,%.1f\n", wl_fixed_to_double(sx), wl_fixed_to_double(sy)); }
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) { (void)d;(void)p;(void)s;(void)t; printf("pointer button 0x%x %s\n", b, st ? "down" : "up"); fflush(stdout); }
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) { (void)d;(void)p;(void)t;(void)a;(void)v; }
static const struct wl_pointer_listener pl = { p_enter, p_leave, p_motion, p_button, p_axis, NULL, NULL, NULL, NULL };
static void draw(int w, int h)
{
	int stride = w * 4, size = stride * h;
	int fd = memfd_create("probe", MFD_CLOEXEC); if (ftruncate(fd, size) < 0) return;
	uint32_t *px = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	for (int k = 0; k < w*h; k++) px[k] = 0xff3050a0;
	munmap(px, size);
	struct wl_shm_pool *p = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *b = wl_shm_pool_create_buffer(p, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(p); close(fd);
	wl_surface_attach(surf, b, 0, 0); wl_surface_damage_buffer(surf, 0, 0, w, h); wl_surface_commit(surf);
}
static void ls_conf(void *d, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
	(void)d; printf("layer_surface configure %ux%u (logical)\n", w, h); fflush(stdout);
	zwlr_layer_surface_v1_ack_configure(l, serial);
	draw(w ? w : 100, h ? h : 100);
}
static void ls_closed(void *d, struct zwlr_layer_surface_v1 *l) { (void)d;(void)l; printf("layer_surface closed\n"); exit(0); }
static const struct zwlr_layer_surface_v1_listener lsl = { ls_conf, ls_closed };

static void s_enter(void *d, struct wl_surface *s, struct wl_output *o) { (void)d;(void)s; for (int k=0;k<nout;k++) if (outs[k]==o) printf("surface enter output[%d]\n",k); }
static void s_leave(void *d, struct wl_surface *s, struct wl_output *o) { (void)d;(void)s;(void)o; printf("surface leave\n"); }
static void s_pscale(void *d, struct wl_surface *s, int32_t f) { (void)d;(void)s; printf("surface preferred_buffer_scale %d\n", f); }
static void s_ptr(void *d, struct wl_surface *s, uint32_t t) { (void)d;(void)s; printf("surface preferred_buffer_transform %u\n", t); }
static const struct wl_surface_listener sl = { s_enter, s_leave, s_pscale, s_ptr };
static void fs_pref(void *d, struct wp_fractional_scale_v1 *f, uint32_t s) { (void)d;(void)f; fscale = s/120.0; printf("fractional preferred_scale %u/120 = %.3f\n", s, fscale); fflush(stdout); }
static const struct wp_fractional_scale_v1_listener fsl = { fs_pref };

static void im_act(void *d, struct zwp_input_method_v2 *m) { (void)d;(void)m; im_pending = 1; }
static void im_deact(void *d, struct zwp_input_method_v2 *m) { (void)d;(void)m; im_pending = 0; }
static void im_surr(void *d, struct zwp_input_method_v2 *m, const char *t, uint32_t c, uint32_t a) { (void)d;(void)m;(void)t;(void)c;(void)a; }
static void im_cause(void *d, struct zwp_input_method_v2 *m, uint32_t c) { (void)d;(void)m;(void)c; }
static void im_ct(void *d, struct zwp_input_method_v2 *m, uint32_t h, uint32_t p) { (void)d;(void)m; printf("im content_type hint=%u purpose=%u\n", h, p); }
static void im_done(void *d, struct zwp_input_method_v2 *m)
{
	(void)d;(void)m; im_serial++;
	if (im_pending != im_active) { im_active = im_pending; printf("im %s (serial %d)\n", im_active ? "ACTIVATE -> would show keyboard" : "DEACTIVATE -> would hide keyboard", im_serial); fflush(stdout); }
}
static void im_unavail(void *d, struct zwp_input_method_v2 *m) { (void)d;(void)m; printf("im UNAVAILABLE (another input method is bound)\n"); fflush(stdout); }
static const struct zwp_input_method_v2_listener iml = { im_act, im_deact, im_surr, im_cause, im_ct, im_done, im_unavail };

static void send_keys(void)
{
	// "hi" via virtual keyboard: evdev KEY_H=35, KEY_I=23
	uint32_t keys[] = { 35, 23 }; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	uint32_t t = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	for (int k = 0; k < 2; k++) { zwp_virtual_keyboard_v1_key(vk, t, keys[k], 1); zwp_virtual_keyboard_v1_key(vk, t+1, keys[k], 0); }
	if (im && im_active) { zwp_input_method_v2_commit_string(im, "+im"); zwp_input_method_v2_commit(im, im_serial); }
	printf("sent vk keys 'hi'%s\n", im_active ? " and im commit_string '+im'" : ""); fflush(stdout);
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 10;
	struct wl_display *dpy = wl_display_connect(NULL); if (!dpy) { perror("connect"); return 1; }
	struct wl_registry *r = wl_display_get_registry(dpy); wl_registry_add_listener(r, &rl, NULL);
	wl_display_roundtrip(dpy); wl_display_roundtrip(dpy);
	printf("globals: layer_shell=%d fractional=%d viewporter=%d im_mgr=%d vk_mgr=%d outputs=%d\n", !!ls, !!fsm, !!vp, !!imm, !!vkm, nout);
	if (!ls || !comp || !shm) return 1;
	if (xom) for (int k=0;k<nout;k++) zxdg_output_v1_add_listener(zxdg_output_manager_v1_get_xdg_output(xom, outs[k]), &xol, (void*)(intptr_t)k);
	if (getenv("PROBE_IM_ONLY")) { if (!imm) return 1; im = zwp_input_method_manager_v2_get_input_method(imm, seat); zwp_input_method_v2_add_listener(im, &iml, NULL); wl_display_roundtrip(dpy); wl_display_roundtrip(dpy); printf("im bound, no unavailable event => free\n"); return 0; }
	surf = wl_compositor_create_surface(comp); wl_surface_add_listener(surf, &sl, NULL);
	if (fsm) { struct wp_fractional_scale_v1 *f = wp_fractional_scale_manager_v1_get_fractional_scale(fsm, surf); wp_fractional_scale_v1_add_listener(f, &fsl, NULL); }
	lsurf = zwlr_layer_shell_v1_get_layer_surface(ls, surf, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "probe");
	zwlr_layer_surface_v1_add_listener(lsurf, &lsl, NULL);
	zwlr_layer_surface_v1_set_anchor(lsurf, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(lsurf, 0, H); zwlr_layer_surface_v1_set_exclusive_zone(lsurf, H);
	zwlr_layer_surface_v1_set_keyboard_interactivity(lsurf, 0);
	wl_surface_commit(surf);
	if (vkm && seat) {
		vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkm, seat);
		struct xkb_context *c = xkb_context_new(0); struct xkb_rule_names n = { .layout = "us" };
		struct xkb_keymap *km = xkb_keymap_new_from_names(c, &n, 0); char *s = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
		size_t len = strlen(s) + 1; int fd = memfd_create("km", MFD_CLOEXEC); if (write(fd, s, len) != (ssize_t)len) return 1;
		zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, len); close(fd); free(s);
	}
	if (imm && seat && !getenv("PROBE_NO_IM")) { im = zwp_input_method_manager_v2_get_input_method(imm, seat); zwp_input_method_v2_add_listener(im, &iml, NULL); }
	time_t end = time(NULL) + secs, sendat = time(NULL) + (getenv("PROBE_SEND_AT") ? atoi(getenv("PROBE_SEND_AT")) : 3);
	while (time(NULL) < end) {
		wl_display_flush(dpy);
		struct pollfd pfd = { wl_display_get_fd(dpy), POLLIN, 0 };
		if (poll(&pfd, 1, 200) > 0 && wl_display_dispatch(dpy) < 0) { printf("display error\n"); return 1; }
		if (!did_send && vk && time(NULL) >= sendat) { send_keys(); did_send = 1; }
	}
	return 0;
}
