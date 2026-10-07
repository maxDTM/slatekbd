/* config.h - runtime configuration, key=value file format, CLI overrides. */
#ifndef SLATEKBD_CONFIG_H
#define SLATEKBD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

enum disp_mode { MODE_POPUP, MODE_OVERLAY };
enum key_shape { SHAPE_RECT, SHAPE_SQUARE };
enum mod_mode { MODMODE_TOGGLE, MODMODE_HOLD };
enum theme { THEME_DARK, THEME_LIGHT };
enum lock_mode { LOCK_AUTO, LOCK_ALWAYS, LOCK_OFF };
enum start_vis { START_AUTO, START_SHOWN, START_HIDDEN };

/* One bit per config key (index into the key table), used for CLI overrides. */
enum cfg_key {
	CK_MODE, CK_AUTO, CK_FONT, CK_FONT_SCALE, CK_KEY_SHAPE, CK_KEY_RADIUS, CK_PREVIEW,
	CK_MODIFIERS, CK_SHIFT_CAPS, CK_SPLIT, CK_SPLIT_GAP, CK_SPLIT_PORTRAIT,
	CK_HEIGHT_LAND, CK_HEIGHT_PORT, CK_THEME, CK_REPEAT, CK_REPEAT_DELAY, CK_REPEAT_RATE,
	CK_AUTO_NUMPAD, CK_OUTPUT, CK_LOCKSCREEN, CK_LOCK_RULE, CK_ROTATE_CMD, CK_START,
	CK_LOCK_BUTTON,
	CK_COUNT
};

#define CFG_MAX_EXTRA 32

struct config {
	enum disp_mode mode;
	bool auto_show;
	char font[64];
	double font_scale;      /* 0.6 .. 2.0 */
	enum key_shape shape;
	int radius;             /* 0 .. 16 */
	bool preview;
	enum mod_mode modmode;
	bool shift_caps;
	bool split;
	int split_gap;          /* % 10 .. 60 */
	bool split_portrait;
	int height_land, height_port; /* 150 .. 600 */
	enum theme theme;
	bool repeat;
	int repeat_delay;       /* ms 200 .. 1000 */
	int repeat_rate;        /* Hz 5 .. 50 */
	bool auto_numpad;
	char output[32];
	enum lock_mode lockscreen;
	bool lock_rule;
	char rotate_cmd[256];
	enum start_vis start;
	bool lock_button;       /* lock/login screen: show a round button; keyboard opens on tap */

	uint64_t overridden;    /* bit per enum cfg_key: value came from the CLI */
	/* values that were in the file for overridden keys (preserved on save) */
	char file_val[CK_COUNT][256];
	uint64_t file_has;
	/* unknown lines kept verbatim */
	char *extra[CFG_MAX_EXTRA];
	int nextra;
};

void config_defaults(struct config *c);
void config_free(struct config *c);
/* Default path into buf ($XDG_CONFIG_HOME/slatekbd/config). */
void config_default_path(char *buf, int len);
/* Parse a file. Missing file is not an error. Returns 0, or -1 on I/O error. */
int config_load(struct config *c, const char *path);
/* Parse a whole buffer (used by load and tests). */
void config_parse_buffer(struct config *c, const char *buf);
/* Set one key from a string. Returns 0 if ok, 1 unknown key, 2 bad value. */
int config_set(struct config *c, const char *key, const char *val);
/* Serialise to buf (canonical order). Returns length or -1 if too small. */
int config_serialize(const struct config *c, char *buf, int len);
/* Atomically write to path (mkdir -p, tmp + rename). Returns 0 on success. */
int config_save(const struct config *c, const char *path);
/* Name of a key */
const char *config_key_name(enum cfg_key k);
/* value formatting for one key */
void config_format_value(const struct config *c, enum cfg_key k, char *buf, int len);
/* Clamp all numeric values into range. */
void config_clamp(struct config *c);
bool config_parse_bool(const char *s, bool *out);

extern int config_verbose; /* print warnings to stderr when > 0 */

#endif
