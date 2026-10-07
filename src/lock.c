/* lock.c - lockscreen support.
 * Sources: hyprland_lock_notifier_v1 (locked/unlocked) and `slatekbd --lock/--unlock`
 * (SIGRTMIN+1 / SIGRTMIN+2, e.g. from noctalia's session_locked/session_unlocked hooks).
 * Hyprland only draws layer surfaces above a session lock when a layer rule
 * `above_lock = 2` matches them; with lock_rule=1 we add that rule at runtime. */
#include "lock.h"

#include <stdlib.h>

#include "app.h"
#include "hyprland-lock-notify-v1-client-protocol.h"
#include "rotate.h"
#include "wayland.h"

static void on_locked(void *data, struct hyprland_lock_notification_v1 *n)
{
	(void)n;
	struct app *a = data;
	LOG(a, "session locked (hyprland_lock_notifier)");
	app_lock(a, true);
}

static void on_unlocked(void *data, struct hyprland_lock_notification_v1 *n)
{
	(void)n;
	struct app *a = data;
	LOG(a, "session unlocked (hyprland_lock_notifier)");
	app_lock(a, false);
}

static const struct hyprland_lock_notification_v1_listener lock_listener = {
	.locked = on_locked,
	.unlocked = on_unlocked,
};

void lock_init(struct app *a)
{
	if (!a->lock_notifier || a->lock_notif) return;
	a->lock_notif = hyprland_lock_notifier_v1_get_lock_notification(a->lock_notifier);
	hyprland_lock_notification_v1_add_listener(a->lock_notif, &lock_listener, a);
}

void lock_fini(struct app *a)
{
	if (a->lock_notif) hyprland_lock_notification_v1_destroy(a->lock_notif);
	a->lock_notif = NULL;
}

void lock_enter(struct app *a)
{
	if (a->locked) return;
	a->pre_lock_visible = a->visible;
	app_release_all(a);
	if (a->settings_open) app_open_settings(a, false);
	timer_cancel(a, T_CONFIRM);
	a->sv.confirm_tile = -1;
	a->locked = true;
	vis_set_locked(&a->vis, true);
	if (a->cfg.lock_button) app_set_collapsed(a, true);
	timer_cancel(a, T_HIDE);
	app_update_size(a); /* overlay (no exclusive zone), no preview band */
	bool rule = !a->greeter && a->cfg.lock_rule && getenv("HYPRLAND_INSTANCE_SIGNATURE") && rotate_lock_rule(a);
	app_update_visibility(a);
	/* Recreate once, after the rule is in place (hyprctl reaped); the timer is only a fallback. */
	a->lock_recreate_pending = rule && a->visible;
	a->lock_recreate_waits = 0;
	if (a->lock_recreate_pending) timer_arm(a, T_LOCK_RECREATE, 300);
	app_mark_dirty(a);
}

void lock_exit(struct app *a)
{
	if (!a->locked || a->greeter) return;
	app_release_all(a);
	app_set_collapsed(a, false);
	a->locked = false;
	vis_set_locked(&a->vis, false);
	timer_cancel(a, T_LOCK_RECREATE);
	a->lock_recreate_pending = false;
	if (a->locked_start) {
		/* started by `slatekbd --lock` only for the lock screen: go away again */
		LOG(a, "unlocked: exiting (instance was started for the lock screen)");
		app_quit(a);
		return;
	}
	if (a->vis.manual == OV_NONE && !(a->vis.auto_on && a->vis.im_avail)) {
		a->vis.manual = a->pre_lock_visible ? OV_SHOW : OV_HIDE;
	}
	app_update_size(a); /* restores mode, preview band and gear from cfg */
	app_update_visibility(a);
	app_mark_dirty(a);
}

void lock_rule_done(struct app *a)
{
	if (!a->lock_recreate_pending) return;
	a->lock_recreate_pending = false;
	timer_cancel(a, T_LOCK_RECREATE);
	/* The rule is matched when a layer maps: recreate the surface (once) so it applies. */
	if (a->locked && a->visible) a->need_recreate = true;
}

void lock_rule_timeout(struct app *a)
{
	if (!a->lock_recreate_pending) return;
	/* hyprctl still running: wait for its reap (up to ~3 s) rather than recreating twice */
	if (rotate_lock_rule_pending() && ++a->lock_recreate_waits < 10) {
		timer_arm(a, T_LOCK_RECREATE, 300);
		return;
	}
	lock_rule_done(a);
}
