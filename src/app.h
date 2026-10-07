/* app.h - global application state shared by all modules. */
#ifndef SLATEKBD_APP_H
#define SLATEKBD_APP_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "config.h"
#include "input.h"
#include "layout.h"
#include "visibility.h"

struct shm_buffer;
struct render;

enum timer_id {
	T_REPEAT,        /* key repeat */
	T_HIDE,          /* hide debounce */
	T_CONFIRM,       /* settings 2-step confirm timeout */
	T_SAVE,          /* config save debounce */
	T_SET_REPEAT,    /* settings stepper auto-repeat */
	T_LOCK_RECREATE, /* recreate the surface after the lock rule was applied */
	T_COUNT
};

struct output {
	struct wl_list link;
	struct app *app;
	struct wl_output *wl;
	struct zxdg_output_v1 *xdg;
	uint32_t global_name;
	char name[32];
	int scale;
	int lw, lh;      /* logical size (xdg_output) */
	int mode_w, mode_h;
	int transform;   /* wl_output.geometry transform (not re-sent on rotation by Hyprland) */
	bool done;
};

#define MAX_TOUCH 10

struct touch_point {
	bool used;
	int32_t id;
	float x, y;
	uint32_t t;
	bool pend_motion;     /* coalesced until wl_touch.frame */
};

struct settings_view {
	int page;           /* settings page index */
	int npages;
	int rows, cols;
	int confirm_tile;   /* tile awaiting a second tap (-1 none) */
	/* active press */
	bool pressing;
	bool press_ptr;
	int32_t press_id;
	int press_tile;     /* tile index or a NAV_* id */
	int press_zone;     /* stepper: -1 minus, +1 plus, 0 middle */
	bool press_inside;
	float px, py;
	char status[96];    /* transient status line (e.g. rotate failed) */
};

struct app {
	struct config cfg;
	char config_path[1024];
	bool no_save;
	int verbose;
	char instance[64];
	bool locked_start;
	bool greeter;         /* --greeter: permanent lock mode for a login screen */
	int button_offset;    /* --button-offset: button slots kept free right of our button */

	/* Wayland globals */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	uint32_t compositor_ver;
	struct wl_shm *shm;
	struct wl_seat *seat;
	uint32_t seat_name;
	struct wl_touch *touch;
	struct wl_pointer *pointer;
	uint32_t pointer_ver;
	struct zwlr_layer_shell_v1 *layer_shell;
	uint32_t layer_shell_ver;
	struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
	struct zwp_input_method_manager_v2 *im_mgr;
	struct wp_fractional_scale_manager_v1 *frac_mgr;
	struct wp_viewporter *viewporter;
	struct zxdg_output_manager_v1 *xdg_out_mgr;
	struct hyprland_lock_notifier_v1 *lock_notifier;
	struct hyprland_lock_notification_v1 *lock_notif;
	struct wl_list outputs;

	/* surface */
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *frac;
	struct output *layer_output;  /* output passed to get_layer_surface (may be NULL) */
	struct output *entered;       /* output the surface entered */
	struct wl_callback *frame_cb;
	bool configured;
	int width;            /* configured width (logical) */
	int req_height;       /* requested total height H+B */
	int kb_height;        /* H */
	int band;             /* B */
	int frac120;          /* fractional preferred scale * 120 (0 = none) */
	int buf_scale;        /* integer preferred buffer scale */
	double scale;         /* effective render scale */
	int transform;        /* last known output transform 0..3 */
	bool portrait;
	struct shm_buffer *buffers[2];
	struct render *render;
	bool dirty;
	bool need_recreate;

	/* pointer */
	bool ptr_inside;
	float ptr_x, ptr_y;
	bool ptr_pend_down, ptr_pend_up, ptr_pend_motion;
	bool ptr_down;
	uint32_t ptr_t;

	struct touch_point tp[MAX_TOUCH];

	/* keyboard model */
	struct keymap *km;
	struct input in;
	struct geometry geo;
	int page;
	bool settings_open;
	struct settings_view sv;

	/* visibility */
	struct vis vis;
	bool visible;
	struct zwp_input_method_v2 *im;
	bool im_pending_active, im_pending_valid;
	bool im_pending_activated; /* an `activate` arrived since the last `done` */
	uint32_t im_pending_purpose, im_pending_hint;
	bool im_was_numeric;

	/* lock */
	bool locked;
	bool pre_lock_visible;
	bool lock_rule_applied;
	bool lock_recreate_pending; /* one surface recreate owed once the lock rule is applied */
	bool collapsed;             /* lock/login screen: only the round keyboard button is shown */
	bool btn_pressing;
	bool btn_ptr;
	int32_t btn_id;
	int lock_recreate_waits;

	/* loop */
	int sig_fd;
	int timer_fd;
	uint64_t timer_deadline[T_COUNT]; /* ms monotonic, 0 = off */
	bool quit;
	int exit_code;
};

/* app.c */
uint32_t now_ms(void);
void app_log(struct app *a, int level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define LOG(a, ...) app_log((a), 1, __VA_ARGS__)
#define DBG(a, ...) app_log((a), 2, __VA_ARGS__)
#define WARN(a, ...) app_log((a), 0, __VA_ARGS__)

void app_init_input(struct app *a);
void app_sync_input_cfg(struct app *a);
/* Recompute H/B for orientation/lock/preview; resize the surface if mapped. */
void app_update_size(struct app *a);
/* Rebuild geometry from current width/height/page/split and mark dirty. */
void app_relayout(struct app *a);
void app_set_page(struct app *a, int page);
void app_open_settings(struct app *a, bool open);
void app_mark_dirty(struct app *a);
void app_update_visibility(struct app *a);
void app_show(struct app *a);
void app_hide(struct app *a);
void app_manual(struct app *a, int ov);   /* enum manual_ov; toggle handled by caller */
void app_toggle(struct app *a);
void app_quit(struct app *a);
void app_release_all(struct app *a);
void app_config_changed(struct app *a, enum cfg_key k);
void app_schedule_save(struct app *a);
void app_save_now(struct app *a);
void app_on_timer(struct app *a, enum timer_id id);
void app_set_auto(struct app *a, bool on);
void app_im_done(struct app *a, bool active, bool activated, uint32_t hint, uint32_t purpose);
void app_im_unavailable(struct app *a);
void app_lock(struct app *a, bool locked);
/* Lock/login screen button: collapse to the button or expand to the keyboard. */
void app_set_collapsed(struct app *a, bool collapsed);
/* Button geometry (logical px): diameter, right and bottom margin. */
#define BTN_D 36
#define BTN_PITCH 44
#define BTN_MARGIN 16
bool app_effective_split(const struct app *a);
bool app_overlay_effective(const struct app *a);
bool app_preview_effective(const struct app *a);
const char *app_output_name(const struct app *a);
struct output *app_ref_output(const struct app *a);
void app_check_orientation(struct app *a);

/* loop.c */
int loop_init(struct app *a);
void loop_fini(struct app *a);
void timer_arm(struct app *a, enum timer_id id, uint32_t ms);
void timer_cancel(struct app *a, enum timer_id id);
bool timer_armed(const struct app *a, enum timer_id id);
void loop_handle_signals(struct app *a);
void loop_handle_timers(struct app *a);
int loop_run(struct app *a);

#endif
