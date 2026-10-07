/* settings.h - settings view model: tiles, pagination, hit-testing, live apply. */
#ifndef SLATEKBD_SETTINGS_H
#define SLATEKBD_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

struct app;

enum tile_id {
	TL_MODE, TL_AUTO, TL_FONT_SCALE, TL_SHAPE, TL_RADIUS, TL_PREVIEW, TL_MODIFIERS,
	TL_SPLIT, TL_SPLIT_GAP, TL_HEIGHT_LAND, TL_HEIGHT_PORT, TL_THEME, TL_REPEAT,
	TL_REPEAT_DELAY, TL_REPEAT_RATE, TL_SHIFT_CAPS, TL_NUMPAD, TL_LOCKSCREEN, TL_OUTPUT,
	TL_ROT0, TL_ROT1, TL_ROT2, TL_ROT3, TL_RESET,
	TL_COUNT
};

enum tile_kind { TK_TOGGLE, TK_CYCLE, TK_STEPPER, TK_ACTION };

enum nav_id { NAV_PREV = 100, NAV_PAGE, NAV_NEXT, NAV_HIDE, NAV_QUIT, NAV_CLOSE, NAV_COUNT_END };
#define NAV_FIRST NAV_PREV
#define NAV_N (NAV_COUNT_END - NAV_PREV)

struct tile_info {
	int kind;
	const char *caption;
	char value[64];
	bool disabled;
	bool active;     /* highlighted (On / current rotation) */
	bool confirming; /* waiting for the second tap */
};

struct rectf { float x, y, w, h; };

void settings_reset_view(struct app *a);
/* Recompute grid for the current size. */
void settings_layout(struct app *a);
/* Rect of the i-th tile on the current page (i in 0..rows*cols-1). */
struct rectf settings_tile_rect(const struct app *a, int i);
struct rectf settings_nav_rect(const struct app *a, int nav);
float settings_nav_h(const struct app *a);
void settings_tile_info(const struct app *a, int tile, struct tile_info *out);
void settings_nav_info(const struct app *a, int nav, struct tile_info *out);
/* Hit test: returns tile id, NAV_* id, or -1; zone for steppers. */
int settings_hit(const struct app *a, float x, float y, int *zone);

void settings_down(struct app *a, int32_t id, bool ptr, float x, float y);
void settings_motion(struct app *a, int32_t id, bool ptr, float x, float y);
void settings_up(struct app *a, int32_t id, bool ptr);
void settings_cancel(struct app *a);
void settings_repeat_fire(struct app *a);
void settings_confirm_timeout(struct app *a);

#endif
