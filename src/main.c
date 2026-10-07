/* main.c - argument parsing, CLI client mode, startup, shutdown. */
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "app.h"
#include "im.h"
#include "instance.h"
#include "keymap.h"
#include "lock.h"
#include "render.h"
#include "rotate.h"
#include "settings.h"
#include "wayland.h"

#ifndef SLATEKBD_VERSION
#define SLATEKBD_VERSION "dev"
#endif

static struct app app;

static void usage(FILE *f)
{
	char cfg[1024];
	config_default_path(cfg, sizeof cfg);
	fprintf(f,
"slatekbd %s - on-screen touch keyboard for Wayland (layer-shell + virtual-keyboard)\n"
"\n"
"Usage:\n"
"  slatekbd [options]               start the keyboard (single instance)\n"
"  slatekbd --show|--hide|--toggle  control the running instance (exit 1 if none)\n"
"  slatekbd --quit                  stop the running instance\n"
"  slatekbd --lock|--unlock         lock-screen hooks (--lock starts an instance if none;\n"
"                                   that instance quits on --unlock)\n"
"  slatekbd --status                print the pid and exit 0 if running, else exit 1\n"
"\n"
"Options (override the config file for this run; not saved unless changed in Settings):\n"
"  -c, --config FILE              config file (default %s)\n"
"  -m, --mode popup|overlay       popup reserves space (windows resize above it);\n"
"                                 overlay floats over windows\n"
"  -a, --auto / -A, --no-auto     auto show/hide when a text field is focused\n"
"                                 (input-method-unstable-v2)\n"
"  -L, -H, --height-landscape PX  keyboard height in landscape (150-600, default 280)\n"
"  -P, --height-portrait PX       keyboard height in portrait (150-600, default 340)\n"
"  -s, --split / -S, --no-split   split keyboard (halves at the edges)\n"
"      --split-gap PCT            width of the empty middle in %% (10-60, default 30)\n"
"      --split-portrait / --no-split-portrait  also split in portrait\n"
"      --shape rect|square        key shape  --radius PX  corner radius (0-16)\n"
"  -f, --font-scale X             text size (0.6-2.0)  --font NAME  font family\n"
"      --modifiers toggle|hold    toggle: tap latches one-shot, double-tap locks;\n"
"                                 hold: active while a finger is on it\n"
"      --shift-caps / --no-shift-caps  double-tap Shift toggles Caps Lock\n"
"      --preview / --no-preview   key press popup\n"
"      --repeat / --no-repeat     key repeat  [--repeat-delay MS] [--repeat-rate HZ]\n"
"      --numpad / --no-numpad     open ?123 for numeric fields\n"
"  -t, --theme dark|light\n"
"  -o, --output NAME|auto         output to show on (auto = eDP*/internal panel)\n"
"      --lockscreen auto|always|off  behaviour while the session is locked\n"
"      --no-lock-rule             do not add the Hyprland above_lock layer rule at runtime\n"
"      --lock-button / --no-lock-button  lock/login screen: keyboard behind a round button\n"
"      --greeter                  login-screen mode: permanently locked, button shown\n"
"      --button-offset N          keep N button slots free right of the button (e.g. 3 to\n"
"                                 sit left of a greeter's restart/shutdown buttons)\n"
"      --rotate-cmd CMD           custom rotate command (%%o output, %%t transform 0-3)\n"
"      --hidden / --shown         initial visibility\n"
"  -i, --instance NAME            separate pidfile slatekbd-NAME.pid (for tests)\n"
"      --no-save                  never write the config file\n"
"  -v, --verbose                  log to stderr (repeat for debug output)\n"
"  -h, --help / -V, --version\n"
"      --dump-geometry W H PAGE   print key rectangles (PAGE = main|sym|fn) and exit\n"
"\n"
"Signals: SIGUSR1 hide, SIGUSR2 show, SIGRTMIN toggle, SIGRTMIN+1 lock,\n"
"         SIGRTMIN+2 unlock, SIGTERM/SIGINT/SIGHUP quit.\n"
"\n"
"Pages: abc (QWERTY with Esc/Tab/Caps/Shift/Ctrl/Super/Alt), ?123 (numbers and\n"
"symbols in US positions), Fn (F1-F12, Ins/Home/PgUp/Del/End/PgDn, arrows, media).\n"
"The gear key opens Settings (text size, key shape, popup, modifiers, split,\n"
"mode, auto, heights, theme, repeat, lockscreen, output, rotation, Hide, Quit).\n"
"\n"
"Rotation buttons run `hyprctl eval` (Lua config) or `hyprctl keyword` (legacy) and\n"
"also rotate input:touchdevice. This is a runtime change only: the next\n"
"`hyprctl reload` (e.g. by an autorotate script) re-applies your config.\n"
"\n"
"Lock screen (Hyprland + noctalia):\n"
"  Hyprland only shows layer surfaces above a session lock with a layer rule.\n"
"  slatekbd adds it at runtime (lock_rule=1); to make it permanent add to hyprland.lua:\n"
"    hl.layer_rule({ name = \"slatekbd\", match = { namespace = \"^slatekbd$\" }, above_lock = 2 })\n"
"  (legacy hyprlang: layerrule = above_lock 2, match:namespace ^slatekbd$)\n"
"  and in ~/.config/noctalia/slatekbd.toml:\n"
"    [hooks]\n"
"    session_locked   = \"slatekbd --lock\"\n"
"    session_unlocked = \"slatekbd --unlock\"\n"
"  Start slatekbd from your compositor autostart, e.g. hl.exec_cmd(\"slatekbd\").\n",
	        SLATEKBD_VERSION, cfg);
}

enum {
	OPT_SHOW = 1000, OPT_HIDE, OPT_TOGGLE, OPT_QUIT, OPT_LOCK, OPT_UNLOCK, OPT_STATUS, OPT_LOCKED_START,
	OPT_SPLIT_GAP, OPT_SPLIT_PORTRAIT, OPT_NO_SPLIT_PORTRAIT, OPT_SHAPE, OPT_RADIUS, OPT_FONT, OPT_MODIFIERS,
	OPT_PREVIEW, OPT_NO_PREVIEW, OPT_REPEAT, OPT_NO_REPEAT, OPT_REPEAT_DELAY, OPT_REPEAT_RATE, OPT_LOCKSCREEN,
	OPT_NO_LOCK_RULE, OPT_LOCK_BUTTON, OPT_NO_LOCK_BUTTON, OPT_GREETER, OPT_BUTTON_OFFSET, OPT_HIDDEN, OPT_SHOWN, OPT_NO_SAVE, OPT_DUMP, OPT_SHIFT_CAPS, OPT_NO_SHIFT_CAPS,
	OPT_NUMPAD, OPT_NO_NUMPAD, OPT_ROTATE_CMD,
};

static const struct option long_opts[] = {
	{ "config", required_argument, 0, 'c' },
	{ "mode", required_argument, 0, 'm' },
	{ "auto", no_argument, 0, 'a' },
	{ "no-auto", no_argument, 0, 'A' },
	{ "height-landscape", required_argument, 0, 'L' },
	{ "height", required_argument, 0, 'H' },
	{ "height-portrait", required_argument, 0, 'P' },
	{ "split", no_argument, 0, 's' },
	{ "no-split", no_argument, 0, 'S' },
	{ "split-gap", required_argument, 0, OPT_SPLIT_GAP },
	{ "split-portrait", no_argument, 0, OPT_SPLIT_PORTRAIT },
	{ "no-split-portrait", no_argument, 0, OPT_NO_SPLIT_PORTRAIT },
	{ "shape", required_argument, 0, OPT_SHAPE },
	{ "radius", required_argument, 0, OPT_RADIUS },
	{ "font-scale", required_argument, 0, 'f' },
	{ "font", required_argument, 0, OPT_FONT },
	{ "modifiers", required_argument, 0, OPT_MODIFIERS },
	{ "shift-caps", no_argument, 0, OPT_SHIFT_CAPS },
	{ "no-shift-caps", no_argument, 0, OPT_NO_SHIFT_CAPS },
	{ "preview", no_argument, 0, OPT_PREVIEW },
	{ "no-preview", no_argument, 0, OPT_NO_PREVIEW },
	{ "repeat", no_argument, 0, OPT_REPEAT },
	{ "no-repeat", no_argument, 0, OPT_NO_REPEAT },
	{ "repeat-delay", required_argument, 0, OPT_REPEAT_DELAY },
	{ "repeat-rate", required_argument, 0, OPT_REPEAT_RATE },
	{ "numpad", no_argument, 0, OPT_NUMPAD },
	{ "no-numpad", no_argument, 0, OPT_NO_NUMPAD },
	{ "theme", required_argument, 0, 't' },
	{ "output", required_argument, 0, 'o' },
	{ "lockscreen", required_argument, 0, OPT_LOCKSCREEN },
	{ "no-lock-rule", no_argument, 0, OPT_NO_LOCK_RULE },
	{ "rotate-cmd", required_argument, 0, OPT_ROTATE_CMD },
	{ "hidden", no_argument, 0, OPT_HIDDEN },
	{ "shown", no_argument, 0, OPT_SHOWN },
	{ "instance", required_argument, 0, 'i' },
	{ "no-save", no_argument, 0, OPT_NO_SAVE },
	{ "verbose", no_argument, 0, 'v' },
	{ "help", no_argument, 0, 'h' },
	{ "version", no_argument, 0, 'V' },
	{ "show", no_argument, 0, OPT_SHOW },
	{ "hide", no_argument, 0, OPT_HIDE },
	{ "toggle", no_argument, 0, OPT_TOGGLE },
	{ "quit", no_argument, 0, OPT_QUIT },
	{ "lock", no_argument, 0, OPT_LOCK },
	{ "unlock", no_argument, 0, OPT_UNLOCK },
	{ "status", no_argument, 0, OPT_STATUS },
	{ "locked-start", no_argument, 0, OPT_LOCKED_START },
	{ "lock-button", no_argument, 0, OPT_LOCK_BUTTON },
	{ "no-lock-button", no_argument, 0, OPT_NO_LOCK_BUTTON },
	{ "greeter", no_argument, 0, OPT_GREETER },
	{ "button-offset", required_argument, 0, OPT_BUTTON_OFFSET },
	{ "dump-geometry", no_argument, 0, OPT_DUMP },
	{ 0, 0, 0, 0 },
};

#define MAX_OVR 48
struct ovr { enum cfg_key k; const char *v; };

static const char *icon_name(int icon)
{
	static const char *const n[] = { "", "gear", "bksp", "enter", "shift", "tab", "caps", "left", "right", "up", "down", "keyboard", "lock" };
	return icon >= 0 && icon < (int)(sizeof n / sizeof n[0]) ? n[icon] : "?";
}

static int dump_geometry(const struct config *c, int argc, char **argv, int optind_)
{
	if (argc - optind_ < 3) {
		fprintf(stderr, "usage: slatekbd [options] --dump-geometry W H PAGE\n");
		return 2;
	}
	int W = atoi(argv[optind_]), H = atoi(argv[optind_ + 1]);
	const char *pg = argv[optind_ + 2];
	int page = !strcmp(pg, "sym") || !strcmp(pg, "1") ? PAGE_SYM : !strcmp(pg, "fn") || !strcmp(pg, "2") ? PAGE_FN : PAGE_MAIN;
	float band = c->preview ? layout_band((float)H) : 0;
	struct geo_params p = { (float)W, (float)H, band, page, c->shape == SHAPE_SQUARE ? GEO_SQUARE : GEO_RECT, c->split, c->split_gap };
	static struct geometry g;
	if (W <= 0 || H <= 0 || layout_build(&g, &p) < 0) {
		fprintf(stderr, "bad geometry parameters\n");
		return 2;
	}
	printf("surface %d %d band %.0f kb_h %d unit %.2f row_h %.2f page %s split %d\n", W, (int)(H + band), band, H, g.unit,
	       g.row_h, layout_pages[page].name, (int)c->split);
	for (int i = 0; i < g.n; i++) {
		const struct keybox *b = &g.box[i];
		const struct key *k = b->key;
		const char *name = k->label && *k->label ? k->label : k->code == KEY_SPACE ? "space" : icon_name(k->icon);
		printf("key %d %s code %u row %d half %d x %.1f y %.1f w %.1f h %.1f cx %.1f cy %.1f\n", i, name, k->code,
		       b->row, b->half, b->x, b->y, b->w, b->h, b->x + b->w / 2, b->y + b->h / 2);
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct app *a = &app;
	memset(a, 0, sizeof(*a));
	a->sig_fd = a->timer_fd = -1;
	a->sv.confirm_tile = -1;
	a->scale = 1;
	wl_list_init(&a->outputs);

	struct ovr ovr[MAX_OVR];
	int novr = 0, client_cmd = 0;
	bool dump = false;
	const char *cfg_path = NULL;
#define OVR(key, val) do { if (novr < MAX_OVR) ovr[novr++] = (struct ovr){ key, val }; } while (0)

	int opt;
	while ((opt = getopt_long(argc, argv, "c:m:aAL:H:P:sSf:t:o:i:vhV", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'c': cfg_path = optarg; break;
		case 'm': OVR(CK_MODE, optarg); break;
		case 'a': OVR(CK_AUTO, "1"); break;
		case 'A': OVR(CK_AUTO, "0"); break;
		case 'L': case 'H': OVR(CK_HEIGHT_LAND, optarg); break;
		case 'P': OVR(CK_HEIGHT_PORT, optarg); break;
		case 's': OVR(CK_SPLIT, "1"); break;
		case 'S': OVR(CK_SPLIT, "0"); break;
		case OPT_SPLIT_GAP: OVR(CK_SPLIT_GAP, optarg); break;
		case OPT_SPLIT_PORTRAIT: OVR(CK_SPLIT_PORTRAIT, "1"); break;
		case OPT_NO_SPLIT_PORTRAIT: OVR(CK_SPLIT_PORTRAIT, "0"); break;
		case OPT_SHAPE: OVR(CK_KEY_SHAPE, optarg); break;
		case OPT_RADIUS: OVR(CK_KEY_RADIUS, optarg); break;
		case 'f': OVR(CK_FONT_SCALE, optarg); break;
		case OPT_FONT: OVR(CK_FONT, optarg); break;
		case OPT_MODIFIERS: OVR(CK_MODIFIERS, optarg); break;
		case OPT_SHIFT_CAPS: OVR(CK_SHIFT_CAPS, "1"); break;
		case OPT_NO_SHIFT_CAPS: OVR(CK_SHIFT_CAPS, "0"); break;
		case OPT_PREVIEW: OVR(CK_PREVIEW, "1"); break;
		case OPT_NO_PREVIEW: OVR(CK_PREVIEW, "0"); break;
		case OPT_REPEAT: OVR(CK_REPEAT, "1"); break;
		case OPT_NO_REPEAT: OVR(CK_REPEAT, "0"); break;
		case OPT_REPEAT_DELAY: OVR(CK_REPEAT_DELAY, optarg); break;
		case OPT_REPEAT_RATE: OVR(CK_REPEAT_RATE, optarg); break;
		case OPT_NUMPAD: OVR(CK_AUTO_NUMPAD, "1"); break;
		case OPT_NO_NUMPAD: OVR(CK_AUTO_NUMPAD, "0"); break;
		case 't': OVR(CK_THEME, optarg); break;
		case 'o': OVR(CK_OUTPUT, optarg); break;
		case OPT_LOCKSCREEN: OVR(CK_LOCKSCREEN, optarg); break;
		case OPT_NO_LOCK_RULE: OVR(CK_LOCK_RULE, "0"); break;
		case OPT_ROTATE_CMD: OVR(CK_ROTATE_CMD, optarg); break;
		case OPT_HIDDEN: OVR(CK_START, "hidden"); break; /* run-only: not saved */
		case OPT_SHOWN: OVR(CK_START, "shown"); break;
		case 'i': snprintf(a->instance, sizeof a->instance, "%s", optarg); break;
		case OPT_NO_SAVE: a->no_save = true; break;
		case 'v': a->verbose++; break;
		case 'h': usage(stdout); return 0;
		case 'V': printf("slatekbd %s\n", SLATEKBD_VERSION); return 0;
		case OPT_LOCKED_START: a->locked_start = true; break;
		case OPT_LOCK_BUTTON: OVR(CK_LOCK_BUTTON, "1"); break;
		case OPT_NO_LOCK_BUTTON: OVR(CK_LOCK_BUTTON, "0"); break;
		case OPT_GREETER: a->greeter = true; a->no_save = true; break;
		case OPT_BUTTON_OFFSET: a->button_offset = atoi(optarg) < 0 ? 0 : atoi(optarg) > 20 ? 20 : atoi(optarg); break;
		case OPT_DUMP: dump = true; break;
		case OPT_SHOW: case OPT_HIDE: case OPT_TOGGLE: case OPT_QUIT: case OPT_LOCK: case OPT_UNLOCK: case OPT_STATUS:
			client_cmd = opt;
			break;
		default: usage(stderr); return 2;
		}
	}
	for (const char *p = a->instance; *p; p++) {
		if (*p == '/' ) {
			fprintf(stderr, "slatekbd: invalid instance name\n");
			return 2;
		}
	}

	/* ---- client mode: signal the running instance ---- */
	if (client_cmd) {
		if (client_cmd == OPT_STATUS) {
			pid_t pid = instance_running(a->instance);
			if (pid > 0) {
				printf("running (pid %d)\n", (int)pid);
				return 0;
			}
			printf("not running\n");
			return 1;
		}
		int sig = client_cmd == OPT_SHOW ? SIGUSR2 : client_cmd == OPT_HIDE ? SIGUSR1 : client_cmd == OPT_TOGGLE ? SIGRTMIN
		        : client_cmd == OPT_QUIT ? SIGTERM : client_cmd == OPT_LOCK ? SIGRTMIN + 1 : SIGRTMIN + 2;
		int r = instance_signal(a->instance, sig);
		if (r == 0) return 0;
		if (r == 1 && client_cmd == OPT_LOCK) {
			char *args[16];
			int n = 0;
			args[n++] = "slatekbd";
			/* no --shown: lock mode decides visibility; lock_exit picks the post-unlock state */
			args[n++] = "--locked-start";
			if (a->instance[0]) { args[n++] = "-i"; args[n++] = a->instance; }
			if (cfg_path) { args[n++] = "-c"; args[n++] = (char *)cfg_path; }
			args[n] = NULL;
			if (instance_spawn_detached(args) == 0) return 0;
			fprintf(stderr, "slatekbd: could not start an instance\n");
			return 1;
		}
		if (r == 1) {
			if (client_cmd == OPT_UNLOCK) return 0; /* nothing to unlock */
			fprintf(stderr, "slatekbd: not running\n");
			return 1;
		}
		perror("slatekbd: kill");
		return 1;
	}

	/* ---- configuration ---- */
	config_defaults(&a->cfg);
	config_verbose = 1;
	for (int i = 0; i < novr; i++) a->cfg.overridden |= 1ULL << ovr[i].k;
	if (cfg_path) snprintf(a->config_path, sizeof a->config_path, "%s", cfg_path);
	else config_default_path(a->config_path, sizeof a->config_path);
	if (config_load(&a->cfg, a->config_path) < 0)
		fprintf(stderr, "slatekbd: warning: cannot read %s\n", a->config_path);
	for (int i = 0; i < novr; i++) {
		int r = config_set(&a->cfg, config_key_name(ovr[i].k), ovr[i].v);
		if (r) {
			fprintf(stderr, "slatekbd: invalid value '%s' for %s\n", ovr[i].v, config_key_name(ovr[i].k));
			config_free(&a->cfg);
			return 2;
		}
	}
	config_clamp(&a->cfg);
	if (cfg_path && !strcmp(cfg_path, "/dev/null")) a->no_save = true;

	char err[256];
	if (!layout_validate(err, sizeof err)) {
		fprintf(stderr, "slatekbd: internal layout error: %s\n", err);
		config_free(&a->cfg);
		return 1;
	}
	if (dump) {
		int r = dump_geometry(&a->cfg, argc, argv, optind);
		config_free(&a->cfg);
		return r;
	}

	/* ---- single instance ---- */
	pid_t owner = 0;
	int pid_fd = instance_acquire(a->instance, &owner);
	if (pid_fd == -1) {
		fprintf(stderr, "slatekbd: already running (pid %d)\n", (int)owner);
		config_free(&a->cfg);
		return 1;
	}
	if (pid_fd < 0) fprintf(stderr, "slatekbd: warning: cannot create the pidfile; single-instance check disabled\n");

	int rc = 1;
	if (loop_init(a) < 0) {
		perror("slatekbd: signalfd/timerfd");
		goto out;
	}
	a->render = render_create();
	if (wl_init(a) < 0) goto out;
	a->km = keymap_create(a);
	if (!a->km || keymap_attach(a->km) < 0) {
		WARN(a, "could not create the xkb keymap / virtual keyboard");
		goto out;
	}
	app_init_input(a);
	lock_init(a);
	if (a->cfg.auto_show) im_acquire(a);
	wl_display_roundtrip(a->display); /* learn `unavailable`, lock state, output names */
	LOG(a, "started (pid %d, auto=%d im=%s, outputs: %s)", (int)getpid(), a->cfg.auto_show,
	    a->im ? "ok" : !a->cfg.auto_show ? "off" : (a->im_mgr ? "unavailable" : "unsupported"), app_output_name(a));

	switch (a->cfg.start) {
	case START_SHOWN: a->vis.manual = OV_SHOW; break;
	case START_HIDDEN: a->vis.manual = OV_HIDE; break;
	default: a->vis.manual = (a->cfg.auto_show && a->vis.im_avail) ? OV_NONE : OV_SHOW; break;
	}
	app_check_orientation(a);
	app_update_size(a);
	if (a->cfg.lock_rule && !a->locked_start && !a->greeter) rotate_lock_rule(a);
	if (a->locked_start || a->greeter) app_lock(a, true);
	app_update_visibility(a);

	rc = loop_run(a) < 0 ? 1 : 0;

	/* ---- clean shutdown ---- */
	LOG(a, "shutting down");
	app_release_all(a);
	if (a->display) {
		wl_display_flush(a->display);
		wl_display_roundtrip(a->display);
	}
	if (timer_armed(a, T_SAVE)) app_save_now(a);
out:
	im_release(a);
	lock_fini(a);
	keymap_destroy(a->km);
	a->km = NULL;
	if (a->display) wl_fini(a);
	render_destroy(a->render);
	config_free(&a->cfg);
	loop_fini(a);
	instance_release(a->instance, pid_fd);
	return rc;
}
