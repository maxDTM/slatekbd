/* test_geometry: normal / square / split geometry, alignment and hit testing. */
#include <linux/input-event-codes.h>
#include <math.h>
#include <string.h>

#include "layout.h"
#include "t.h"

static struct geometry g;

static const struct keybox *find(const struct geometry *geo, uint16_t code, int nth)
{
	for (int i = 0; i < geo->n; i++)
		if (geo->box[i].key->code == code && geo->box[i].key->type != KT_PAGE && nth-- == 0) return &geo->box[i];
	return NULL;
}

static const struct keybox *find_type(const struct geometry *geo, int type)
{
	for (int i = 0; i < geo->n; i++)
		if (geo->box[i].key->type == type) return &geo->box[i];
	return NULL;
}

static bool near(float a, float b) { return fabsf(a - b) < 0.6f; }

static void check_common(const struct geometry *geo, float W, const char *what)
{
	/* no overlaps within a row, inside the surface, hit test at centres returns the key */
	for (int i = 0; i < geo->n; i++) {
		const struct keybox *a = &geo->box[i];
		CHECKF(a->x >= -0.01f && a->x + a->w <= W + 0.01f, "%s: box %d out of bounds", what, i);
		CHECKF(a->w > 1, "%s: box %d too small", what, i);
		for (int j = i + 1; j < geo->n; j++) {
			const struct keybox *b = &geo->box[j];
			if (a->row != b->row) continue;
			bool overlap = a->x < b->x + b->w - 0.01f && b->x < a->x + a->w - 0.01f;
			CHECKF(!overlap, "%s: boxes %d and %d overlap", what, i, j);
		}
		const struct keybox *h = layout_hit(geo, a->x + a->w / 2, a->y + a->h / 2);
		CHECKF(h == a, "%s: hit test at centre of box %d", what, i);
	}
}

static void check_fn_alignment(const struct geometry *geo, const char *what)
{
	const struct keybox *up = find(geo, KEY_UP, 0), *down = find(geo, KEY_DOWN, 0);
	const struct keybox *left = find(geo, KEY_LEFT, 0), *right = find(geo, KEY_RIGHT, 0);
	const struct keybox *del = find(geo, KEY_DELETE, 0), *end = find(geo, KEY_END, 0), *pgdn = find(geo, KEY_PAGEDOWN, 0);
	const struct keybox *ins = find(geo, KEY_INSERT, 0), *pgup = find(geo, KEY_PAGEUP, 0);
	CHECK(up && down && left && right && del && end && pgdn && ins && pgup);
	if (!(up && down && left && right && del && end && pgdn && ins && pgup)) return;
	CHECKF(near(up->x + up->w / 2, down->x + down->w / 2), "%s: up centred over down", what);
	CHECKF(near(del->x, left->x) && near(end->x, down->x) && near(pgdn->x, right->x), "%s: Del/End/PgDn over arrows", what);
	CHECKF(near(ins->x, del->x) && near(pgup->x, pgdn->x), "%s: Ins/PgUp over Del/PgDn", what);
}

int main(void)
{
	const float Ws[2] = { 1368, 912 }, Hs[2] = { 280, 340 };
	for (int o = 0; o < 2; o++) {
		float W = Ws[o], H = Hs[o];
		for (int page = 0; page < PAGE_COUNT; page++) {
			for (int shape = 0; shape < 2; shape++) {
				for (int split = 0; split < 2; split++) {
					char what[64];
					snprintf(what, sizeof what, "W%.0f page%d shape%d split%d", W, page, shape, split);
					struct geo_params p = { W, H, layout_band(H), page, shape, split, 30 };
					CHECK(layout_build(&g, &p) == 0);
					check_common(&g, W, what);
					CHECKF(find_type(&g, KT_SETTINGS) != NULL, "%s: gear", what);
					if (page == PAGE_FN) check_fn_alignment(&g, what);
					if (split) {
						const struct page *pg = &layout_pages[page];
						float lx = g.kb_x + pg->split_l * g.unit;              /* left half's gap edge */
						float rx = W - g.kb_x - pg->split_r * g.unit;          /* right half's gap edge */
						CHECKF(rx - lx >= W * 0.30f - 0.5f, "%s: gap too narrow", what);
						for (int i = 0; i < g.n; i++) {
							const struct keybox *b = &g.box[i];
							if (b->half == 0) CHECKF(b->x + b->w <= lx + 0.5f, "%s: left box %d crosses gap", what, i);
							else CHECKF(b->x >= rx - 0.5f, "%s: right box %d crosses gap", what, i);
						}
						/* gap hit returns NULL */
						for (int r = 0; r < g.nrows; r++)
							CHECK(layout_hit(&g, W / 2, g.kb_y + (r + 0.5f) * g.row_h) == NULL);
						/* every row: straight gap edges on both halves, flush outer edges */
						for (int r = 0; r < g.nrows; r++) {
							bool gl = false, gr = false, ol = false, orr = false;
							for (int i = g.row_start[r]; i < g.row_start[r + 1]; i++) {
								const struct keybox *b = &g.box[i];
								if (b->half == 0 && near(b->x + b->w, lx)) gl = true;
								if (b->half == 1 && near(b->x, rx)) gr = true;
								if (near(b->x, g.kb_x)) ol = true;
								if (near(b->x + b->w, W - g.kb_x)) orr = true;
							}
							/* FN row 4 ends with a spacer right of Up (inverted T) */
							if (page == PAGE_FN && r == 3) orr = true;
							CHECKF(gl && gr && ol && orr, "%s: row %d edges %d%d%d%d", what, r, gl, gr, ol, orr);
						}
						if (shape == 0) CHECK(near(g.kb_x, 0));
						/* outer function keys are 2u; space is 3u on each side */
						const struct keybox *tab = find(&g, KEY_TAB, 0), *sp0 = find(&g, KEY_SPACE, 0);
						CHECK(tab && near(tab->w, 2 * g.unit));
						CHECK(sp0 && near(sp0->w, 3 * g.unit));
						if (page != PAGE_FN) {
							const struct keybox *sp1 = find(&g, KEY_SPACE, 1);
							CHECK(sp1 && near(sp1->w, 3 * g.unit) && near(sp1->x, rx) && near(sp0->x + sp0->w, lx));
						}
					} else if (shape == 0) {
						CHECK(near(g.unit, W / 15));
					} else {
						CHECK(g.unit <= g.row_h + 0.01f);
						CHECK(near(g.kb_x * 2 + g.kb_w, W));
					}
				}
			}
		}
	}
	/* points above the keyboard (band) and in spacers return NULL */
	struct geo_params p = { 1368, 280, 70, PAGE_FN, GEO_RECT, false, 30 };
	layout_build(&g, &p);
	CHECK(layout_hit(&g, 100, 10) == NULL);
	/* FN row 4 (index 3): right Shift sits directly left of Up (12..13.5u); only
	 * 13.5..15u (right of Up, inverted T) is empty */
	float u = 1368.0f / 15;
	const struct keybox *rs = layout_hit(&g, 11.0f * u, g.kb_y + 3.5f * g.row_h);
	CHECK(rs && rs->key->code == KEY_RIGHTSHIFT && near(rs->x + rs->w, 12.0f * u));
	const struct keybox *up = layout_hit(&g, 12.75f * u, g.kb_y + 3.5f * g.row_h);
	CHECK(up && up->key->code == KEY_UP);
	CHECK(layout_hit(&g, 14.0f * u, g.kb_y + 3.5f * g.row_h) == NULL);
	/* FN row 3: Enter ends where Del starts */
	const struct keybox *ent = find(&g, KEY_ENTER, 0), *del = find(&g, KEY_DELETE, 0);
	CHECK(ent && del && near(ent->x + ent->w, del->x));
	/* no empty gaps on FN rows 1-3 and 5 (non-split) */
	for (int r = 0; r < g.nrows; r++) {
		if (r == 3) continue;
		for (float x = 0.25f * u; x < 15 * u; x += 0.5f * u)
			CHECKF(layout_hit(&g, x, g.kb_y + (r + 0.5f) * g.row_h) != NULL, "FN row %d gap at %.2fu", r, x / u);
	}
	/* MAIN: number row on top, Q at 1.5u (after Tab), same row height as the other pages */
	float fn_row_h = g.row_h;
	p.page = PAGE_MAIN;
	layout_build(&g, &p);
	CHECK(near(g.row_h, fn_row_h));
	const struct keybox *q = find(&g, KEY_Q, 0), *one = find(&g, KEY_1, 0);
	CHECK(q && near(q->x, 1.5f * u) && q->row == 1);
	CHECK(one && one->row == 0 && near(one->x, 1.0f * u));
	T_DONE();
}
