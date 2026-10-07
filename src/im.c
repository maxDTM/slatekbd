/* im.c - input-method-unstable-v2. activate / deactivate / content_type are
 * double-buffered and applied on `done`. grab_keyboard is never called, so the
 * virtual keyboard keeps reaching the focused surface (including a lock screen). */
#include "im.h"

#include "app.h"
#include "input-method-unstable-v2-client-protocol.h"

/* zwp_text_input_v3.content_purpose values (input-method-v2 reuses them) */
enum { PURPOSE_DIGITS = 2, PURPOSE_NUMBER = 3, PURPOSE_PHONE = 4, PURPOSE_PIN = 9 };

bool im_purpose_numeric(unsigned p)
{
	return p == PURPOSE_DIGITS || p == PURPOSE_NUMBER || p == PURPOSE_PHONE || p == PURPOSE_PIN;
}

static void im_activate(void *data, struct zwp_input_method_v2 *im)
{
	(void)im;
	struct app *a = data;
	a->im_pending_valid = true;
	a->im_pending_active = true;
	a->im_pending_activated = true;
	/* content type resets on activate */
	a->im_pending_hint = 0;
	a->im_pending_purpose = 0;
}

static void im_deactivate(void *data, struct zwp_input_method_v2 *im)
{
	(void)im;
	struct app *a = data;
	a->im_pending_valid = true;
	a->im_pending_active = false;
}

static void im_surrounding(void *data, struct zwp_input_method_v2 *im, const char *text, uint32_t cursor, uint32_t anchor) { (void)anchor; (void)data; (void)im; (void)text; (void)cursor; }
static void im_cause(void *data, struct zwp_input_method_v2 *im, uint32_t cause) { (void)data; (void)im; (void)cause; }

static void im_content_type(void *data, struct zwp_input_method_v2 *im, uint32_t hint, uint32_t purpose)
{
	(void)im;
	struct app *a = data;
	a->im_pending_hint = hint;
	a->im_pending_purpose = purpose;
}

static void im_done(void *data, struct zwp_input_method_v2 *im)
{
	(void)im;
	struct app *a = data;
	bool active = a->im_pending_valid ? a->im_pending_active : a->vis.im_active;
	bool activated = a->im_pending_activated;
	a->im_pending_valid = false;
	a->im_pending_activated = false;
	DBG(a, "im done: active=%d activated=%d purpose=%u hint=%u", active, activated, a->im_pending_purpose,
	    a->im_pending_hint);
	app_im_done(a, active, activated, a->im_pending_hint, a->im_pending_purpose);
}

static void im_unavailable(void *data, struct zwp_input_method_v2 *im)
{
	(void)im;
	struct app *a = data;
	WARN(a, "input method unavailable (another input method, e.g. `wvkbd --auto` or fcitx5, owns the seat); "
	        "auto show/hide disabled");
	im_release(a);
	app_im_unavailable(a);
}

static const struct zwp_input_method_v2_listener im_listener = {
	.activate = im_activate,
	.deactivate = im_deactivate,
	.surrounding_text = im_surrounding,
	.text_change_cause = im_cause,
	.content_type = im_content_type,
	.done = im_done,
	.unavailable = im_unavailable,
};

void im_acquire(struct app *a)
{
	if (a->im || !a->im_mgr || !a->seat) return;
	a->im = zwp_input_method_manager_v2_get_input_method(a->im_mgr, a->seat);
	zwp_input_method_v2_add_listener(a->im, &im_listener, a);
	a->im_pending_valid = false;
	a->im_pending_activated = false;
	a->vis.im_avail = true;
	a->vis.im_active = false;
}

void im_release(struct app *a)
{
	if (a->im) zwp_input_method_v2_destroy(a->im);
	a->im = NULL;
	a->vis.im_avail = false;
	a->vis.im_active = false;
}
