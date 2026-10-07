/* visibility.c - show/hide rules (DESIGN §8). Pure logic. */
#include "visibility.h"

#include "config.h"

bool vis_want(const struct vis *v, bool cur)
{
	if (v->locked) {
		switch (v->lock_mode) {
		case LOCK_ALWAYS: return true;
		case LOCK_AUTO:
			/* Auto-hide on the lock screen only once the lock surface has proven it
			 * drives text-input; otherwise stay up so the password can always be typed. */
			if (v->lock_hidden) return false;
			if (v->lock_button) return true;
			if (v->auto_on && v->im_avail && v->lock_im) return v->im_active;
			return true;
		default: return v->lock_shown && !v->lock_hidden;
		}
	}
	if (v->manual == OV_SHOW) return true;
	if (v->manual == OV_HIDE) return false;
	if (v->auto_on && v->im_avail) return v->im_active;
	return cur;
}

/* Only a hide caused by the input method is debounced. */
static bool hide_is_from_im(const struct vis *v)
{
	if (!v->auto_on || !v->im_avail) return false;
	if (v->locked) return v->lock_mode == LOCK_AUTO && v->lock_im && !v->lock_hidden && !v->lock_button;
	return v->manual == OV_NONE;
}

enum vis_action vis_step(const struct vis *v, bool visible, bool hide_armed)
{
	bool want = vis_want(v, visible);
	if (want) {
		if (hide_armed) return visible ? VA_CANCEL_HIDE : VA_SHOW;
		return visible ? VA_NONE : VA_SHOW;
	}
	if (!visible) return hide_armed ? VA_CANCEL_HIDE : VA_NONE;
	if (hide_is_from_im(v)) return hide_armed ? VA_NONE : VA_ARM_HIDE;
	return VA_HIDE;
}

void vis_im_done(struct vis *v, bool active, bool activated)
{
	/* Newest event wins. A fresh `activate` while already active (the client re-sent
	 * text_input.enable, e.g. GTK/Qt asking for the OSK on a tap, or a disable+enable
	 * focus change in one batch) also counts as a request to show. */
	if (active != v->im_active || (active && activated)) v->manual = OV_NONE;
	if (v->locked && activated) {
		v->lock_im = true;
		v->lock_hidden = false;
	}
	v->im_active = active;
}

void vis_manual(struct vis *v, int ov)
{
	if (v->locked) {
		v->lock_hidden = (ov == OV_HIDE);
		v->lock_shown = (ov == OV_SHOW);
		return;
	}
	v->manual = ov;
}

void vis_set_locked(struct vis *v, bool locked)
{
	if (v->locked == locked) return;
	v->locked = locked;
	v->lock_hidden = false;
	v->lock_shown = false;
	v->lock_im = false;
}
