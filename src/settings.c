/* settings.c - the settings view drawn inside the keyboard surface: paginated tiles,
 * a navigation row, hit-testing, stepper auto-repeat, 2-step confirmation, live apply. */
#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "render.h"
#include "rotate.h"
#include "wayland.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int tile_kind(int tile)
{
	switch (tile) {
	case TL_FONT_SCALE: case TL_RADIUS: case TL_SPLIT_GAP: case TL_HEIGHT_LAND: case TL_HEIGHT_PORT:
	case TL_REPEAT_DELAY: case TL_REPEAT_RATE:
		return TK_STEPPER;
	case TL_AUTO: case TL_PREVIEW: case TL_SPLIT: case TL_REPEAT: case TL_SHIFT_CAPS: case TL_NUMPAD:
		return TK_TOGGLE;
	case TL_ROT0: case TL_ROT1: case TL_ROT2: case TL_ROT3: case TL_RESET:
		return TK_ACTION;
	default:
		return TK_CYCLE;
	}
}

float settings_nav_h(const struct app *a)
{
	return roundf(a->kb_height / 5.0f);
}

void settings_reset_view(struct app *a)
{
	struct settings_view *sv = &a->sv;
	sv->pressing = false;
	sv->confirm_tile = -1;
	sv->status[0] = 0;
	timer_cancel(a, T_SET_REPEAT);
	timer_cancel(a, T_CONFIRM);
}

void settings_layout(struct app *a)
{
	struct settings_view *sv = &a->sv;
	float content = a->kb_height - settings_nav_h(a);
	sv->rows = clampi((int)floorf(content / 48.0f), 2, 6);
	sv->cols = clampi((int)floorf(a->width / 200.0f), 2, 8);
	int per = sv->rows * sv->cols;
	sv->npages = (TL_COUNT + per - 1) / per;
	if (sv->page >= sv->npages) sv->page = sv->npages - 1;
	if (sv->page < 0) sv->page = 0;
}

struct rectf settings_tile_rect(const struct app *a, int i)
{
	const struct settings_view *sv = &a->sv;
	float content = a->kb_height - settings_nav_h(a);
	float w = (float)a->width / (sv->cols > 0 ? sv->cols : 1), h = content / (sv->rows > 0 ? sv->rows : 1);
	int c = sv->cols > 0 ? i % sv->cols : 0, r = sv->cols > 0 ? i / sv->cols : 0;
	return (struct rectf){ c * w, a->band + r * h, w, h };
}

struct rectf settings_nav_rect(const struct app *a, int nav)
{
	float u = a->width / 12.0f, nh = settings_nav_h(a);
	float y = a->band + a->kb_height - nh;
	switch (nav) {
	case NAV_PREV: return (struct rectf){ 0, y, 1.5f * u, nh };
	case NAV_PAGE: return (struct rectf){ 1.5f * u, y, 2.5f * u, nh };
	case NAV_NEXT: return (struct rectf){ 4 * u, y, 1.5f * u, nh };
	case NAV_HIDE: return (struct rectf){ 6.5f * u, y, 1.75f * u, nh };
	case NAV_QUIT: return (struct rectf){ 8.25f * u, y, 1.75f * u, nh };
	case NAV_CLOSE: return (struct rectf){ 10 * u, y, 2 * u, nh };
	default: return (struct rectf){ 0, 0, 0, 0 };
	}
}

static const char *const rot_names[4] = { "Landscape", "Portrait", "Landscape flipped", "Portrait flipped" };

void settings_tile_info(const struct app *a, int tile, struct tile_info *ti)
{
	const struct config *c = &a->cfg;
	memset(ti, 0, sizeof(*ti));
	ti->kind = tile_kind(tile);
	ti->confirming = (a->sv.confirm_tile == tile);
	switch (tile) {
	case TL_MODE:
		ti->caption = "Mode";
		snprintf(ti->value, sizeof ti->value, "%s", c->mode == MODE_POPUP ? "Popup" : "Overlay");
		break;
	case TL_AUTO:
		ti->caption = "Auto show/hide";
		if (!a->im_mgr) {
			snprintf(ti->value, sizeof ti->value, "Unsupported");
			ti->disabled = true;
		} else if (c->auto_show && !a->vis.im_avail) {
			snprintf(ti->value, sizeof ti->value, "Unavailable");
			ti->disabled = true; /* greyed, but a tap retries */
		} else {
			snprintf(ti->value, sizeof ti->value, "%s", c->auto_show ? "On" : "Off");
			ti->active = c->auto_show;
		}
		break;
	case TL_FONT_SCALE:
		ti->caption = "Text size";
		snprintf(ti->value, sizeof ti->value, "%.1f×", c->font_scale);
		break;
	case TL_SHAPE:
		ti->caption = "Key shape";
		snprintf(ti->value, sizeof ti->value, "%s", c->shape == SHAPE_RECT ? "Rectangle" : "Square");
		break;
	case TL_RADIUS:
		ti->caption = "Corner radius";
		snprintf(ti->value, sizeof ti->value, "%d", c->radius);
		break;
	case TL_PREVIEW:
		ti->caption = "Key popup";
		snprintf(ti->value, sizeof ti->value, "%s", c->preview ? "On" : "Off");
		ti->active = c->preview;
		break;
	case TL_MODIFIERS:
		ti->caption = "Modifiers";
		snprintf(ti->value, sizeof ti->value, "%s", c->modmode == MODMODE_TOGGLE ? "Toggle" : "Hold");
		break;
	case TL_SPLIT:
		ti->caption = a->portrait && !c->split_portrait ? "Split (landscape)" : "Split keyboard";
		snprintf(ti->value, sizeof ti->value, "%s", c->split ? "On" : "Off");
		ti->active = c->split;
		break;
	case TL_SPLIT_GAP:
		ti->caption = "Split gap";
		snprintf(ti->value, sizeof ti->value, "%d%%", c->split_gap);
		ti->disabled = !c->split;
		break;
	case TL_HEIGHT_LAND:
		ti->caption = "Height landscape";
		snprintf(ti->value, sizeof ti->value, "%d", c->height_land);
		ti->active = !a->portrait;
		break;
	case TL_HEIGHT_PORT:
		ti->caption = "Height portrait";
		snprintf(ti->value, sizeof ti->value, "%d", c->height_port);
		ti->active = a->portrait;
		break;
	case TL_THEME:
		ti->caption = "Theme";
		snprintf(ti->value, sizeof ti->value, "%s", c->theme == THEME_DARK ? "Dark" : "Light");
		break;
	case TL_REPEAT:
		ti->caption = "Key repeat";
		snprintf(ti->value, sizeof ti->value, "%s", c->repeat ? "On" : "Off");
		ti->active = c->repeat;
		break;
	case TL_REPEAT_DELAY:
		ti->caption = "Repeat delay";
		snprintf(ti->value, sizeof ti->value, "%d ms", c->repeat_delay);
		ti->disabled = !c->repeat;
		break;
	case TL_REPEAT_RATE:
		ti->caption = "Repeat rate";
		snprintf(ti->value, sizeof ti->value, "%d/s", c->repeat_rate);
		ti->disabled = !c->repeat;
		break;
	case TL_SHIFT_CAPS:
		ti->caption = "Shift ×2 = Caps";
		snprintf(ti->value, sizeof ti->value, "%s", c->shift_caps ? "On" : "Off");
		ti->active = c->shift_caps;
		break;
	case TL_NUMPAD:
		ti->caption = "Numbers for numeric fields";
		snprintf(ti->value, sizeof ti->value, "%s", c->auto_numpad ? "On" : "Off");
		ti->active = c->auto_numpad;
		break;
	case TL_LOCKSCREEN:
		ti->caption = "Lockscreen";
		snprintf(ti->value, sizeof ti->value, "%s",
		         c->lockscreen == LOCK_AUTO ? "Auto" : c->lockscreen == LOCK_ALWAYS ? "Always" : "Off");
		break;
	case TL_OUTPUT:
		ti->caption = "Output";
		snprintf(ti->value, sizeof ti->value, "%s", c->output);
		break;
	case TL_ROT0: case TL_ROT1: case TL_ROT2: case TL_ROT3: {
		int t = tile - TL_ROT0;
		static const char *const caps[4] = { "Rotate ↑", "Rotate →", "Rotate ↓", "Rotate ←" };
		ti->caption = caps[t];
		snprintf(ti->value, sizeof ti->value, "%s", rot_names[t]);
		ti->active = (a->transform == t);
		ti->disabled = !rotate_available(a) || a->locked;
		break;
	}
	case TL_RESET:
		ti->caption = "Settings";
		snprintf(ti->value, sizeof ti->value, "%s", ti->confirming ? "Tap again" : "Reset defaults");
		break;
	default:
		ti->caption = "";
		break;
	}
}

void settings_nav_info(const struct app *a, int nav, struct tile_info *ti)
{
	memset(ti, 0, sizeof(*ti));
	ti->kind = TK_ACTION;
	ti->caption = "";
	ti->confirming = (a->sv.confirm_tile == nav);
	switch (nav) {
	case NAV_PREV:
		snprintf(ti->value, sizeof ti->value, "◀");
		ti->disabled = a->sv.npages <= 1;
		break;
	case NAV_NEXT:
		snprintf(ti->value, sizeof ti->value, "▶");
		ti->disabled = a->sv.npages <= 1;
		break;
	case NAV_PAGE:
		snprintf(ti->value, sizeof ti->value, "Settings %d/%d", a->sv.page + 1, a->sv.npages);
		break;
	case NAV_HIDE:
		snprintf(ti->value, sizeof ti->value, "Hide");
		break;
	case NAV_QUIT:
		snprintf(ti->value, sizeof ti->value, "%s", ti->confirming ? "Tap again" : "Quit");
		ti->disabled = a->locked;
		break;
	case NAV_CLOSE:
		snprintf(ti->value, sizeof ti->value, "Keyboard");
		break;
	}
}

static bool in_rect(struct rectf r, float x, float y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int settings_hit(const struct app *a, float x, float y, int *zone)
{
	*zone = 0;
	for (int n = NAV_FIRST; n < NAV_COUNT_END; n++)
		if (in_rect(settings_nav_rect(a, n), x, y)) return n;
	int per = a->sv.rows * a->sv.cols;
	for (int i = 0; i < per; i++) {
		int tile = a->sv.page * per + i;
		if (tile >= TL_COUNT) break;
		struct rectf r = settings_tile_rect(a, i);
		if (!in_rect(r, x, y)) continue;
		if (tile_kind(tile) == TK_STEPPER) {
			float fx = (x - r.x) / r.w;
			*zone = fx < 0.3f ? -1 : fx > 0.7f ? 1 : 0;
		}
		return tile;
	}
	return -1;
}

/* ------------------------------------------------------------- apply */
static void stepper(struct app *a, int tile, int dir)
{
	struct config *c = &a->cfg;
	enum cfg_key k;
	switch (tile) {
	case TL_FONT_SCALE:
		c->font_scale = round((c->font_scale + dir * 0.1) * 10) / 10.0;
		k = CK_FONT_SCALE;
		break;
	case TL_RADIUS: c->radius += dir * 2; k = CK_KEY_RADIUS; break;
	case TL_SPLIT_GAP: c->split_gap += dir * 5; k = CK_SPLIT_GAP; break;
	case TL_HEIGHT_LAND: c->height_land += dir * 10; k = CK_HEIGHT_LAND; break;
	case TL_HEIGHT_PORT: c->height_port += dir * 10; k = CK_HEIGHT_PORT; break;
	case TL_REPEAT_DELAY: c->repeat_delay += dir * 50; k = CK_REPEAT_DELAY; break;
	case TL_REPEAT_RATE: c->repeat_rate += dir * 5; k = CK_REPEAT_RATE; break;
	default: return;
	}
	config_clamp(c);
	app_config_changed(a, k);
}

static void cycle_output(struct app *a)
{
	struct output *o, *next = NULL;
	bool found = !strcmp(a->cfg.output, "auto");
	bool take = found;
	wl_list_for_each(o, &a->outputs, link) {
		if (!o->name[0]) continue;
		if (take) {
			next = o;
			break;
		}
		if (!strcmp(o->name, a->cfg.output)) take = true;
	}
	snprintf(a->cfg.output, sizeof a->cfg.output, "%s", next ? next->name : "auto");
	app_config_changed(a, CK_OUTPUT);
}

static void fire(struct app *a, int tile)
{
	struct config *c = &a->cfg;
	struct tile_info ti;
	if (tile >= NAV_FIRST) settings_nav_info(a, tile, &ti);
	else settings_tile_info(a, tile, &ti);

	/* two-step confirmation */
	if (tile == TL_RESET || tile == NAV_QUIT) {
		if (ti.disabled) return;
		if (a->sv.confirm_tile != tile) {
			a->sv.confirm_tile = tile;
			timer_arm(a, T_CONFIRM, 3000);
			return;
		}
		a->sv.confirm_tile = -1;
		timer_cancel(a, T_CONFIRM);
		if (tile == NAV_QUIT) {
			LOG(a, "quit requested from settings");
			app_quit(a);
			return;
		}
		LOG(a, "settings reset to defaults");
		config_free(c);
		config_defaults(c);
		app_config_changed(a, CK_COUNT);
		return;
	}
	if (a->sv.confirm_tile >= 0) {
		a->sv.confirm_tile = -1;
		timer_cancel(a, T_CONFIRM);
	}
	a->sv.status[0] = 0;

	switch (tile) {
	case NAV_PREV:
		if (a->sv.npages > 1) a->sv.page = (a->sv.page + a->sv.npages - 1) % a->sv.npages;
		return;
	case NAV_NEXT:
		if (a->sv.npages > 1) a->sv.page = (a->sv.page + 1) % a->sv.npages;
		return;
	case NAV_PAGE:
		return;
	case NAV_HIDE:
		app_open_settings(a, false);
		app_manual(a, OV_HIDE);
		return;
	case NAV_CLOSE:
		app_open_settings(a, false);
		return;
	}
	if (tile == TL_AUTO) {
		/* always tappable: retries the input method when unavailable */
		if (!a->im_mgr) return;
		if (c->auto_show && !a->vis.im_avail) {
			app_set_auto(a, true);
			return;
		}
		c->auto_show = !c->auto_show;
		app_config_changed(a, CK_AUTO);
		return;
	}
	if (ti.disabled) return;
	switch (tile) {
	case TL_MODE: c->mode = c->mode == MODE_POPUP ? MODE_OVERLAY : MODE_POPUP; app_config_changed(a, CK_MODE); break;
	case TL_SHAPE: c->shape = c->shape == SHAPE_RECT ? SHAPE_SQUARE : SHAPE_RECT; app_config_changed(a, CK_KEY_SHAPE); break;
	case TL_PREVIEW: c->preview = !c->preview; app_config_changed(a, CK_PREVIEW); break;
	case TL_MODIFIERS:
		c->modmode = c->modmode == MODMODE_TOGGLE ? MODMODE_HOLD : MODMODE_TOGGLE;
		app_config_changed(a, CK_MODIFIERS);
		break;
	case TL_SPLIT: c->split = !c->split; app_config_changed(a, CK_SPLIT); break;
	case TL_THEME: c->theme = c->theme == THEME_DARK ? THEME_LIGHT : THEME_DARK; app_config_changed(a, CK_THEME); break;
	case TL_REPEAT: c->repeat = !c->repeat; app_config_changed(a, CK_REPEAT); break;
	case TL_SHIFT_CAPS: c->shift_caps = !c->shift_caps; app_config_changed(a, CK_SHIFT_CAPS); break;
	case TL_NUMPAD: c->auto_numpad = !c->auto_numpad; app_config_changed(a, CK_AUTO_NUMPAD); break;
	case TL_LOCKSCREEN: c->lockscreen = (c->lockscreen + 1) % 3; app_config_changed(a, CK_LOCKSCREEN); break;
	case TL_OUTPUT: cycle_output(a); break;
	case TL_ROT0: case TL_ROT1: case TL_ROT2: case TL_ROT3:
		if (!rotate_apply(a, tile - TL_ROT0)) snprintf(a->sv.status, sizeof a->sv.status, "Rotate unavailable");
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------- touches */
void settings_down(struct app *a, int32_t id, bool ptr, float x, float y)
{
	struct settings_view *sv = &a->sv;
	if (sv->pressing) return;
	int zone, hit = settings_hit(a, x, y, &zone);
	if (hit < 0) return;
	sv->pressing = true;
	sv->press_ptr = ptr;
	sv->press_id = id;
	sv->press_tile = hit;
	sv->press_zone = zone;
	sv->press_inside = true;
	sv->px = x;
	sv->py = y;
	if (hit < NAV_FIRST && tile_kind(hit) == TK_STEPPER && zone != 0) {
		struct tile_info ti;
		settings_tile_info(a, hit, &ti);
		if (!ti.disabled) {
			stepper(a, hit, zone);
			timer_arm(a, T_SET_REPEAT, (uint32_t)a->cfg.repeat_delay);
		}
	}
	app_mark_dirty(a);
}

static bool is_press(const struct settings_view *sv, int32_t id, bool ptr)
{
	return sv->pressing && sv->press_ptr == ptr && (ptr || sv->press_id == id);
}

void settings_motion(struct app *a, int32_t id, bool ptr, float x, float y)
{
	struct settings_view *sv = &a->sv;
	if (!is_press(sv, id, ptr)) return;
	int zone, hit = settings_hit(a, x, y, &zone);
	bool inside = hit == sv->press_tile && zone == sv->press_zone;
	if (inside != sv->press_inside) {
		sv->press_inside = inside;
		if (!inside) timer_cancel(a, T_SET_REPEAT);
		app_mark_dirty(a);
	}
}

void settings_up(struct app *a, int32_t id, bool ptr)
{
	struct settings_view *sv = &a->sv;
	if (!is_press(sv, id, ptr)) return;
	sv->pressing = false;
	timer_cancel(a, T_SET_REPEAT);
	int tile = sv->press_tile;
	bool stepper_zone = tile < NAV_FIRST && tile_kind(tile) == TK_STEPPER;
	if (sv->press_inside && !stepper_zone) fire(a, tile);
	if (a->settings_open) render_invalidate(a);
	app_mark_dirty(a);
}

void settings_cancel(struct app *a)
{
	a->sv.pressing = false;
	timer_cancel(a, T_SET_REPEAT);
	app_mark_dirty(a);
}

void settings_repeat_fire(struct app *a)
{
	struct settings_view *sv = &a->sv;
	if (!sv->pressing || !sv->press_inside || sv->press_zone == 0 || sv->press_tile >= NAV_FIRST) return;
	struct tile_info ti;
	settings_tile_info(a, sv->press_tile, &ti);
	if (ti.disabled) return;
	stepper(a, sv->press_tile, sv->press_zone);
	int rate = a->cfg.repeat_rate > 12 ? 12 : a->cfg.repeat_rate;
	timer_arm(a, T_SET_REPEAT, (uint32_t)(1000 / (rate > 0 ? rate : 10)));
}

void settings_confirm_timeout(struct app *a)
{
	if (a->sv.confirm_tile >= 0) {
		a->sv.confirm_tile = -1;
		render_invalidate(a);
	}
}
