/* test_visibility: truth table over auto/IM/manual/lock + the debounce cancel. */
#include <string.h>

#include "config.h"
#include "t.h"
#include "visibility.h"

int main(void)
{
	struct vis v;
	memset(&v, 0, sizeof v);

	/* auto on, IM available: follows the IM */
	v.auto_on = true;
	v.im_avail = true;
	CHECK(!vis_want(&v, true));
	vis_im_done(&v, true, true);
	CHECK(vis_want(&v, false));
	CHECK(vis_step(&v, false, false) == VA_SHOW);
	vis_im_done(&v, false, false);
	CHECK(vis_step(&v, true, false) == VA_ARM_HIDE); /* debounced */
	CHECK(vis_step(&v, true, true) == VA_NONE);
	vis_im_done(&v, true, true);                           /* activate during the debounce */
	CHECK(vis_step(&v, true, true) == VA_CANCEL_HIDE);

	/* manual override until the next IM change */
	vis_manual(&v, OV_HIDE);
	CHECK(vis_step(&v, true, false) == VA_HIDE);     /* immediate */
	vis_im_done(&v, true, false);                    /* unchanged state, no activate: manual stays */
	CHECK(!vis_want(&v, false));
	vis_im_done(&v, false, false);                          /* change clears manual */
	CHECK(v.manual == OV_NONE);
	/* hidden while a field is focused, then the field is tapped again (re-enable -> activate) */
	vis_im_done(&v, true, true);
	vis_manual(&v, OV_HIDE);
	CHECK(!vis_want(&v, true));
	vis_im_done(&v, true, false);                    /* plain done (surrounding text etc.) */
	CHECK(!vis_want(&v, true));
	vis_im_done(&v, true, true);                     /* activate again: show */
	CHECK(v.manual == OV_NONE && vis_want(&v, false));
	vis_im_done(&v, false, false);
	vis_manual(&v, OV_SHOW);
	CHECK(vis_want(&v, false));
	vis_im_done(&v, true, true);
	vis_im_done(&v, false, false);
	CHECK(!vis_want(&v, true));

	/* auto off: keeps the current state, manual changes it */
	memset(&v, 0, sizeof v);
	v.im_avail = true;
	CHECK(vis_want(&v, true) && !vis_want(&v, false));
	vis_manual(&v, OV_SHOW);
	CHECK(vis_want(&v, false));
	CHECK(vis_step(&v, true, false) == VA_NONE);
	vis_manual(&v, OV_HIDE);
	CHECK(vis_step(&v, true, false) == VA_HIDE);
	/* auto on but IM unavailable: like auto off */
	memset(&v, 0, sizeof v);
	v.auto_on = true;
	CHECK(vis_want(&v, true) && !vis_want(&v, false));

	/* lock modes */
	memset(&v, 0, sizeof v);
	v.auto_on = true;
	v.im_avail = true;
	v.lock_mode = LOCK_AUTO;
	vis_set_locked(&v, true);
	CHECK(vis_want(&v, false));                      /* locking shows it */
	CHECK(vis_step(&v, true, false) == VA_NONE);
	vis_manual(&v, OV_HIDE);                         /* manual hide while locked */
	CHECK(!vis_want(&v, true));
	CHECK(vis_step(&v, true, false) == VA_HIDE);     /* not debounced */
	vis_manual(&v, OV_SHOW);
	CHECK(vis_want(&v, false));
	vis_set_locked(&v, false);
	CHECK(!v.lock_hidden);
	CHECK(!vis_want(&v, true));                      /* back to the IM (inactive) */

	/* lock screen auto-hide: only after the lock surface activated the IM */
	memset(&v, 0, sizeof v);
	v.auto_on = true;
	v.im_avail = true;
	v.lock_mode = LOCK_AUTO;
	vis_set_locked(&v, true);
	vis_im_done(&v, false, false);                   /* no text-input from the lock yet */
	CHECK(vis_want(&v, false));                      /* stays up */
	vis_im_done(&v, true, true);                     /* password field activates */
	CHECK(v.lock_im && vis_want(&v, false));
	vis_im_done(&v, false, false);                   /* field lost focus */
	CHECK(!vis_want(&v, true));
	CHECK(vis_step(&v, true, false) == VA_ARM_HIDE); /* debounced like unlocked */
	vis_im_done(&v, true, true);
	CHECK(vis_want(&v, false));
	vis_manual(&v, OV_HIDE);
	CHECK(vis_step(&v, true, false) == VA_HIDE);
	vis_im_done(&v, true, true);                     /* tap the field again: back */
	CHECK(vis_want(&v, false));
	v.auto_on = false;                               /* auto off: always up while locked */
	vis_im_done(&v, false, false);
	CHECK(vis_want(&v, false));
	vis_set_locked(&v, false);
	CHECK(!v.lock_im);
	v.auto_on = true;

	v.lock_mode = LOCK_ALWAYS;
	vis_set_locked(&v, true);
	vis_manual(&v, OV_HIDE);
	CHECK(vis_want(&v, false));                      /* always visible */
	vis_set_locked(&v, false);

	v.lock_mode = LOCK_OFF;
	vis_set_locked(&v, true);
	CHECK(!vis_want(&v, true));
	vis_manual(&v, OV_SHOW);
	CHECK(vis_want(&v, false));
	vis_set_locked(&v, false);
	T_DONE();
}
