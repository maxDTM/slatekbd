/* app.c - glue between modules: sizing, layout, visibility, settings apply, timers. */
#include "app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "im.h"
#include "keymap.h"
#include "lock.h"
#include "render.h"
#include "settings.h"
#include "wayland.h"

uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}

void app_log(struct app *a, int level, const char *fmt, ...)
{
	if (level > 0 && (!a || a->verbose < level)) return;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	fprintf(stderr, "slatekbd[%5ld.%03ld]%s ", (long)(ts.tv_sec % 100000), ts.tv_nsec / 1000000L,
	        level == 0 ? " warning:" : "");
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* ------------------------------------------------------------- input ops */
static void op_key(void *ud, uint16_t code, bool down)
{
	struct app *a = ud;
	keymap_key(a->km, code, down);
}

static void op_release_all(void *ud)
{
	struct app *a = ud;
	if (a->km) keymap_release_all(a->km);
	if (a->display) wl_display_flush(a->display);
}

static void op_action(void *ud, const struct key *k)
{
	struct app *a = ud;
	if (k->type == KT_PAGE) {
		app_set_page(a, k->page);
	} else if (k->type == KT_SETTINGS) {
		if (a->locked) {
			/* lock/login screen: the gear key folds the keyboard back into the button */
			if (a->cfg.lock_button) app_set_collapsed(a, true);
			return;
		}
		app_open_settings(a, !a->settings_open);
	}
}

static void op_timer(void *ud, uint32_t ms)
{
	struct app *a = ud;
	if (ms) timer_arm(a, T_REPEAT, ms);
	else timer_cancel(a, T_REPEAT);
}

static void op_changed(void *ud)
{
	app_mark_dirty(ud);
}

static const struct input_ops input_ops = {
	.key = op_key,
	.release_all = op_release_all,
	.action = op_action,
	.timer = op_timer,
	.changed = op_changed,
};

void app_init_input(struct app *a)
{
	input_init(&a->in, &input_ops, a);
	app_sync_input_cfg(a);
}

void app_sync_input_cfg(struct app *a)
{
	a->in.hold_mode = a->cfg.modmode == MODMODE_HOLD;
	a->in.repeat_on = a->cfg.repeat;
	a->in.repeat_delay = a->cfg.repeat_delay;
	a->in.repeat_rate = a->cfg.repeat_rate;
	a->in.shift_caps = a->cfg.shift_caps;
}

/* ------------------------------------------------------------- state helpers */
void app_mark_dirty(struct app *a)
{
	a->dirty = true;
}

bool app_effective_split(const struct app *a)
{
	return a->cfg.split && (!a->portrait || a->cfg.split_portrait);
}

bool app_overlay_effective(const struct app *a)
{
	return a->cfg.mode == MODE_OVERLAY || a->locked;
}

bool app_preview_effective(const struct app *a)
{
	return a->cfg.preview && !a->locked;
}

struct output *app_ref_output(const struct app *a)
{
	if (a->entered) return a->entered;
	if (a->layer_output) return a->layer_output;
	struct output *o = wl_pick_output(a);
	if (o) return o;
	if (!wl_list_empty(&a->outputs)) return wl_container_of(a->outputs.next, o, link);
	return NULL;
}

const char *app_output_name(const struct app *a)
{
	struct output *o = app_ref_output(a);
	if (o && o->name[0]) return o->name;
	if (strcmp(a->cfg.output, "auto") != 0) return a->cfg.output;
	return "eDP-1";
}

void app_check_orientation(struct app *a)
{
	struct output *o = app_ref_output(a);
	bool p = a->portrait;
	if (o && o->lw > 0 && o->lh > 0) p = o->lh > o->lw;
	if (p == a->portrait) return;
	LOG(a, "orientation: %s (%s %dx%d)", p ? "portrait" : "landscape", o ? o->name : "?", o ? o->lw : 0, o ? o->lh : 0);
	a->portrait = p;
	if (!a->surface) a->transform = p ? 1 : 0; /* best guess until preferred_buffer_transform arrives */
	app_update_size(a);
	app_relayout(a);
}

void app_update_size(struct app *a)
{
	int H = a->portrait ? a->cfg.height_port : a->cfg.height_land;
	int B = app_preview_effective(a) ? (int)layout_band((float)H) : 0;
	bool changed = H != a->kb_height || B != a->band;
	a->kb_height = H;
	a->band = B;
	a->req_height = H + B;
	if (a->layer) surface_apply_size(a);
	if (changed) app_relayout(a);
	app_mark_dirty(a);
}

void app_relayout(struct app *a)
{
	if (a->width > 0 && a->kb_height > 0) {
		struct geo_params p = {
			.width = (float)a->width,
			.height = (float)a->kb_height,
			.band = (float)a->band,
			.page = a->page,
			.shape = a->cfg.shape == SHAPE_SQUARE ? GEO_SQUARE : GEO_RECT,
			.split = app_effective_split(a),
			.gap_pct = a->cfg.split_gap,
		};
		if (layout_build(&a->geo, &p) < 0) WARN(a, "layout_build failed (%dx%d)", a->width, a->kb_height);
		settings_layout(a);
	}
	render_invalidate(a);
}

void app_set_page(struct app *a, int page)
{
	if (page < 0 || page >= PAGE_COUNT || page == a->page) return;
	a->page = page;
	DBG(a, "page %s", layout_pages[page].name);
	app_relayout(a);
}

void app_open_settings(struct app *a, bool open)
{
	if (open == a->settings_open) return;
	if (open && a->locked) return;
	app_release_all(a);
	settings_reset_view(a);
	a->settings_open = open;
	if (open) settings_layout(a);
	LOG(a, "settings %s", open ? "opened" : "closed");
	render_invalidate(a);
}

void app_release_all(struct app *a)
{
	input_release_all(&a->in);
}

/* ------------------------------------------------------------- visibility */
void app_show(struct app *a)
{
	timer_cancel(a, T_HIDE);
	if (a->visible) return;
	a->visible = true;
	LOG(a, "show");
	surface_create(a);
}

void app_hide(struct app *a)
{
	timer_cancel(a, T_HIDE);
	if (!a->visible) return;
	LOG(a, "hide");
	app_release_all(a);
	if (a->settings_open) app_open_settings(a, false);
	a->visible = false;
	surface_destroy(a);
	if (a->display) wl_display_flush(a->display);
}

void app_update_visibility(struct app *a)
{
	a->vis.auto_on = a->cfg.auto_show;
	a->vis.lock_mode = a->cfg.lockscreen;
	a->vis.lock_button = a->cfg.lock_button;
	switch (vis_step(&a->vis, a->visible, timer_armed(a, T_HIDE))) {
	case VA_SHOW: app_show(a); break;
	case VA_HIDE: app_hide(a); break;
	case VA_ARM_HIDE: timer_arm(a, T_HIDE, HIDE_DEBOUNCE_MS); break;
	case VA_CANCEL_HIDE: timer_cancel(a, T_HIDE); break;
	default: break;
	}
}

void app_manual(struct app *a, int ov)
{
	vis_manual(&a->vis, ov);
	timer_cancel(a, T_HIDE);
	app_update_visibility(a);
}

void app_toggle(struct app *a)
{
	app_manual(a, a->visible ? OV_HIDE : OV_SHOW);
}

void app_quit(struct app *a)
{
	a->quit = true;
}

void app_set_auto(struct app *a, bool on)
{
	if (on) {
		if (!a->im) im_acquire(a);
	} else {
		im_release(a);
	}
	/* keep the current visibility until the next input-method change */
	if (a->visible) a->vis.manual = OV_SHOW;
	render_invalidate(a);
	app_update_visibility(a);
}

void app_im_done(struct app *a, bool active, bool activated, uint32_t hint, uint32_t purpose)
{
	(void)hint;
	bool was = a->vis.im_active;
	vis_im_done(&a->vis, active, activated);
	if (active && a->cfg.auto_numpad && !a->locked) {
		if (im_purpose_numeric(purpose)) {
			if (!a->settings_open) app_set_page(a, PAGE_SYM);
			a->im_was_numeric = true;
		} else if (a->im_was_numeric) {
			if (a->page == PAGE_SYM) app_set_page(a, PAGE_MAIN);
			a->im_was_numeric = false;
		}
	}
	if (active != was) LOG(a, "input method %s", active ? "activated" : "deactivated");
	app_update_visibility(a);
}

void app_im_unavailable(struct app *a)
{
	a->vis.im_avail = false;
	a->vis.im_active = false;
	render_invalidate(a);
	app_update_visibility(a);
}

void app_set_collapsed(struct app *a, bool collapsed)
{
	if (a->collapsed == collapsed) return;
	app_release_all(a);
	a->collapsed = collapsed;
	a->btn_pressing = false;
	LOG(a, "%s", collapsed ? "collapsed to the keyboard button" : "expanded from the keyboard button");
	render_invalidate(a);
	if (a->layer) surface_apply_size(a);
	app_mark_dirty(a);
}

void app_lock(struct app *a, bool locked)
{
	if (locked) lock_enter(a);
	else lock_exit(a);
}

/* ------------------------------------------------------------- config */
void app_save_now(struct app *a)
{
	timer_cancel(a, T_SAVE);
	if (a->no_save || a->locked || !a->config_path[0]) return;
	if (config_save(&a->cfg, a->config_path) < 0) WARN(a, "could not save %s", a->config_path);
	else LOG(a, "saved %s", a->config_path);
}

void app_schedule_save(struct app *a)
{
	if (a->no_save || a->locked) return;
	timer_arm(a, T_SAVE, 1000);
}

void app_config_changed(struct app *a, enum cfg_key k)
{
	if (k >= CK_COUNT) a->cfg.overridden = 0;
	else a->cfg.overridden &= ~(1ULL << k);
	config_clamp(&a->cfg);
	switch (k) {
	case CK_MODE:
	case CK_HEIGHT_LAND:
	case CK_HEIGHT_PORT:
	case CK_PREVIEW:
		app_update_size(a);
		break;
	case CK_KEY_SHAPE:
	case CK_SPLIT:
	case CK_SPLIT_GAP:
	case CK_SPLIT_PORTRAIT:
		app_relayout(a);
		break;
	case CK_MODIFIERS:
		app_release_all(a);
		app_sync_input_cfg(a);
		break;
	case CK_REPEAT:
	case CK_REPEAT_DELAY:
	case CK_REPEAT_RATE:
	case CK_SHIFT_CAPS:
		app_sync_input_cfg(a);
		break;
	case CK_AUTO:
		app_set_auto(a, a->cfg.auto_show);
		break;
	case CK_OUTPUT:
		if (a->visible) a->need_recreate = true;
		break;
	case CK_LOCKSCREEN:
		app_update_visibility(a);
		break;
	case CK_COUNT:
		app_release_all(a);
		app_sync_input_cfg(a);
		if (!!a->im != a->cfg.auto_show) app_set_auto(a, a->cfg.auto_show);
		app_update_size(a);
		app_relayout(a);
		if (a->visible && wl_pick_output(a) != a->layer_output) a->need_recreate = true;
		break;
	default:
		break;
	}
	render_invalidate(a);
	app_schedule_save(a);
}

/* ------------------------------------------------------------- timers */
void app_on_timer(struct app *a, enum timer_id id)
{
	switch (id) {
	case T_REPEAT:
		input_repeat_fire(&a->in);
		break;
	case T_HIDE:
		if (!vis_want(&a->vis, a->visible)) app_hide(a);
		break;
	case T_CONFIRM:
		settings_confirm_timeout(a);
		break;
	case T_SAVE:
		app_save_now(a);
		break;
	case T_SET_REPEAT:
		settings_repeat_fire(a);
		break;
	case T_LOCK_RECREATE:
		lock_rule_timeout(a);
		break;
	default:
		break;
	}
}
