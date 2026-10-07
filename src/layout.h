/* layout.h - key tables, geometry engine and hit-testing (pure, no Wayland). */
#ifndef SLATEKBD_LAYOUT_H
#define SLATEKBD_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

enum key_type {
	KT_CHAR,     /* sends `code` (with implied Shift for KF_SHIFTED) */
	KT_MOD,      /* Shift/Ctrl/Alt/Super: `mod` is which, `code` is the L or R keycode */
	KT_CAPS,     /* KEY_CAPSLOCK tap */
	KT_PAGE,     /* switch to `page` */
	KT_SETTINGS, /* gear: open/close the settings view */
	KT_SPACER,   /* inert gap; width < 0 = flex */
};

enum key_flag {
	KF_REPEAT       = 1 << 0,
	KF_LETTER       = 1 << 1,
	KF_SHIFTED      = 1 << 2,
	KF_SPLIT_BEFORE = 1 << 3, /* split tables: first key of the right half */
	KF_SMALL        = 1 << 6,
	KF_NO_PREVIEW   = 1 << 7,
	KF_DIM          = 1 << 8, /* drawn with the modifier-key colour */
};

enum mod_id { MOD_SHIFT, MOD_CTRL, MOD_ALT, MOD_SUPER, MOD_COUNT };

enum icon {
	IC_NONE, IC_GEAR, IC_BKSP, IC_ENTER, IC_SHIFT, IC_TAB, IC_CAPS,
	IC_LEFT, IC_RIGHT, IC_UP, IC_DOWN, IC_KEYBOARD, IC_LOCK,
};

struct key {
	const char *label;
	const char *shift_label;
	uint8_t type;   /* enum key_type */
	uint16_t code;  /* evdev KEY_* */
	uint16_t flags; /* enum key_flag */
	float width;    /* u; spacer < 0 = flex */
	uint8_t mod;    /* KT_MOD: enum mod_id */
	uint8_t page;   /* KT_PAGE: target page */
	uint8_t icon;   /* enum icon */
};

struct row {
	const struct key *keys;
	int n;
};

enum page_id { PAGE_MAIN, PAGE_SYM, PAGE_FN, PAGE_COUNT };

struct page {
	const char *name;
	const struct row *rows;
	int nrows;
	float units;
	const struct row *split_rows; /* nrows rows; KF_SPLIT_BEFORE starts the right half */
	float split_l, split_r;       /* width of each half in units (every row) */
};

extern const struct page layout_pages[PAGE_COUNT];

enum key_shape_e { GEO_RECT, GEO_SQUARE };

struct keybox {
	const struct key *key;
	float x, y, w, h; /* logical px, relative to the surface (the cell; draw inset by pad/2) */
	uint8_t row;
	uint8_t half; /* 0 = full/left, 1 = right */
};

#define MAX_BOXES 112
#define MAX_ROWS 6

struct geometry {
	struct keybox box[MAX_BOXES];
	int n;
	int row_start[MAX_ROWS + 1]; /* box index range per row */
	int nrows;
	float kb_x, kb_y, kb_w, kb_h;
	float unit, row_h, pad;
	/* inputs */
	int page;
	bool split;
	int shape;
	float width, height, band;
	int gap_pct;
};

struct geo_params {
	float width;   /* surface width W */
	float height;  /* keyboard height H (excl. band) */
	float band;    /* preview band B above the keyboard */
	int page;
	int shape;     /* enum key_shape_e */
	bool split;
	int gap_pct;   /* 10..60 */
};

/* Build geometry. Returns 0 on success. */
int layout_build(struct geometry *g, const struct geo_params *p);
/* Hit test: returns the keybox under (x,y) or NULL (gaps, spacers, outside). */
const struct keybox *layout_hit(const struct geometry *g, float x, float y);
/* Validate all static tables: row sums, single flex, a gear on each page.
 * Writes a message into err on failure and returns false. */
bool layout_validate(char *err, int errlen);
/* Preview band height for a keyboard height */
float layout_band(float height);
/* Row height of a page for a keyboard height */
float layout_row_h(int page, float height);

#endif
