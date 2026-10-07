/* wayland.c - registry, outputs, seat (touch + pointer), layer surface, scale, frames. */
#include "wayland.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "fractional-scale-v1-client-protocol.h"
#include "hyprland-lock-notify-v1-client-protocol.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "render.h"
#include "settings.h"
#include "shm.h"
#include "viewporter-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* ------------------------------------------------------------------ outputs */
static struct output *output_from_wl(struct app *a, struct wl_output *wl)
{
	struct output *o;
	wl_list_for_each(o, &a->outputs, link)
		if (o->wl == wl) return o;
	return NULL;
}

static void out_geometry(void *data, struct wl_output *wl, int32_t x, int32_t y, int32_t pw, int32_t ph,
                         int32_t subpixel, const char *make, const char *model, int32_t transform)
{
	(void)ph; (void)wl; (void)x; (void)y; (void)pw; (void)subpixel; (void)make; (void)model;
	struct output *o = data;
	o->transform = transform;
}

static void out_mode(void *data, struct wl_output *wl, uint32_t flags, int32_t w, int32_t h, int32_t refresh)
{
	(void)refresh; (void)wl;
	struct output *o = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		o->mode_w = w;
		o->mode_h = h;
	}
}

static void out_done(void *data, struct wl_output *wl)
{
	(void)wl;
	struct output *o = data;
	struct app *a = o->app;
	bool first = !o->done;
	o->done = true;
	if (first) DBG(a, "output %s: %dx%d logical %dx%d scale %d", o->name, o->mode_w, o->mode_h, o->lw, o->lh, o->scale);
	/* hotplug: move to the preferred output once it appears */
	if (first && a->visible && a->surface && wl_pick_output(a) == o && a->layer_output != o) a->need_recreate = true;
	app_check_orientation(a);
}

static void out_scale(void *data, struct wl_output *wl, int32_t factor)
{
	(void)wl;
	struct output *o = data;
	o->scale = factor;
}

static void out_name(void *data, struct wl_output *wl, const char *name)
{
	(void)wl;
	struct output *o = data;
	snprintf(o->name, sizeof o->name, "%s", name);
}

static void out_description(void *data, struct wl_output *wl, const char *desc) { (void)data; (void)wl; (void)desc; }

static const struct wl_output_listener output_listener = {
	.geometry = out_geometry,
	.mode = out_mode,
	.done = out_done,
	.scale = out_scale,
	.name = out_name,
	.description = out_description,
};

static void xo_position(void *data, struct zxdg_output_v1 *x, int32_t px, int32_t py) { (void)data; (void)x; (void)px; (void)py; }

static void xo_size(void *data, struct zxdg_output_v1 *x, int32_t w, int32_t h)
{
	(void)x;
	struct output *o = data;
	o->lw = w;
	o->lh = h;
	/* Hyprland sends this before the matching configure; apply right away */
	app_check_orientation(o->app);
}

static void xo_done(void *data, struct zxdg_output_v1 *x) { (void)data; (void)x; }

static void xo_name(void *data, struct zxdg_output_v1 *x, const char *name)
{
	(void)x;
	struct output *o = data;
	if (!o->name[0]) snprintf(o->name, sizeof o->name, "%s", name);
}

static void xo_description(void *data, struct zxdg_output_v1 *x, const char *d) { (void)data; (void)x; (void)d; }

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xo_position,
	.logical_size = xo_size,
	.done = xo_done,
	.name = xo_name,
	.description = xo_description,
};

static void output_add_xdg(struct app *a, struct output *o)
{
	if (o->xdg || !a->xdg_out_mgr) return;
	o->xdg = zxdg_output_manager_v1_get_xdg_output(a->xdg_out_mgr, o->wl);
	zxdg_output_v1_add_listener(o->xdg, &xdg_output_listener, o);
}

static void output_free(struct output *o)
{
	if (o->xdg) zxdg_output_v1_destroy(o->xdg);
	if (wl_output_get_version(o->wl) >= WL_OUTPUT_RELEASE_SINCE_VERSION) wl_output_release(o->wl);
	else wl_output_destroy(o->wl);
	wl_list_remove(&o->link);
	free(o);
}

struct output *wl_pick_output(const struct app *a)
{
	struct output *o;
	if (strcmp(a->cfg.output, "auto") != 0) {
		wl_list_for_each(o, &a->outputs, link)
			if (!strcmp(o->name, a->cfg.output)) return o;
		return NULL;
	}
	wl_list_for_each(o, &a->outputs, link)
		if (!strncmp(o->name, "eDP", 3) || !strncmp(o->name, "LVDS", 4) || !strncmp(o->name, "DSI", 3)) return o;
	return NULL;
}

/* ---------------------------------------------------------------- routing */
static void route_down(struct app *a, int32_t id, bool ptr, float x, float y, uint32_t t)
{
	if (a->collapsed) {
		a->btn_pressing = true;
		a->btn_id = id;
		a->btn_ptr = ptr;
		app_mark_dirty(a);
		return;
	}
	if (a->settings_open) {
		settings_down(a, id, ptr, x, y);
		return;
	}
	const struct keybox *b = layout_hit(&a->geo, x, y);
	input_down(&a->in, id, ptr, b, x, y, t);
}

static void route_motion(struct app *a, int32_t id, bool ptr, float x, float y)
{
	if (a->collapsed) return;
	settings_motion(a, id, ptr, x, y);
	input_motion(&a->in, id, ptr, x, y);
}

static void route_up(struct app *a, int32_t id, bool ptr, uint32_t t)
{
	if (a->collapsed) {
		bool tap = a->btn_pressing && a->btn_id == id && a->btn_ptr == ptr;
		a->btn_pressing = false;
		app_mark_dirty(a);
		if (tap) app_set_collapsed(a, false);
		return;
	}
	settings_up(a, id, ptr);
	input_up(&a->in, id, ptr, t);
}

/* ------------------------------------------------------------------ touch */
static struct touch_point *tp_find(struct app *a, int32_t id)
{
	for (int i = 0; i < MAX_TOUCH; i++)
		if (a->tp[i].used && a->tp[i].id == id) return &a->tp[i];
	return NULL;
}

/* Down and up are dispatched immediately, in arrival order (a frame may carry an up
 * and a down for the same id, or a modifier up and a letter down); only motion is
 * coalesced until wl_touch.frame. */
static void tp_flush_motion(struct app *a, struct touch_point *p)
{
	if (!p->pend_motion) return;
	p->pend_motion = false;
	route_motion(a, p->id, false, p->x, p->y);
}

static void tp_end(struct app *a, struct touch_point *p, uint32_t t)
{
	tp_flush_motion(a, p);
	route_up(a, p->id, false, t);
	p->used = false;
}

static void touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time, struct wl_surface *s,
                       int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	(void)t; (void)serial; (void)time;
	struct app *a = data;
	if (!a->surface || s != a->surface) return;
	uint32_t now = now_ms(); /* own clock: device timestamps vary in base and quality */
	struct touch_point *p = tp_find(a, id);
	if (p) tp_end(a, p, now); /* id reused without an up we saw: end the old contact first */
	p = NULL;
	for (int i = 0; i < MAX_TOUCH; i++)
		if (!a->tp[i].used) { p = &a->tp[i]; break; }
	if (!p) return;
	memset(p, 0, sizeof(*p));
	p->used = true;
	p->id = id;
	p->x = (float)wl_fixed_to_double(x);
	p->y = (float)wl_fixed_to_double(y);
	p->t = now;
	route_down(a, p->id, false, p->x, p->y, p->t);
}

static void touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
	(void)t; (void)serial; (void)time;
	struct app *a = data;
	struct touch_point *p = tp_find(a, id);
	if (!p) return;
	tp_end(a, p, now_ms());
}

static void touch_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	(void)t; (void)time;
	struct app *a = data;
	struct touch_point *p = tp_find(a, id);
	if (!p) return;
	p->x = (float)wl_fixed_to_double(x);
	p->y = (float)wl_fixed_to_double(y);
	p->pend_motion = true;
}

static void touch_frame(void *data, struct wl_touch *t)
{
	(void)t;
	struct app *a = data;
	for (int i = 0; i < MAX_TOUCH; i++)
		if (a->tp[i].used) tp_flush_motion(a, &a->tp[i]);
}

static void touch_cancel(void *data, struct wl_touch *t)
{
	(void)t;
	struct app *a = data;
	DBG(a, "touch cancel");
	for (int i = 0; i < MAX_TOUCH; i++) a->tp[i].used = false;
	settings_cancel(a);
	input_cancel(&a->in);
}

static void touch_shape(void *data, struct wl_touch *t, int32_t id, wl_fixed_t maj, wl_fixed_t min) { (void)data; (void)t; (void)id; (void)maj; (void)min; }
static void touch_orientation(void *data, struct wl_touch *t, int32_t id, wl_fixed_t o) { (void)data; (void)t; (void)id; (void)o; }

static const struct wl_touch_listener touch_listener = {
	.down = touch_down,
	.up = touch_up,
	.motion = touch_motion,
	.frame = touch_frame,
	.cancel = touch_cancel,
	.shape = touch_shape,
	.orientation = touch_orientation,
};

/* ---------------------------------------------------------------- pointer */
static void ptr_flush(struct app *a)
{
	if (a->ptr_pend_down) {
		a->ptr_down = true;
		route_down(a, -1, true, a->ptr_x, a->ptr_y, a->ptr_t);
	}
	if (a->ptr_pend_motion && a->ptr_down) route_motion(a, -1, true, a->ptr_x, a->ptr_y);
	if (a->ptr_pend_up) {
		route_up(a, -1, true, a->ptr_t);
		a->ptr_down = false;
	}
	a->ptr_pend_down = a->ptr_pend_up = a->ptr_pend_motion = false;
}

static void ptr_maybe_flush(struct app *a)
{
	if (a->pointer_ver < WL_POINTER_FRAME_SINCE_VERSION) ptr_flush(a);
}

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
	(void)p; (void)serial;
	struct app *a = data;
	a->ptr_inside = s && s == a->surface;
	a->ptr_x = (float)wl_fixed_to_double(x);
	a->ptr_y = (float)wl_fixed_to_double(y);
}

static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)p; (void)serial; (void)s;
	struct app *a = data;
	a->ptr_inside = false;
	if (a->ptr_down) {
		/* treat leaving with the button held as sliding off, then release */
		a->ptr_x = -1e6f;
		a->ptr_y = -1e6f;
		a->ptr_pend_motion = true;
		a->ptr_pend_up = true;
		a->ptr_t = now_ms();
		ptr_maybe_flush(a);
	}
}

static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)p; (void)time;
	struct app *a = data;
	a->ptr_x = (float)wl_fixed_to_double(x);
	a->ptr_y = (float)wl_fixed_to_double(y);
	a->ptr_pend_motion = true;
	ptr_maybe_flush(a);
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	(void)p; (void)serial; (void)time;
	struct app *a = data;
	if (button != BTN_LEFT || !a->ptr_inside) {
		if (button == BTN_LEFT && !state && a->ptr_down) {
			a->ptr_pend_up = true;
			a->ptr_t = now_ms();
			ptr_maybe_flush(a);
		}
		return;
	}
	a->ptr_t = now_ms();
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (!a->ptr_down) a->ptr_pend_down = true;
	} else if (a->ptr_down || a->ptr_pend_down) {
		a->ptr_pend_up = true;
	}
	ptr_maybe_flush(a);
}

static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v) { (void)data; (void)p; (void)time; (void)axis; (void)v; }

static void ptr_frame(void *data, struct wl_pointer *p)
{
	(void)p;
	struct app *a = data;
	/* a down and an up in the same frame: deliver both, in order */
	if (a->ptr_pend_down && a->ptr_pend_up) {
		a->ptr_pend_up = false;
		ptr_flush(a);
		a->ptr_pend_up = true;
	}
	ptr_flush(a);
}

static void ptr_axis_source(void *data, struct wl_pointer *p, uint32_t s) { (void)data; (void)p; (void)s; }
static void ptr_axis_stop(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis) { (void)data; (void)p; (void)t; (void)axis; }
static void ptr_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t d) { (void)data; (void)p; (void)axis; (void)d; }

static const struct wl_pointer_listener pointer_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
	.frame = ptr_frame,
	.axis_source = ptr_axis_source,
	.axis_stop = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
};

/* ------------------------------------------------------------------- seat */
static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
	struct app *a = data;
	bool has_touch = caps & WL_SEAT_CAPABILITY_TOUCH, has_ptr = caps & WL_SEAT_CAPABILITY_POINTER;
	if (has_touch && !a->touch) {
		a->touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(a->touch, &touch_listener, a);
	} else if (!has_touch && a->touch) {
		touch_cancel(a, a->touch);
		if (wl_touch_get_version(a->touch) >= WL_TOUCH_RELEASE_SINCE_VERSION) wl_touch_release(a->touch);
		else wl_touch_destroy(a->touch);
		a->touch = NULL;
	}
	if (has_ptr && !a->pointer) {
		a->pointer = wl_seat_get_pointer(seat);
		a->pointer_ver = wl_pointer_get_version(a->pointer);
		wl_pointer_add_listener(a->pointer, &pointer_listener, a);
	} else if (!has_ptr && a->pointer) {
		if (a->ptr_down) {
			a->ptr_pend_up = true;
			ptr_flush(a);
		}
		if (wl_pointer_get_version(a->pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) wl_pointer_release(a->pointer);
		else wl_pointer_destroy(a->pointer);
		a->pointer = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) { (void)data; (void)seat; (void)name; }

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

/* --------------------------------------------------------------- registry */
static void reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	struct app *a = data;
	if (!strcmp(iface, wl_compositor_interface.name)) {
		a->compositor_ver = min_u32(ver, 6);
		a->compositor = wl_registry_bind(r, name, &wl_compositor_interface, a->compositor_ver);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		a->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (!strcmp(iface, wl_seat_interface.name) && !a->seat) {
		a->seat = wl_registry_bind(r, name, &wl_seat_interface, min_u32(ver, 7));
		a->seat_name = name;
		wl_seat_add_listener(a->seat, &seat_listener, a);
	} else if (!strcmp(iface, wl_output_interface.name)) {
		struct output *o = calloc(1, sizeof(*o));
		if (!o) return;
		o->app = a;
		o->global_name = name;
		o->scale = 1;
		o->wl = wl_registry_bind(r, name, &wl_output_interface, min_u32(ver, 4));
		wl_output_add_listener(o->wl, &output_listener, o);
		wl_list_insert(a->outputs.prev, &o->link);
		output_add_xdg(a, o);
	} else if (!strcmp(iface, zxdg_output_manager_v1_interface.name)) {
		a->xdg_out_mgr = wl_registry_bind(r, name, &zxdg_output_manager_v1_interface, min_u32(ver, 3));
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
		a->layer_shell_ver = min_u32(ver, 4);
		a->layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, a->layer_shell_ver);
	} else if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name)) {
		a->vk_mgr = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name)) {
		a->im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
	} else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name)) {
		a->frac_mgr = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
	} else if (!strcmp(iface, wp_viewporter_interface.name)) {
		a->viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
	} else if (!strcmp(iface, hyprland_lock_notifier_v1_interface.name)) {
		a->lock_notifier = wl_registry_bind(r, name, &hyprland_lock_notifier_v1_interface, 1);
	}
}

static void reg_global_remove(void *data, struct wl_registry *r, uint32_t name)
{
	(void)r;
	struct app *a = data;
	struct output *o, *tmp;
	wl_list_for_each_safe(o, tmp, &a->outputs, link) {
		if (o->global_name != name) continue;
		LOG(a, "output %s removed", o->name);
		bool ours = (o == a->layer_output || o == a->entered);
		if (ours && a->surface) {
			app_release_all(a);
			surface_destroy(a);
			if (a->visible) a->need_recreate = true;
		}
		if (a->entered == o) a->entered = NULL;
		if (a->layer_output == o) a->layer_output = NULL;
		output_free(o);
		app_check_orientation(a);
		return;
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = reg_global,
	.global_remove = reg_global_remove,
};

int wl_init(struct app *a)
{
	wl_list_init(&a->outputs);
	a->display = wl_display_connect(NULL);
	if (!a->display) {
		WARN(a, "cannot connect to the Wayland display (WAYLAND_DISPLAY=%s)", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "");
		return -1;
	}
	a->registry = wl_display_get_registry(a->display);
	wl_registry_add_listener(a->registry, &registry_listener, a);
	wl_display_roundtrip(a->display);
	struct output *o;
	wl_list_for_each(o, &a->outputs, link) output_add_xdg(a, o);
	wl_display_roundtrip(a->display);
	if (!a->compositor || !a->shm || !a->seat) {
		WARN(a, "compositor lacks wl_compositor/wl_shm/wl_seat");
		return -1;
	}
	if (!a->layer_shell) {
		WARN(a, "compositor does not support zwlr_layer_shell_v1 (layer-shell); cannot show a keyboard");
		return -1;
	}
	if (!a->vk_mgr) {
		WARN(a, "compositor does not support zwp_virtual_keyboard_manager_v1; cannot send keys");
		return -1;
	}
	if (!a->im_mgr) LOG(a, "no zwp_input_method_manager_v2: auto show/hide disabled");
	if (!a->frac_mgr || !a->viewporter) LOG(a, "no fractional-scale/viewporter: using integer buffer scale");
	return 0;
}

void wl_fini(struct app *a)
{
	surface_destroy(a);
	for (int i = 0; i < 2; i++) {
		shm_buffer_destroy(a->buffers[i]);
		a->buffers[i] = NULL;
	}
	struct output *o, *tmp;
	wl_list_for_each_safe(o, tmp, &a->outputs, link) output_free(o);
	if (a->touch) wl_touch_destroy(a->touch);
	if (a->pointer) wl_pointer_destroy(a->pointer);
	if (a->im_mgr) zwp_input_method_manager_v2_destroy(a->im_mgr);
	if (a->vk_mgr) zwp_virtual_keyboard_manager_v1_destroy(a->vk_mgr);
	if (a->layer_shell) {
		if (a->layer_shell_ver >= 3) zwlr_layer_shell_v1_destroy(a->layer_shell);
		else wl_proxy_destroy((struct wl_proxy *)a->layer_shell);
	}
	if (a->frac_mgr) wp_fractional_scale_manager_v1_destroy(a->frac_mgr);
	if (a->viewporter) wp_viewporter_destroy(a->viewporter);
	if (a->xdg_out_mgr) zxdg_output_manager_v1_destroy(a->xdg_out_mgr);
	if (a->lock_notifier) hyprland_lock_notifier_v1_destroy(a->lock_notifier);
	if (a->seat) {
		if (wl_seat_get_version(a->seat) >= WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(a->seat);
		else wl_seat_destroy(a->seat);
	}
	if (a->shm) wl_shm_destroy(a->shm);
	if (a->compositor) wl_compositor_destroy(a->compositor);
	if (a->registry) wl_registry_destroy(a->registry);
	if (a->display) {
		wl_display_flush(a->display);
		wl_display_disconnect(a->display);
	}
	a->display = NULL;
}

/* --------------------------------------------------------------- surface */
static void update_scale(struct app *a)
{
	double s;
	if (a->frac120 > 0 && a->viewport) s = a->frac120 / 120.0;
	else if (a->buf_scale > 0) s = a->buf_scale;
	else {
		struct output *o = a->entered ? a->entered : a->layer_output;
		s = (o && o->scale > 0) ? o->scale : 1;
	}
	if (s != a->scale) {
		DBG(a, "render scale %.3f", s);
		a->scale = s;
		app_relayout(a);
	}
}

static void surf_enter(void *data, struct wl_surface *s, struct wl_output *wl)
{
	(void)s;
	struct app *a = data;
	a->entered = output_from_wl(a, wl);
	update_scale(a);
	app_check_orientation(a);
}

static void surf_leave(void *data, struct wl_surface *s, struct wl_output *wl)
{
	(void)s;
	struct app *a = data;
	struct output *o = output_from_wl(a, wl);
	if (a->entered == o) a->entered = NULL;
}

static void surf_buffer_scale(void *data, struct wl_surface *s, int32_t factor)
{
	(void)s;
	struct app *a = data;
	a->buf_scale = factor;
	update_scale(a);
}

static void surf_buffer_transform(void *data, struct wl_surface *s, uint32_t transform)
{
	(void)s;
	struct app *a = data;
	a->transform = (int)(transform & 3);
	DBG(a, "preferred buffer transform %u", transform);
	render_invalidate(a); /* rotate tile highlight */
}

static const struct wl_surface_listener surface_listener = {
	.enter = surf_enter,
	.leave = surf_leave,
	.preferred_buffer_scale = surf_buffer_scale,
	.preferred_buffer_transform = surf_buffer_transform,
};

static void frac_preferred(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale)
{
	(void)f;
	struct app *a = data;
	a->frac120 = (int)scale;
	update_scale(a);
}

static const struct wp_fractional_scale_v1_listener frac_listener = { .preferred_scale = frac_preferred };

static void set_input_region(struct app *a)
{
	if (!a->surface || (!a->collapsed && a->width <= 0)) return;
	struct wl_region *r = wl_compositor_create_region(a->compositor);
	if (a->collapsed) wl_region_add(r, 0, 0, BTN_D, BTN_D);
	else wl_region_add(r, 0, a->band, a->width, a->kb_height);
	wl_surface_set_input_region(a->surface, r);
	wl_region_destroy(r);
}

static uint32_t closed_count;
static uint32_t closed_since;

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
	struct app *a = data;
	zwlr_layer_surface_v1_ack_configure(l, serial);
	if (h == 0) h = (uint32_t)a->req_height;
	DBG(a, "layer configure %ux%u (requested height %d)", w, h, a->req_height);
	if (a->collapsed || (int)w == BTN_D) {
		/* the round button (or a stale configure from it): keep the keyboard width */
		a->configured = true;
		set_input_region(a);
		a->dirty = true;
		surface_maybe_draw(a);
		return;
	}
	bool changed = !a->configured || (int)w != a->width;
	a->configured = true;
	a->width = (int)w;
	set_input_region(a);
	if (changed) app_relayout(a);
	a->dirty = true;
	surface_maybe_draw(a);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *l)
{
	(void)l;
	struct app *a = data;
	LOG(a, "layer surface closed by the compositor");
	app_release_all(a);
	surface_destroy(a);
	uint32_t t = now_ms();
	if (t - closed_since > 5000) {
		closed_since = t;
		closed_count = 0;
	}
	if (++closed_count > 5) {
		WARN(a, "layer surface keeps being closed; hiding");
		a->visible = false;
		return;
	}
	if (a->visible) a->need_recreate = true;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

int surface_create(struct app *a)
{
	if (a->surface) return 0;
	a->surface = wl_compositor_create_surface(a->compositor);
	if (!a->surface) return -1;
	wl_surface_add_listener(a->surface, &surface_listener, a);
	if (a->frac_mgr && a->viewporter) {
		a->viewport = wp_viewporter_get_viewport(a->viewporter, a->surface);
		a->frac = wp_fractional_scale_manager_v1_get_fractional_scale(a->frac_mgr, a->surface);
		wp_fractional_scale_v1_add_listener(a->frac, &frac_listener, a);
	}
	struct output *o = wl_pick_output(a);
	if (!o && strcmp(a->cfg.output, "auto") != 0)
		WARN(a, "output '%s' not found; letting the compositor choose", a->cfg.output);
	a->layer_output = o;
	a->layer = zwlr_layer_shell_v1_get_layer_surface(a->layer_shell, a->surface, o ? o->wl : NULL,
	                                                 ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "slatekbd");
	zwlr_layer_surface_v1_add_listener(a->layer, &layer_listener, a);
	zwlr_layer_surface_v1_set_keyboard_interactivity(a->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	a->configured = false;
	a->frac120 = 0;
	a->buf_scale = 0;
	app_check_orientation(a);
	app_update_size(a); /* computes H/B, pushes set_size + exclusive zone and commits */
	LOG(a, "surface created on %s (height %d + band %d, %s)", o ? o->name : "(compositor choice)", a->kb_height,
	    a->band, app_overlay_effective(a) ? "overlay" : "popup");
	return 0;
}

void surface_destroy(struct app *a)
{
	if (a->frame_cb) wl_callback_destroy(a->frame_cb);
	a->frame_cb = NULL;
	if (a->layer) zwlr_layer_surface_v1_destroy(a->layer);
	a->layer = NULL;
	if (a->viewport) wp_viewport_destroy(a->viewport);
	a->viewport = NULL;
	if (a->frac) wp_fractional_scale_v1_destroy(a->frac);
	a->frac = NULL;
	if (a->surface) wl_surface_destroy(a->surface);
	a->surface = NULL;
	for (int i = 0; i < 2; i++) {
		shm_buffer_destroy(a->buffers[i]);
		a->buffers[i] = NULL;
	}
	a->configured = false;
	a->entered = NULL;
	a->layer_output = NULL;
	for (int i = 0; i < MAX_TOUCH; i++) a->tp[i].used = false;
	/* the matching up will never arrive: end any settings press and its stepper repeat */
	if (a->sv.pressing || timer_armed(a, T_SET_REPEAT)) settings_cancel(a);
	a->ptr_inside = false;
	a->ptr_down = false;
	a->ptr_pend_down = a->ptr_pend_up = a->ptr_pend_motion = false;
}

void surface_apply_size(struct app *a)
{
	if (!a->layer) return;
	if (a->collapsed) {
		zwlr_layer_surface_v1_set_anchor(a->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(a->layer, BTN_D, BTN_D);
		zwlr_layer_surface_v1_set_margin(a->layer, 0, BTN_MARGIN + a->button_offset * BTN_PITCH, BTN_MARGIN, 0);
		zwlr_layer_surface_v1_set_exclusive_zone(a->layer, 0);
	} else {
		zwlr_layer_surface_v1_set_anchor(a->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(a->layer, 0, (uint32_t)a->req_height);
		zwlr_layer_surface_v1_set_margin(a->layer, 0, 0, 0, 0);
		zwlr_layer_surface_v1_set_exclusive_zone(a->layer, app_overlay_effective(a) ? 0 : a->kb_height);
	}
	set_input_region(a);
	wl_surface_commit(a->surface);
	a->dirty = true;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t t)
{
	(void)t;
	struct app *a = data;
	wl_callback_destroy(cb);
	if (a->frame_cb == cb) a->frame_cb = NULL;
}

static const struct wl_callback_listener frame_listener = { .done = frame_done };

void surface_maybe_draw(struct app *a)
{
	if (!a->surface || !a->configured || !a->dirty || a->frame_cb || (!a->collapsed && a->width <= 0)) return;
	int lw = a->collapsed ? BTN_D : a->width;
	int total_h = a->collapsed ? BTN_D : a->kb_height + a->band;
	bool use_vp = a->viewport && a->frac120 > 0;
	double s = a->scale > 0 ? a->scale : 1;
	int iscale = 1;
	if (!use_vp) {
		iscale = (int)lround(s);
		if (iscale < 1) iscale = 1;
		s = iscale;
	}
	int bw = (int)lround(lw * s), bh = (int)lround(total_h * s);
	struct shm_buffer *buf = shm_get_buffer(a->shm, a->buffers, bw, bh);
	if (!buf) return; /* both busy: retry after a release */
	if (a->collapsed) render_button(a, buf, s);
	else render_frame(a, buf, s);
	if (use_vp) {
		wl_surface_set_buffer_scale(a->surface, 1);
		wp_viewport_set_destination(a->viewport, lw, total_h);
	} else {
		if (a->viewport) wp_viewport_set_destination(a->viewport, -1, -1);
		wl_surface_set_buffer_scale(a->surface, iscale);
	}
	wl_surface_attach(a->surface, buf->wl, 0, 0);
	wl_surface_damage_buffer(a->surface, 0, 0, bw, bh);
	a->frame_cb = wl_surface_frame(a->surface);
	wl_callback_add_listener(a->frame_cb, &frame_listener, a);
	wl_surface_commit(a->surface);
	buf->busy = true;
	a->dirty = false;
}
