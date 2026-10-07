/* visibility.h - decides whether the keyboard should be shown (pure logic). */
#ifndef SLATEKBD_VISIBILITY_H
#define SLATEKBD_VISIBILITY_H

#include <stdbool.h>

enum manual_ov { OV_NONE, OV_SHOW, OV_HIDE };

struct vis {
	bool auto_on;     /* cfg.auto_show */
	bool im_avail;    /* input method bound and not unavailable */
	bool im_active;   /* applied state after `done` */
	int manual;       /* enum manual_ov */
	bool locked;
	int lock_mode;    /* enum lock_mode */
	bool lock_hidden; /* manual hide while locked */
	bool lock_shown;  /* manual show while locked (lockscreen=off) */
	bool lock_im;     /* the lock surface activated the input method during this lock */
	bool lock_button; /* cfg.lock_button: while locked a button is shown; the IM is ignored */
};

enum vis_action {
	VA_NONE,
	VA_SHOW,        /* show now */
	VA_HIDE,        /* hide now */
	VA_ARM_HIDE,    /* start the hide debounce timer */
	VA_CANCEL_HIDE, /* cancel a pending debounced hide */
};

#define HIDE_DEBOUNCE_MS 150

/* Desired visibility given the current state. */
bool vis_want(const struct vis *v, bool currently_visible);
/* Next step given the current visibility and whether the debounce timer is armed. */
enum vis_action vis_step(const struct vis *v, bool visible, bool hide_armed);

/* Event helpers (keep the state rules in one place) */
/* IM state applied on `done`; activated = an `activate` event arrived in this batch */
void vis_im_done(struct vis *v, bool active, bool activated);
void vis_manual(struct vis *v, int ov);         /* OV_SHOW / OV_HIDE from signal/CLI/UI */
void vis_set_locked(struct vis *v, bool locked);

#endif
