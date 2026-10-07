/* config.c - defaults, key=value parse/serialise, atomic save. */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int config_verbose = 1;

static const char *const key_names[CK_COUNT] = {
	[CK_MODE] = "mode", [CK_AUTO] = "auto", [CK_FONT] = "font", [CK_FONT_SCALE] = "font_scale",
	[CK_KEY_SHAPE] = "key_shape", [CK_KEY_RADIUS] = "key_radius", [CK_PREVIEW] = "preview",
	[CK_MODIFIERS] = "modifiers", [CK_SHIFT_CAPS] = "shift_caps", [CK_SPLIT] = "split",
	[CK_SPLIT_GAP] = "split_gap", [CK_SPLIT_PORTRAIT] = "split_portrait",
	[CK_HEIGHT_LAND] = "height_landscape", [CK_HEIGHT_PORT] = "height_portrait",
	[CK_THEME] = "theme", [CK_REPEAT] = "repeat", [CK_REPEAT_DELAY] = "repeat_delay",
	[CK_REPEAT_RATE] = "repeat_rate", [CK_AUTO_NUMPAD] = "auto_numpad", [CK_OUTPUT] = "output",
	[CK_LOCKSCREEN] = "lockscreen", [CK_LOCK_RULE] = "lock_rule", [CK_ROTATE_CMD] = "rotate_cmd",
	[CK_START] = "start", [CK_LOCK_BUTTON] = "lock_button",
};

static const char *const key_comments[CK_COUNT] = {
	[CK_MODE] = "popup | overlay", [CK_AUTO] = "auto show/hide via input-method-v2",
	[CK_FONT_SCALE] = "0.6 .. 2.0", [CK_KEY_SHAPE] = "rect | square", [CK_KEY_RADIUS] = "0 .. 16",
	[CK_MODIFIERS] = "toggle | hold", [CK_SHIFT_CAPS] = "double-tap Shift toggles Caps Lock",
	[CK_SPLIT_GAP] = "percent of width, 10 .. 60", [CK_HEIGHT_LAND] = "logical px, 150 .. 600",
	[CK_HEIGHT_PORT] = "logical px, 150 .. 600", [CK_THEME] = "dark | light",
	[CK_REPEAT_DELAY] = "ms", [CK_REPEAT_RATE] = "per second",
	[CK_AUTO_NUMPAD] = "numeric fields open the ?123 page",
	[CK_OUTPUT] = "auto | connector name (eDP-1, DP-3, ...)",
	[CK_LOCKSCREEN] = "auto | always | off",
	[CK_LOCK_RULE] = "add the Hyprland above_lock layer rule at runtime",
	[CK_ROTATE_CMD] = "empty = built-in hyprctl chain; %o output, %t transform 0-3",
	[CK_START] = "auto | shown | hidden",
	[CK_LOCK_BUTTON] = "lock/login screen: keyboard behind a button (1) or always open (0)",
};

const char *config_key_name(enum cfg_key k)
{
	return (k >= 0 && k < CK_COUNT) ? key_names[k] : "?";
}

void config_defaults(struct config *c)
{
	memset(c, 0, sizeof(*c));
	c->mode = MODE_POPUP;
	c->auto_show = true;
	snprintf(c->font, sizeof c->font, "Sans");
	c->font_scale = 1.0;
	c->shape = SHAPE_RECT;
	c->radius = 6;
	c->preview = true;
	c->modmode = MODMODE_TOGGLE;
	c->shift_caps = true;
	c->split = false;
	c->split_gap = 30;
	c->split_portrait = false;
	c->height_land = 280;
	c->height_port = 340;
	c->theme = THEME_DARK;
	c->repeat = true;
	c->repeat_delay = 400;
	c->repeat_rate = 25;
	c->auto_numpad = true;
	snprintf(c->output, sizeof c->output, "auto");
	c->lockscreen = LOCK_AUTO;
	c->lock_rule = true;
	c->rotate_cmd[0] = 0;
	c->start = START_AUTO;
	c->lock_button = true;
}

void config_free(struct config *c)
{
	for (int i = 0; i < c->nextra; i++) free(c->extra[i]);
	c->nextra = 0;
}

void config_default_path(char *buf, int len)
{
	const char *x = getenv("XDG_CONFIG_HOME");
	if (x && *x) snprintf(buf, len, "%s/slatekbd/config", x);
	else snprintf(buf, len, "%s/.config/slatekbd/config", getenv("HOME") ? getenv("HOME") : "/tmp");
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void config_clamp(struct config *c)
{
	if (!isfinite(c->font_scale)) c->font_scale = 1.0;
	if (c->font_scale < 0.6) c->font_scale = 0.6;
	if (c->font_scale > 2.0) c->font_scale = 2.0;
	c->radius = clampi(c->radius, 0, 16);
	c->split_gap = clampi(c->split_gap, 10, 60);
	c->height_land = clampi(c->height_land, 150, 600);
	c->height_port = clampi(c->height_port, 150, 600);
	c->repeat_delay = clampi(c->repeat_delay, 200, 1000);
	c->repeat_rate = clampi(c->repeat_rate, 5, 50);
}

bool config_parse_bool(const char *s, bool *out)
{
	static const char *t[] = { "1", "true", "yes", "on" }, *f[] = { "0", "false", "no", "off" };
	for (int i = 0; i < 4; i++) {
		if (!strcasecmp(s, t[i])) { *out = true; return true; }
		if (!strcasecmp(s, f[i])) { *out = false; return true; }
	}
	return false;
}

static bool parse_int(const char *s, int lo, int hi, int *out)
{
	char *end;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno || end == s || *end || v < lo || v > hi) return false;
	*out = (int)v;
	return true;
}

static bool parse_enum(const char *s, const char *const *names, int n, int *out)
{
	for (int i = 0; i < n; i++)
		if (!strcasecmp(s, names[i])) { *out = i; return true; }
	return false;
}

static const char *const mode_names[] = { "popup", "overlay" };
static const char *const shape_names[] = { "rect", "square" };
static const char *const modmode_names[] = { "toggle", "hold" };
static const char *const theme_names[] = { "dark", "light" };
static const char *const lock_names[] = { "auto", "always", "off" };
static const char *const start_names[] = { "auto", "shown", "hidden" };

static int find_key(const char *key)
{
	for (int i = 0; i < CK_COUNT; i++)
		if (!strcmp(key, key_names[i])) return i;
	/* aliases */
	if (!strcmp(key, "height") || !strcmp(key, "height_land")) return CK_HEIGHT_LAND;
	if (!strcmp(key, "height_port")) return CK_HEIGHT_PORT;
	return -1;
}

int config_set(struct config *c, const char *key, const char *val)
{
	int k = find_key(key), iv;
	bool b;
	switch (k) {
	case CK_MODE: if (!parse_enum(val, mode_names, 2, &iv)) return 2; c->mode = iv; return 0;
	case CK_AUTO: if (!config_parse_bool(val, &b)) return 2; c->auto_show = b; return 0;
	case CK_FONT:
		if (!*val || strlen(val) >= sizeof c->font) return 2;
		snprintf(c->font, sizeof c->font, "%s", val);
		return 0;
	case CK_FONT_SCALE: {
		char *end;
		double d = strtod(val, &end);
		if (end == val || *end || !(d >= 0.6 - 1e-9 && d <= 2.0 + 1e-9)) return 2; /* also rejects NaN */
		c->font_scale = d;
		return 0;
	}
	case CK_KEY_SHAPE: if (!parse_enum(val, shape_names, 2, &iv)) return 2; c->shape = iv; return 0;
	case CK_KEY_RADIUS: if (!parse_int(val, 0, 16, &iv)) return 2; c->radius = iv; return 0;
	case CK_PREVIEW: if (!config_parse_bool(val, &b)) return 2; c->preview = b; return 0;
	case CK_MODIFIERS: if (!parse_enum(val, modmode_names, 2, &iv)) return 2; c->modmode = iv; return 0;
	case CK_SHIFT_CAPS: if (!config_parse_bool(val, &b)) return 2; c->shift_caps = b; return 0;
	case CK_SPLIT: if (!config_parse_bool(val, &b)) return 2; c->split = b; return 0;
	case CK_SPLIT_GAP: if (!parse_int(val, 10, 60, &iv)) return 2; c->split_gap = iv; return 0;
	case CK_SPLIT_PORTRAIT: if (!config_parse_bool(val, &b)) return 2; c->split_portrait = b; return 0;
	case CK_HEIGHT_LAND: if (!parse_int(val, 150, 600, &iv)) return 2; c->height_land = iv; return 0;
	case CK_HEIGHT_PORT: if (!parse_int(val, 150, 600, &iv)) return 2; c->height_port = iv; return 0;
	case CK_THEME: if (!parse_enum(val, theme_names, 2, &iv)) return 2; c->theme = iv; return 0;
	case CK_REPEAT: if (!config_parse_bool(val, &b)) return 2; c->repeat = b; return 0;
	case CK_REPEAT_DELAY: if (!parse_int(val, 200, 1000, &iv)) return 2; c->repeat_delay = iv; return 0;
	case CK_REPEAT_RATE: if (!parse_int(val, 5, 50, &iv)) return 2; c->repeat_rate = iv; return 0;
	case CK_AUTO_NUMPAD: if (!config_parse_bool(val, &b)) return 2; c->auto_numpad = b; return 0;
	case CK_OUTPUT:
		if (!*val || strlen(val) >= sizeof c->output) return 2;
		snprintf(c->output, sizeof c->output, "%s", val);
		return 0;
	case CK_LOCKSCREEN: if (!parse_enum(val, lock_names, 3, &iv)) return 2; c->lockscreen = iv; return 0;
	case CK_LOCK_RULE: if (!config_parse_bool(val, &b)) return 2; c->lock_rule = b; return 0;
	case CK_ROTATE_CMD:
		if (strlen(val) >= sizeof c->rotate_cmd) return 2;
		snprintf(c->rotate_cmd, sizeof c->rotate_cmd, "%s", val);
		return 0;
	case CK_START: if (!parse_enum(val, start_names, 3, &iv)) return 2; c->start = iv; return 0;
	case CK_LOCK_BUTTON: if (!config_parse_bool(val, &b)) return 2; c->lock_button = b; return 0;
	default: return 1;
	}
}

static char *trim(char *s)
{
	while (isspace((unsigned char)*s)) s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
	return s;
}

static void parse_line(struct config *c, char *line, int lineno)
{
	char *orig = strdup(line);
	char *s = trim(line);
	if (!*s || *s == '#') { free(orig); return; }
	char *eq = strchr(s, '=');
	if (!eq) {
		if (config_verbose) fprintf(stderr, "slatekbd: config line %d: missing '='\n", lineno);
		free(orig);
		return;
	}
	*eq = 0;
	char *key = trim(s), *val = trim(eq + 1);
	/* strip trailing " # comment" (only when preceded by whitespace, so values may contain '#') */
	for (char *h = val; (h = strchr(h, '#')); h++) {
		if (h == val || isspace((unsigned char)h[-1])) { *h = 0; val = trim(val); break; }
	}
	int k = find_key(key);
	if (k >= 0 && (c->overridden & (1ULL << k))) {
		/* CLI wins; remember the file value so a save keeps it */
		snprintf(c->file_val[k], sizeof c->file_val[k], "%s", val);
		c->file_has |= 1ULL << k;
		free(orig);
		return;
	}
	int r = config_set(c, key, val);
	if (r == 1) {
		if (config_verbose) fprintf(stderr, "slatekbd: config line %d: unknown key '%s' (kept)\n", lineno, key);
		if (c->nextra < CFG_MAX_EXTRA) {
			char *nl = strchr(orig, '\n');
			if (nl) *nl = 0;
			c->extra[c->nextra++] = orig;
			return;
		}
	} else if (r == 2) {
		if (config_verbose) fprintf(stderr, "slatekbd: config line %d: bad value '%s' for %s, using default\n", lineno, val, key);
	}
	free(orig);
}

void config_parse_buffer(struct config *c, const char *buf)
{
	int lineno = 0;
	const char *p = buf;
	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		char line[1024];
		if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n);
		line[n] = 0;
		parse_line(c, line, ++lineno);
		if (!nl) break;
		p = nl + 1;
	}
}

int config_load(struct config *c, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return errno == ENOENT ? 0 : -1;
	char *buf = NULL;
	size_t cap = 0, len = 0;
	char tmp[4096];
	size_t n;
	while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) {
		if (len + n + 1 > cap) {
			cap = (len + n + 1) * 2;
			char *nb = realloc(buf, cap);
			if (!nb) { free(buf); fclose(f); return -1; }
			buf = nb;
		}
		memcpy(buf + len, tmp, n);
		len += n;
	}
	fclose(f);
	if (!buf) return 0;
	buf[len] = 0;
	config_parse_buffer(c, buf);
	free(buf);
	return 0;
}

void config_format_value(const struct config *c, enum cfg_key k, char *buf, int len)
{
	switch (k) {
	case CK_MODE: snprintf(buf, len, "%s", mode_names[c->mode]); break;
	case CK_AUTO: snprintf(buf, len, "%d", c->auto_show); break;
	case CK_FONT: snprintf(buf, len, "%s", c->font); break;
	case CK_FONT_SCALE: snprintf(buf, len, "%.1f", c->font_scale); break;
	case CK_KEY_SHAPE: snprintf(buf, len, "%s", shape_names[c->shape]); break;
	case CK_KEY_RADIUS: snprintf(buf, len, "%d", c->radius); break;
	case CK_PREVIEW: snprintf(buf, len, "%d", c->preview); break;
	case CK_MODIFIERS: snprintf(buf, len, "%s", modmode_names[c->modmode]); break;
	case CK_SHIFT_CAPS: snprintf(buf, len, "%d", c->shift_caps); break;
	case CK_SPLIT: snprintf(buf, len, "%d", c->split); break;
	case CK_SPLIT_GAP: snprintf(buf, len, "%d", c->split_gap); break;
	case CK_SPLIT_PORTRAIT: snprintf(buf, len, "%d", c->split_portrait); break;
	case CK_HEIGHT_LAND: snprintf(buf, len, "%d", c->height_land); break;
	case CK_HEIGHT_PORT: snprintf(buf, len, "%d", c->height_port); break;
	case CK_THEME: snprintf(buf, len, "%s", theme_names[c->theme]); break;
	case CK_REPEAT: snprintf(buf, len, "%d", c->repeat); break;
	case CK_REPEAT_DELAY: snprintf(buf, len, "%d", c->repeat_delay); break;
	case CK_REPEAT_RATE: snprintf(buf, len, "%d", c->repeat_rate); break;
	case CK_AUTO_NUMPAD: snprintf(buf, len, "%d", c->auto_numpad); break;
	case CK_OUTPUT: snprintf(buf, len, "%s", c->output); break;
	case CK_LOCKSCREEN: snprintf(buf, len, "%s", lock_names[c->lockscreen]); break;
	case CK_LOCK_RULE: snprintf(buf, len, "%d", c->lock_rule); break;
	case CK_ROTATE_CMD: snprintf(buf, len, "%s", c->rotate_cmd); break;
	case CK_START: snprintf(buf, len, "%s", start_names[c->start]); break;
	case CK_LOCK_BUTTON: snprintf(buf, len, "%d", c->lock_button); break;
	default: snprintf(buf, len, "?"); break;
	}
}

int config_serialize(const struct config *c, char *buf, int len)
{
	int off = snprintf(buf, len, "# slatekbd configuration (written by slatekbd; edits are kept)\n"
	                              "# Format: key = value. See `slatekbd --help`.\n");
	for (int k = 0; k < CK_COUNT; k++) {
		char val[300];
		if ((c->overridden & (1ULL << k))) {
			/* CLI override for this run only: keep the file's value (or omit if none) */
			if (!(c->file_has & (1ULL << k))) continue;
			snprintf(val, sizeof val, "%s", c->file_val[k]);
		} else {
			config_format_value(c, k, val, sizeof val);
		}
		char line[400];
		if (key_comments[k])
			snprintf(line, sizeof line, "%-16s = %-12s # %s\n", key_names[k], val, key_comments[k]);
		else
			snprintf(line, sizeof line, "%-16s = %s\n", key_names[k], val);
		/* a value that itself contains " #" would be cut on reload: drop the comment then */
		if (strstr(val, " #") || strchr(val, '#')) snprintf(line, sizeof line, "%-16s = %s\n", key_names[k], val);
		if (k == CK_ROTATE_CMD && !*val) snprintf(line, sizeof line, "%-16s =\n", key_names[k]);
		int n = snprintf(buf + off, off < len ? len - off : 0, "%s", line);
		off += n;
	}
	if (c->nextra) {
		int n = snprintf(buf + off, off < len ? len - off : 0, "# unknown keys (kept)\n");
		off += n;
		for (int i = 0; i < c->nextra; i++) {
			n = snprintf(buf + off, off < len ? len - off : 0, "%s\n", c->extra[i]);
			off += n;
		}
	}
	return off < len ? off : -1;
}

static int mkdir_p(const char *dir)
{
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", dir);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
	return 0;
}

int config_save(const struct config *c, const char *path)
{
	char dir[1024];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash && slash != dir) {
		*slash = 0;
		if (mkdir_p(dir) < 0) return -1;
	}
	char buf[16384];
	int n = config_serialize(c, buf, sizeof buf);
	if (n < 0) return -1;
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) return -1;
	ssize_t w = write(fd, buf, n);
	if (w != n || fsync(fd) < 0) {
		close(fd);
		unlink(tmp);
		return -1;
	}
	close(fd);
	if (rename(tmp, path) < 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}
