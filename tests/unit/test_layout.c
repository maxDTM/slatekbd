/* test_layout: static tables are consistent and every glyph matches the US xkb keymap. */
#include <linux/input-event-codes.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#include "layout.h"
#include "t.h"

static void utf8_for(struct xkb_keymap *map, uint16_t code, bool shift, char *out, size_t n)
{
	struct xkb_state *st = xkb_state_new(map);
	if (shift) xkb_state_update_key(st, KEY_LEFTSHIFT + 8, XKB_KEY_DOWN);
	xkb_state_key_get_utf8(st, code + 8, out, n);
	xkb_state_unref(st);
}

int main(void)
{
	char err[256] = "";
	bool ok = layout_validate(err, sizeof err);
	CHECKF(ok, "%s", err);

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_rule_names names = { "evdev", "pc105", "us", "", "" };
	struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &names, 0);
	CHECK(map != NULL);
	if (!map) T_DONE();

	for (int p = 0; p < PAGE_COUNT; p++) {
		const struct page *pg = &layout_pages[p];
		int gears = 0;
		for (int r = 0; r < pg->nrows; r++) {
			float sum = 0;
			int flex = 0;
			for (int i = 0; i < pg->rows[r].n; i++) {
				const struct key *k = &pg->rows[r].keys[i];
				if (k->width < 0) flex++;
				else sum += k->width;
				if (k->type == KT_SETTINGS) gears++;
				if (k->type == KT_CHAR || k->type == KT_MOD || k->type == KT_CAPS) {
					const xkb_keysym_t *syms;
					int n = xkb_keymap_key_get_syms_by_level(map, k->code + 8, 0, 0, &syms);
					CHECKF(n > 0 && syms[0] != XKB_KEY_NoSymbol, "page %s key '%s' code %u has no keysym", pg->name,
					       k->label, k->code);
				}
				if (k->type != KT_CHAR || !k->label || !*k->label) continue;
				char lo[16] = "", hi[16] = "";
				utf8_for(map, k->code, false, lo, sizeof lo);
				utf8_for(map, k->code, true, hi, sizeof hi);
				if (k->flags & KF_SHIFTED) {
					CHECKF(!strcmp(k->label, hi), "page %s: shifted '%s' but xkb gives '%s'", pg->name, k->label, hi);
				} else if (k->flags & KF_LETTER) {
					CHECKF(!strcmp(k->label, lo) && k->shift_label && !strcmp(k->shift_label, hi),
					       "letter '%s'/'%s' vs xkb '%s'/'%s'", k->label, k->shift_label, lo, hi);
				} else if (strlen(k->label) == 1 && k->code != KEY_SPACE) {
					CHECKF(!strcmp(k->label, lo), "page %s: '%s' but xkb gives '%s'", pg->name, k->label, lo);
					if (k->shift_label)
						CHECKF(!strcmp(k->shift_label, hi), "page %s: shift of '%s' is '%s', xkb '%s'", pg->name,
						       k->label, k->shift_label, hi);
				}
			}
			if (!flex) CHECKF(sum > 14.99f && sum < 15.01f, "page %s row %d sums %.2f", pg->name, r, sum);
		}
		CHECKF(gears >= 1, "page %s has no gear", pg->name);
	}

	/* conventional positions on MAIN */
	const struct row *r = layout_pages[PAGE_MAIN].rows;
	CHECK(layout_pages[PAGE_MAIN].nrows == 5);
	CHECK(r[0].keys[0].code == KEY_ESC);
	for (int i = 0; i < 10; i++) CHECK(r[0].keys[1 + i].code == KEY_1 + i);
	CHECK(r[0].keys[r[0].n - 1].code == KEY_BACKSPACE);
	CHECK(r[1].keys[0].code == KEY_TAB && r[1].keys[1].code == KEY_Q && r[1].keys[r[1].n - 1].code == KEY_BACKSLASH);
	CHECK(r[2].keys[0].code == KEY_CAPSLOCK && r[2].keys[r[2].n - 1].code == KEY_ENTER);
	CHECK(r[3].keys[0].code == KEY_LEFTSHIFT && r[3].keys[1].code == KEY_Z && r[3].keys[r[3].n - 1].code == KEY_RIGHTSHIFT);
	CHECK(r[4].keys[0].code == KEY_LEFTCTRL && r[4].keys[1].code == KEY_LEFTMETA && r[4].keys[2].code == KEY_LEFTALT);
	CHECK(r[4].keys[r[4].n - 1].code == KEY_RIGHTCTRL);
	/* every page has the same row count, so key height does not jump between pages */
	for (int p = 0; p < PAGE_COUNT; p++) CHECK(layout_pages[p].nrows == 5);
	/* SYM number row is 1..0 in order */
	const struct row *s = layout_pages[PAGE_SYM].rows;
	for (int i = 0; i < 10; i++) CHECK(s[0].keys[1 + i].code == KEY_1 + i);

	xkb_keymap_unref(map);
	xkb_context_unref(ctx);
	T_DONE();
}
