/* render.c - cached base layer (idle keys or settings tiles) plus per-frame overlays
 * (pressed keys, modifier states, preview bubbles). All drawing is in logical px. */
#include "render.h"

#include <cairo.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "settings.h"
#include "shm.h"

struct rgba { double r, g, b, a; };

struct theme_colors {
	struct rgba bg, key, mod, pressed, text, accent, bubble, dim_text, on_accent;
};

static const struct theme_colors themes[2] = {
	[THEME_DARK] = {
		{ 0x1b / 255.0, 0x1d / 255.0, 0x22 / 255.0, 1 }, { 0x2c / 255.0, 0x30 / 255.0, 0x38 / 255.0, 1 },
		{ 0x23 / 255.0, 0x26 / 255.0, 0x2c / 255.0, 1 }, { 0x4a / 255.0, 0x52 / 255.0, 0x63 / 255.0, 1 },
		{ 0xe8 / 255.0, 0xea / 255.0, 0xed / 255.0, 1 }, { 0x5b / 255.0, 0x9d / 255.0, 0xff / 255.0, 1 },
		{ 0x3a / 255.0, 0x41 / 255.0, 0x50 / 255.0, 1 }, { 0x9a / 255.0, 0xa0 / 255.0, 0xa8 / 255.0, 1 },
		{ 0x0b / 255.0, 0x0d / 255.0, 0x12 / 255.0, 1 },
	},
	[THEME_LIGHT] = {
		{ 0xd5 / 255.0, 0xd8 / 255.0, 0xde / 255.0, 1 }, { 0xfb / 255.0, 0xfb / 255.0, 0xfc / 255.0, 1 },
		{ 0xbc / 255.0, 0xc1 / 255.0, 0xca / 255.0, 1 }, { 0x9a / 255.0, 0xa3 / 255.0, 0xb2 / 255.0, 1 },
		{ 0x15 / 255.0, 0x17 / 255.0, 0x1a / 255.0, 1 }, { 0x1d / 255.0, 0x64 / 255.0, 0xd8 / 255.0, 1 },
		{ 0xff / 255.0, 0xff / 255.0, 0xff / 255.0, 1 }, { 0x55 / 255.0, 0x5b / 255.0, 0x66 / 255.0, 1 },
		{ 0xff / 255.0, 0xff / 255.0, 0xff / 255.0, 1 },
	},
};

struct base_key {
	int page;
	bool shift, caps, settings, locked;
	int theme, w, h;
	double scale;
	unsigned gen;
};

struct render {
	cairo_surface_t *base;
	struct base_key key;
	bool valid;
	unsigned gen;
	PangoFontDescription *font;
	char font_family[64];
};

struct render *render_create(void)
{
	return calloc(1, sizeof(struct render));
}

void render_destroy(struct render *r)
{
	if (!r) return;
	if (r->base) cairo_surface_destroy(r->base);
	if (r->font) pango_font_description_free(r->font);
	free(r);
}

void render_invalidate(struct app *a)
{
	if (a->render) a->render->gen++;
	app_mark_dirty(a);
}

static void set_rgba(cairo_t *cr, struct rgba c, double alpha)
{
	cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r <= 0.5) {
		cairo_rectangle(cr, x, y, w, h);
		return;
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

/* --------------------------------------------------------------- text */
static PangoFontDescription *font_for(struct app *a)
{
	struct render *r = a->render;
	if (!r->font || strcmp(r->font_family, a->cfg.font) != 0) {
		if (r->font) pango_font_description_free(r->font);
		r->font = pango_font_description_from_string(a->cfg.font);
		pango_font_description_set_weight(r->font, PANGO_WEIGHT_NORMAL);
		snprintf(r->font_family, sizeof r->font_family, "%s", a->cfg.font);
	}
	return r->font;
}

/* Draw text centred at (cx, cy), shrinking to fit maxw. */
static void draw_text(struct app *a, cairo_t *cr, PangoLayout *lay, const char *text, double cx, double cy,
                      double size, double maxw, bool bold)
{
	if (!text || !*text || size < 1) return;
	PangoFontDescription *fd = pango_font_description_copy(font_for(a));
	if (bold) pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(lay, fd);
	pango_layout_set_text(lay, text, -1);
	PangoRectangle ink, log;
	pango_layout_get_pixel_extents(lay, &ink, &log);
	if (maxw > 0 && log.width > maxw) {
		double ns = size * maxw / log.width;
		pango_font_description_set_absolute_size(fd, ns * PANGO_SCALE);
		pango_layout_set_font_description(lay, fd);
		pango_layout_get_pixel_extents(lay, &ink, &log);
	}
	pango_font_description_free(fd);
	cairo_move_to(cr, cx - log.width / 2.0 - log.x, cy - log.height / 2.0 - log.y);
	pango_cairo_show_layout(cr, lay);
}

/* --------------------------------------------------------------- icons */
static void icon_path_arrow(cairo_t *cr, double cx, double cy, double s, double angle)
{
	/* arrow pointing right, then rotated */
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, angle);
	double h = s / 2;
	cairo_move_to(cr, -h, 0);
	cairo_line_to(cr, h, 0);
	cairo_move_to(cr, h * 0.15, -h * 0.65);
	cairo_line_to(cr, h, 0);
	cairo_line_to(cr, h * 0.15, h * 0.65);
	cairo_restore(cr);
}

static void draw_icon(cairo_t *cr, int icon, double cx, double cy, double s, struct rgba col, bool filled)
{
	cairo_save(cr);
	set_rgba(cr, col, 1);
	cairo_set_line_width(cr, fmax(1.5, s * 0.09));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	double h = s / 2;
	switch (icon) {
	case IC_GEAR: {
		int teeth = 8;
		double ro = h * 0.95, ri = h * 0.72;
		cairo_new_path(cr);
		for (int i = 0; i < teeth * 2; i++) {
			double a0 = (i * M_PI) / teeth - M_PI / (teeth * 2.2);
			double a1 = (i * M_PI) / teeth + M_PI / (teeth * 2.2);
			double r = (i % 2 == 0) ? ro : ri;
			if (i == 0) cairo_move_to(cr, cx + r * cos(a0), cy + r * sin(a0));
			else cairo_line_to(cr, cx + r * cos(a0), cy + r * sin(a0));
			cairo_line_to(cr, cx + r * cos(a1), cy + r * sin(a1));
		}
		cairo_close_path(cr);
		cairo_new_sub_path(cr);
		cairo_arc_negative(cr, cx, cy, h * 0.32, 2 * M_PI, 0);
		cairo_close_path(cr);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
		cairo_fill(cr);
		break;
	}
	case IC_BKSP: {
		double w = s * 1.1, hh = s * 0.36;
		double x0 = cx - w / 2, x1 = cx + w / 2;
		cairo_move_to(cr, x0, cy);
		cairo_line_to(cr, x0 + hh, cy - hh);
		cairo_line_to(cr, x1, cy - hh);
		cairo_line_to(cr, x1, cy + hh);
		cairo_line_to(cr, x0 + hh, cy + hh);
		cairo_close_path(cr);
		cairo_stroke(cr);
		double xc = (x0 + hh + x1) / 2, d = hh * 0.45;
		cairo_move_to(cr, xc - d, cy - d);
		cairo_line_to(cr, xc + d, cy + d);
		cairo_move_to(cr, xc + d, cy - d);
		cairo_line_to(cr, xc - d, cy + d);
		cairo_stroke(cr);
		break;
	}
	case IC_ENTER: {
		double x1 = cx + h * 0.8, y0 = cy - h * 0.6, y1 = cy + h * 0.25, x0 = cx - h * 0.8;
		cairo_move_to(cr, x1, y0);
		cairo_line_to(cr, x1, y1);
		cairo_line_to(cr, x0, y1);
		cairo_move_to(cr, x0 + h * 0.4, y1 - h * 0.4);
		cairo_line_to(cr, x0, y1);
		cairo_line_to(cr, x0 + h * 0.4, y1 + h * 0.4);
		cairo_stroke(cr);
		break;
	}
	case IC_SHIFT:
	case IC_CAPS: {
		double t = cy - h * 0.85, m = cy - h * 0.05, b = cy + h * 0.55;
		cairo_move_to(cr, cx, t);
		cairo_line_to(cr, cx + h * 0.8, m);
		cairo_line_to(cr, cx + h * 0.38, m);
		cairo_line_to(cr, cx + h * 0.38, b);
		cairo_line_to(cr, cx - h * 0.38, b);
		cairo_line_to(cr, cx - h * 0.38, m);
		cairo_line_to(cr, cx - h * 0.8, m);
		cairo_close_path(cr);
		if (filled) cairo_fill_preserve(cr);
		cairo_stroke(cr);
		if (icon == IC_CAPS) {
			cairo_rectangle(cr, cx - h * 0.38, b + h * 0.18, h * 0.76, h * 0.14);
			cairo_fill(cr);
		}
		break;
	}
	case IC_TAB: {
		icon_path_arrow(cr, cx - h * 0.25, cy, s * 0.75, 0);
		cairo_stroke(cr);
		cairo_move_to(cr, cx + h * 0.4, cy - h * 0.5);
		cairo_line_to(cr, cx + h * 0.4, cy + h * 0.5);
		cairo_stroke(cr);
		break;
	}
	case IC_LEFT: icon_path_arrow(cr, cx, cy, s, M_PI); cairo_stroke(cr); break;
	case IC_RIGHT: icon_path_arrow(cr, cx, cy, s, 0); cairo_stroke(cr); break;
	case IC_UP: icon_path_arrow(cr, cx, cy, s, -M_PI / 2); cairo_stroke(cr); break;
	case IC_DOWN: icon_path_arrow(cr, cx, cy, s, M_PI / 2); cairo_stroke(cr); break;
	case IC_KEYBOARD: {
		double w = s * 1.2, hh = s * 0.75;
		rounded_rect(cr, cx - w / 2, cy - hh / 2, w, hh, s * 0.1);
		cairo_stroke(cr);
		double dx = w / 6, dy = hh / 4;
		for (int r = 0; r < 2; r++)
			for (int c = 0; c < 5; c++) {
				cairo_rectangle(cr, cx - w / 2 + dx * (c + 0.75), cy - hh / 2 + dy * (r + 0.75), dx * 0.45, dy * 0.45);
			}
		cairo_fill(cr);
		cairo_move_to(cr, cx - w * 0.25, cy + hh * 0.25);
		cairo_line_to(cr, cx + w * 0.25, cy + hh * 0.25);
		cairo_stroke(cr);
		break;
	}
	case IC_LOCK: {
		double w = s * 0.7, hh = s * 0.5;
		rounded_rect(cr, cx - w / 2, cy - hh * 0.1, w, hh, s * 0.06);
		cairo_fill(cr);
		cairo_arc(cr, cx, cy - hh * 0.1, w * 0.32, M_PI, 2 * M_PI);
		cairo_stroke(cr);
		break;
	}
	default:
		break;
	}
	cairo_restore(cr);
}

/* --------------------------------------------------------------- keys */
enum kstate { KS_IDLE, KS_PRESSED, KS_LATCHED, KS_LOCKED };

static bool is_dim_key(const struct key *k)
{
	return (k->flags & KF_DIM) || k->type == KT_MOD || k->type == KT_PAGE || k->type == KT_SETTINGS || k->type == KT_CAPS;
}

static const char *key_label(const struct app *a, const struct key *k)
{
	bool shift = input_shift_active(&a->in);
	if (k->flags & KF_LETTER) return (shift != a->in.caps) && k->shift_label ? k->shift_label : k->label;
	if (shift && k->shift_label) return k->shift_label;
	return k->label;
}

static double base_font(const struct app *a)
{
	double rh = a->geo.row_h;
	double u = a->geo.unit;
	double s = fmin(rh * 0.38, u * 0.55);
	return s * a->cfg.font_scale;
}

static void draw_key(struct app *a, cairo_t *cr, PangoLayout *lay, const struct keybox *b, enum kstate st)
{
	const struct theme_colors *t = &themes[a->cfg.theme];
	const struct key *k = b->key;
	double pad = a->geo.pad;
	double x = b->x + pad / 2, y = b->y + pad / 2, w = b->w - pad, h = b->h - pad;
	if (w <= 1 || h <= 1) return;
	double rad = a->cfg.radius;
	struct rgba fill = is_dim_key(k) ? t->mod : t->key;
	struct rgba fg = t->text;
	if (st == KS_PRESSED) fill = t->pressed;
	if (st == KS_LOCKED) {
		fill = t->accent;
		fg = t->on_accent;
	}
	rounded_rect(cr, x, y, w, h, rad);
	set_rgba(cr, fill, 1);
	cairo_fill(cr);
	if (st == KS_LATCHED) {
		rounded_rect(cr, x + 1, y + 1, w - 2, h - 2, rad);
		set_rgba(cr, t->accent, 1);
		cairo_set_line_width(cr, fmax(2.0, a->geo.row_h * 0.045));
		cairo_stroke(cr);
	}
	double cx = x + w / 2, cy = y + h / 2;
	double fs = base_font(a);
	if (k->flags & KF_SMALL) fs *= 0.75;
	bool gear_locked = k->type == KT_SETTINGS && a->locked;
	if (k->icon != IC_NONE) {
		double is = fmin(h * 0.42, w * 0.5) * fmin(a->cfg.font_scale, 1.3);
		int icon = k->icon;
		if (k->type == KT_SETTINGS && a->settings_open) icon = IC_KEYBOARD;
		if (gear_locked && a->cfg.lock_button) {
			/* folds the keyboard back into the lock-screen button */
			draw_icon(cr, IC_KEYBOARD, cx, cy - is * 0.15, is * 0.8, fg, false);
			icon_path_arrow(cr, cx, cy + is * 0.5, is * 0.35, M_PI / 2);
			set_rgba(cr, fg, 1);
			cairo_set_line_width(cr, fmax(1.5, is * 0.08));
			cairo_stroke(cr);
		} else if (gear_locked) {
			draw_icon(cr, IC_GEAR, cx, cy, is, (struct rgba){ fg.r, fg.g, fg.b, 0.25 }, false);
			draw_icon(cr, IC_LOCK, cx + is * 0.45, cy + is * 0.35, is * 0.55, fg, false);
		} else {
			bool filled = (k->icon == IC_SHIFT && (st == KS_LOCKED || st == KS_LATCHED));
			draw_icon(cr, icon, cx, cy, is, fg, filled);
		}
		/* Caps indicator: a dot on the Shift keys */
		if (k->type == KT_MOD && k->mod == MOD_SHIFT && a->in.caps) {
			cairo_arc(cr, x + w - h * 0.16, y + h * 0.16, fmax(2.5, h * 0.06), 0, 2 * M_PI);
			set_rgba(cr, t->accent, 1);
			cairo_fill(cr);
		}
		return;
	}
	const char *label = key_label(a, k);
	set_rgba(cr, fg, 1);
	draw_text(a, cr, lay, label, cx, cy, fs, w - 2 * pad, false);
	/* shifted-symbol hint in the corner of number/punctuation keys */
	if (k->type == KT_CHAR && !(k->flags & (KF_LETTER | KF_SHIFTED)) && k->shift_label && !input_shift_active(&a->in)) {
		set_rgba(cr, st == KS_LOCKED ? fg : t->dim_text, 1);
		draw_text(a, cr, lay, k->shift_label, x + w - fs * 0.45, y + fs * 0.45, fs * 0.5, w / 2, false);
	}
	if (k->type == KT_CAPS && a->in.caps) {
		cairo_arc(cr, x + w - h * 0.16, y + h * 0.16, fmax(2.5, h * 0.06), 0, 2 * M_PI);
		set_rgba(cr, st == KS_LOCKED ? fg : t->accent, 1);
		cairo_fill(cr);
	}
}

static void draw_keyboard_base(struct app *a, cairo_t *cr, PangoLayout *lay)
{
	const struct theme_colors *t = &themes[a->cfg.theme];
	cairo_rectangle(cr, 0, a->band, a->width, a->kb_height);
	set_rgba(cr, t->bg, 1);
	cairo_fill(cr);
	for (int i = 0; i < a->geo.n; i++) draw_key(a, cr, lay, &a->geo.box[i], KS_IDLE);
}

/* --------------------------------------------------------------- settings */
static void draw_tile(struct app *a, cairo_t *cr, PangoLayout *lay, struct rectf r, const struct tile_info *ti, bool pressed, int zone)
{
	const struct theme_colors *t = &themes[a->cfg.theme];
	double pad = fmax(3, a->kb_height * 0.012);
	double x = r.x + pad, y = r.y + pad, w = r.w - 2 * pad, h = r.h - 2 * pad;
	if (w <= 2 || h <= 2) return;
	double alpha = ti->disabled ? 0.4 : 1.0;
	rounded_rect(cr, x, y, w, h, a->cfg.radius);
	set_rgba(cr, pressed && zone == 0 ? t->pressed : t->key, 1);
	cairo_fill(cr);
	if (ti->active || ti->confirming) {
		rounded_rect(cr, x + 1, y + 1, w - 2, h - 2, a->cfg.radius);
		set_rgba(cr, t->accent, alpha);
		cairo_set_line_width(cr, 2);
		cairo_stroke(cr);
	}
	/* sized for a ~49 px tall tile (landscape default) on a 2736x1824 @2x panel:
	 * value ~17 px, caption >= 12 px logical */
	double fs = fmin(h * 0.34, 26) * a->cfg.font_scale;
	if (fs > h * 0.42) fs = h * 0.42;
	double cap_fs = fmin(fmax(fs * 0.8, 12), h * 0.3);
	bool has_cap = ti->caption && *ti->caption;
	if (ti->kind == TK_STEPPER) {
		/* visible -/+ touch targets: a pill per zone around the value line (the whole
		 * zone column stays touchable), highlighted while pressed */
		double zw = w * 0.3, ip = fmax(2, h * 0.07);
		double py = has_cap ? y + h * 0.42 : y + ip, ph = y + h - ip - py;
		for (int zi = -1; zi <= 1; zi += 2) {
			double zx = zi < 0 ? x : x + w - zw;
			bool hot = pressed && zone == zi;
			if (hot) {
				rounded_rect(cr, zx, y, zw, h, a->cfg.radius);
				set_rgba(cr, t->pressed, 1);
			} else {
				rounded_rect(cr, zx + ip, py, zw - 2 * ip, ph, fmin(a->cfg.radius, ph / 2));
				set_rgba(cr, t->mod, alpha);
			}
			cairo_fill(cr);
		}
	}
	if (has_cap) {
		set_rgba(cr, t->dim_text, alpha);
		draw_text(a, cr, lay, ti->caption, x + w / 2, y + h * 0.27, cap_fs, w - 8, false);
	}
	double vy = (ti->caption && *ti->caption) ? y + h * 0.66 : y + h / 2;
	if (ti->kind == TK_STEPPER) {
		double zw = w * 0.3;
		set_rgba(cr, t->accent, alpha);
		draw_text(a, cr, lay, "−", x + zw / 2, vy, fs * 1.2, zw, true);
		draw_text(a, cr, lay, "+", x + w - zw / 2, vy, fs * 1.2, zw, true);
		set_rgba(cr, t->text, alpha);
		draw_text(a, cr, lay, ti->value, x + w / 2, vy, fs, w - 2 * zw, true);
	} else {
		set_rgba(cr, ti->confirming ? t->accent : (ti->active && ti->kind == TK_TOGGLE ? t->accent : t->text), alpha);
		draw_text(a, cr, lay, ti->value, x + w / 2, vy, fs, w - 8, true);
	}
}

static void draw_settings_base(struct app *a, cairo_t *cr, PangoLayout *lay)
{
	const struct theme_colors *t = &themes[a->cfg.theme];
	cairo_rectangle(cr, 0, a->band, a->width, a->kb_height);
	set_rgba(cr, t->bg, 1);
	cairo_fill(cr);
	struct settings_view *sv = &a->sv;
	int per = sv->rows * sv->cols;
	for (int i = 0; i < per; i++) {
		int tile = sv->page * per + i;
		if (tile >= TL_COUNT) break;
		struct tile_info ti;
		settings_tile_info(a, tile, &ti);
		draw_tile(a, cr, lay, settings_tile_rect(a, i), &ti, false, 0);
	}
	for (int n = NAV_FIRST; n < NAV_COUNT_END; n++) {
		struct tile_info ti;
		settings_nav_info(a, n, &ti);
		struct rectf r = settings_nav_rect(a, n);
		if (r.w <= 0) continue;
		if (n == NAV_CLOSE) {
			double pad = fmax(3, a->kb_height * 0.012);
			rounded_rect(cr, r.x + pad, r.y + pad, r.w - 2 * pad, r.h - 2 * pad, a->cfg.radius);
			set_rgba(cr, t->mod, 1);
			cairo_fill(cr);
			double is = fmin(r.h * 0.42, r.w * 0.4);
			draw_icon(cr, IC_KEYBOARD, r.x + r.w / 2, r.y + r.h / 2, is, t->text, false);
			continue;
		}
		if (n == NAV_PAGE) {
			set_rgba(cr, t->dim_text, 1);
			const char *txt = sv->status[0] ? sv->status : ti.value;
			draw_text(a, cr, lay, txt, r.x + r.w / 2, r.y + r.h / 2, fmin(r.h * 0.3, 20) * a->cfg.font_scale, r.w - 6, false);
			continue;
		}
		draw_tile(a, cr, lay, r, &ti, false, 0);
	}
}

/* --------------------------------------------------------------- frame */
static bool slot_box_current(const struct app *a, const struct slot *s, const struct keybox **out)
{
	for (int i = 0; i < a->geo.n; i++) {
		const struct keybox *b = &a->geo.box[i];
		if (b->key == s->key && fabsf(b->x - s->box.x) < 0.5f && fabsf(b->y - s->box.y) < 0.5f) {
			*out = b;
			return true;
		}
	}
	return false;
}

static void draw_bubble(struct app *a, cairo_t *cr, PangoLayout *lay, const struct keybox *b)
{
	const struct theme_colors *t = &themes[a->cfg.theme];
	double rh = a->geo.row_h;
	double bw = fmax(b->w * 1.3, rh * 0.9), bh = rh * 1.1;
	double bx = b->x + b->w / 2 - bw / 2;
	double by = b->y + 0.15 * rh - bh;
	if (bx < 0) bx = 0;
	if (bx + bw > a->width) bx = a->width - bw;
	if (by < 0) by = 0;
	rounded_rect(cr, bx + 1, by + 2, bw, bh, a->cfg.radius + 2);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
	cairo_fill(cr);
	rounded_rect(cr, bx, by, bw, bh, a->cfg.radius + 2);
	set_rgba(cr, t->bubble, 1);
	cairo_fill_preserve(cr);
	set_rgba(cr, t->accent, 0.6);
	cairo_set_line_width(cr, 1.5);
	cairo_stroke(cr);
	set_rgba(cr, t->text, 1);
	draw_text(a, cr, lay, key_label(a, b->key), bx + bw / 2, by + bh * 0.45, base_font(a) * 1.5, bw - 6, false);
}

static void draw_overlays(struct app *a, cairo_t *cr, PangoLayout *lay)
{
	if (a->settings_open) {
		struct settings_view *sv = &a->sv;
		if (sv->pressing && sv->press_inside && sv->press_tile >= 0) {
			struct tile_info ti;
			struct rectf r;
			int per = sv->rows * sv->cols;
			if (sv->press_tile >= NAV_FIRST) {
				if (sv->press_tile == NAV_PAGE || sv->press_tile == NAV_CLOSE) return;
				settings_nav_info(a, sv->press_tile, &ti);
				r = settings_nav_rect(a, sv->press_tile);
			} else {
				int i = sv->press_tile - sv->page * per;
				if (i < 0 || i >= per) return;
				settings_tile_info(a, sv->press_tile, &ti);
				r = settings_tile_rect(a, i);
			}
			if (!ti.disabled) draw_tile(a, cr, lay, r, &ti, true, sv->press_zone);
		}
		return;
	}
	/* modifier / caps states */
	for (int i = 0; i < a->geo.n; i++) {
		const struct keybox *b = &a->geo.box[i];
		const struct key *k = b->key;
		enum kstate st = KS_IDLE;
		if (k->type == KT_MOD) {
			switch (a->in.mod[k->mod].st) {
			case MS_LATCHED: st = KS_LATCHED; break;
			case MS_LOCKED:
			case MS_HELD: st = KS_LOCKED; break;
			default: break;
			}
		} else if (k->type == KT_CAPS && a->in.caps) {
			st = KS_LOCKED;
		}
		if (st != KS_IDLE) draw_key(a, cr, lay, b, st);
	}
	/* pressed keys */
	for (int i = 0; i < MAX_SLOTS; i++) {
		const struct slot *s = &a->in.slot[i];
		const struct keybox *b;
		if (!s->used || !s->inside || !s->key || s->key->type == KT_MOD) continue;
		if (!slot_box_current(a, s, &b)) continue;
		draw_key(a, cr, lay, b, KS_PRESSED);
	}
	/* preview bubbles */
	if (!app_preview_effective(a)) return;
	for (int i = 0; i < MAX_SLOTS; i++) {
		const struct slot *s = &a->in.slot[i];
		const struct keybox *b;
		if (!s->used || !s->inside || !s->key || s->key->type != KT_CHAR || (s->key->flags & KF_NO_PREVIEW)) continue;
		if (!slot_box_current(a, s, &b)) continue;
		draw_bubble(a, cr, lay, b);
	}
}

void render_frame(struct app *a, struct shm_buffer *buf, double scale)
{
	struct render *r = a->render;
	struct base_key key;
	memset(&key, 0, sizeof key); /* padding must be zero for memcmp */
	key.page = a->page;
	key.shift = input_shift_active(&a->in);
	key.caps = a->in.caps;
	key.settings = a->settings_open;
	key.locked = a->locked;
	key.theme = a->cfg.theme;
	key.w = buf->width;
	key.h = buf->height;
	key.scale = scale;
	key.gen = r->gen;
	if (!r->valid || memcmp(&key, &r->key, sizeof key) != 0 || !r->base) {
		if (!r->base || cairo_image_surface_get_width(r->base) != buf->width || cairo_image_surface_get_height(r->base) != buf->height) {
			if (r->base) cairo_surface_destroy(r->base);
			r->base = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, buf->width, buf->height);
		}
		cairo_t *cr = cairo_create(r->base);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_scale(cr, scale, scale);
		PangoLayout *lay = pango_cairo_create_layout(cr);
		if (a->settings_open) draw_settings_base(a, cr, lay);
		else draw_keyboard_base(a, cr, lay);
		g_object_unref(lay);
		cairo_destroy(cr);
		cairo_surface_flush(r->base);
		r->key = key;
		r->valid = true;
	}
	cairo_surface_t *dst = cairo_image_surface_create_for_data(buf->data, CAIRO_FORMAT_ARGB32, buf->width, buf->height, buf->stride);
	cairo_t *cr = cairo_create(dst);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, r->base, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_scale(cr, scale, scale);
	PangoLayout *lay = pango_cairo_create_layout(cr);
	draw_overlays(a, cr, lay);
	g_object_unref(lay);
	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);
}

/* Round button in the style of the noctalia greeter's session buttons: a faint ring
 * on a translucent dark disc with a light outline icon. */
void render_button(struct app *a, struct shm_buffer *buf, double scale)
{
	cairo_surface_t *dst = cairo_image_surface_create_for_data(buf->data, CAIRO_FORMAT_ARGB32, buf->width, buf->height, buf->stride);
	cairo_t *cr = cairo_create(dst);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_scale(cr, scale, scale);
	double r = BTN_D / 2.0, lw = 1.25;
	cairo_arc(cr, r, r, r - lw, 0, 2 * M_PI);
	if (a->btn_pressing) cairo_set_source_rgba(cr, 0.16, 0.16, 0.32, 0.85);
	else cairo_set_source_rgba(cr, 0.03, 0.03, 0.13, 0.55);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.30, 0.30, 0.62, 0.55);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
	draw_icon(cr, IC_KEYBOARD, r, r, BTN_D * 0.4, (struct rgba){ 0.75, 0.73, 0.78, 1 }, false);
	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);
}
