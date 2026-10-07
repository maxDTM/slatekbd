/* input.c - multi-touch slots, modifier state machine, repeat, actions. Pure logic. */
#include "input.h"

#include <linux/input-event-codes.h>
#include <string.h>

void input_init(struct input *in, const struct input_ops *ops, void *ud)
{
	memset(in, 0, sizeof(*in));
	in->ops = ops;
	in->ud = ud;
	in->repeat_slot = -1;
	in->repeat_on = true;
	in->repeat_delay = 400;
	in->repeat_rate = 25;
	in->shift_caps = true;
}

static void emit(struct input *in, uint16_t code, bool down)
{
	if (in->ops && in->ops->key) in->ops->key(in->ud, code, down);
}

static void changed(struct input *in)
{
	if (in->ops && in->ops->changed) in->ops->changed(in->ud);
}

static void arm(struct input *in, uint32_t ms)
{
	if (in->ops && in->ops->timer) in->ops->timer(in->ud, ms);
}

static struct slot *find_slot(struct input *in, int32_t id, bool ptr)
{
	for (int i = 0; i < MAX_SLOTS; i++) {
		struct slot *s = &in->slot[i];
		if (s->used && s->is_pointer == ptr && (ptr || s->id == id)) return s;
	}
	return NULL;
}

bool input_has_slot(const struct input *in, int32_t id, bool ptr)
{
	return find_slot((struct input *)in, id, ptr) != NULL;
}

int input_active_slots(const struct input *in)
{
	int n = 0;
	for (int i = 0; i < MAX_SLOTS; i++) n += in->slot[i].used;
	return n;
}

bool input_shift_active(const struct input *in)
{
	return in->mod[MOD_SHIFT].st != MS_OFF;
}

bool input_any_mod(const struct input *in)
{
	for (int m = 0; m < MOD_COUNT; m++)
		if (in->mod[m].st != MS_OFF) return true;
	return false;
}

static void mod_press(struct input *in, struct modifier *m, uint16_t code)
{
	if (m->code) return;
	m->code = code;
	emit(in, code, true);
}

static void mod_release(struct input *in, struct modifier *m)
{
	if (!m->code) return;
	emit(in, m->code, false);
	m->code = 0;
}

static void caps_tap(struct input *in)
{
	emit(in, KEY_CAPSLOCK, true);
	emit(in, KEY_CAPSLOCK, false);
	in->caps = !in->caps;
}

/* Send a character key as press+release, with implied Shift for KF_SHIFTED glyphs. */
static void tap_char(struct input *in, const struct key *k)
{
	bool need_shift = (k->flags & KF_SHIFTED) && !input_shift_active(in);
	if (need_shift) emit(in, KEY_LEFTSHIFT, true);
	emit(in, k->code, true);
	emit(in, k->code, false);
	if (need_shift) emit(in, KEY_LEFTSHIFT, false);
}

static void stop_repeat(struct input *in)
{
	if (in->repeat_slot >= 0) {
		in->repeat_slot = -1;
		arm(in, 0);
	}
}

static void mark_chorded(struct input *in)
{
	for (int m = 0; m < MOD_COUNT; m++)
		if (in->mod[m].held > 0) in->mod[m].chorded = true;
}

static void mod_down(struct input *in, const struct key *k, uint32_t t)
{
	struct modifier *m = &in->mod[k->mod];
	m->held++;
	if (m->held > 1) return; /* second finger on the other copy (e.g. both Shifts) */
	m->chorded = false;
	if (in->hold_mode) {
		mod_press(in, m, k->code);
		m->st = MS_HELD;
		return;
	}
	m->prev = m->st;
	if (m->st == MS_OFF) mod_press(in, m, k->code);
	m->st = MS_HELD;
	(void)t;
}

static void mod_up(struct input *in, const struct key *k, struct slot *s, uint32_t t)
{
	struct modifier *m = &in->mod[k->mod];
	if (m->held > 0) m->held--;
	if (m->held > 0) return;

	if (in->hold_mode) {
		mod_release(in, m);
		m->st = MS_OFF;
		if (k->mod == MOD_SHIFT && in->shift_caps) {
			if (!m->chorded && t - s->down_ms < DOUBLE_TAP_MS) {
				if (in->shift_last_tap_ms && s->down_ms - in->shift_last_tap_ms < DOUBLE_TAP_MS) {
					caps_tap(in);
					in->shift_last_tap_ms = 0;
				} else {
					in->shift_last_tap_ms = t ? t : 1;
				}
			} else {
				in->shift_last_tap_ms = 0;
			}
		}
		return;
	}

	/* toggle mode */
	if (m->chorded) {
		if (m->prev == MS_OFF) {
			mod_release(in, m);
			m->st = MS_OFF;
		} else {
			m->st = m->prev; /* chording does not unlatch/unlock */
		}
		return;
	}
	switch (m->prev) {
	case MS_OFF:
		m->st = MS_LATCHED;
		m->last_tap_ms = t;
		break;
	case MS_LATCHED:
		if (s->down_ms - m->last_tap_ms <= DOUBLE_TAP_MS) {
			if (k->mod == MOD_SHIFT && in->shift_caps) {
				mod_release(in, m);
				m->st = MS_OFF;
				caps_tap(in);
			} else {
				m->st = MS_LOCKED;
			}
		} else {
			mod_release(in, m);
			m->st = MS_OFF;
		}
		break;
	default: /* LOCKED (or HELD, which cannot be a prev state) */
		mod_release(in, m);
		m->st = MS_OFF;
		break;
	}
}

/* One-shot modifiers are consumed once no other non-modifier key is down. */
static void consume_latched(struct input *in, const struct slot *self)
{
	for (int i = 0; i < MAX_SLOTS; i++) {
		const struct slot *s = &in->slot[i];
		if (s == self || !s->used || !s->key) continue;
		if (s->key->type == KT_CHAR || s->key->type == KT_CAPS) return;
	}
	for (int mi = 0; mi < MOD_COUNT; mi++) {
		struct modifier *m = &in->mod[mi];
		if (m->st == MS_LATCHED && m->held == 0) {
			mod_release(in, m);
			m->st = MS_OFF;
		}
	}
}

void input_down(struct input *in, int32_t id, bool ptr, const struct keybox *box, float x, float y, uint32_t t)
{
	if (!box || !box->key || box->key->type == KT_SPACER) return;
	if (find_slot(in, id, ptr)) return; /* duplicate down: ignore */
	struct slot *s = NULL;
	int touches = 0;
	for (int i = 0; i < MAX_SLOTS; i++) {
		if (in->slot[i].used) touches += !in->slot[i].is_pointer;
		else if (!s) s = &in->slot[i];
	}
	if (!s) return;                                  /* all slots busy */
	if (!ptr && touches >= MAX_SLOTS - 1) return;    /* 10 touch points + 1 pointer */
	memset(s, 0, sizeof(*s));
	s->used = true;
	s->is_pointer = ptr;
	s->id = ptr ? -1 : id;
	s->key = box->key;
	s->box = *box;
	s->x = x;
	s->y = y;
	s->inside = true;
	s->down_ms = t;

	const struct key *k = box->key;
	switch (k->type) {
	case KT_CHAR:
		mark_chorded(in);
		tap_char(in, k);
		if ((k->flags & KF_REPEAT) && in->repeat_on) {
			in->repeat_slot = (int)(s - in->slot);
			arm(in, (uint32_t)in->repeat_delay);
		}
		break;
	case KT_CAPS:
		mark_chorded(in);
		caps_tap(in);
		break;
	case KT_MOD:
		mod_down(in, k, t);
		break;
	default:
		break; /* page / settings fire on up */
	}
	changed(in);
}

void input_motion(struct input *in, int32_t id, bool ptr, float x, float y)
{
	struct slot *s = find_slot(in, id, ptr);
	if (!s) return;
	s->x = x;
	s->y = y;
	bool inside = x >= s->box.x && x < s->box.x + s->box.w && y >= s->box.y && y < s->box.y + s->box.h;
	if (inside != s->inside) {
		s->inside = inside;
		if (!inside && in->repeat_slot == (int)(s - in->slot)) stop_repeat(in);
		changed(in);
	}
}

void input_up(struct input *in, int32_t id, bool ptr, uint32_t t)
{
	struct slot *s = find_slot(in, id, ptr);
	if (!s) return;
	const struct key *k = s->key;
	if (in->repeat_slot == (int)(s - in->slot)) stop_repeat(in);
	switch (k->type) {
	case KT_CHAR:
	case KT_CAPS:
		consume_latched(in, s);
		break;
	case KT_MOD:
		mod_up(in, k, s, t);
		break;
	case KT_PAGE:
	case KT_SETTINGS:
		if (s->inside) {
			s->used = false; /* free the slot before the action (it may release_all) */
			if (in->ops && in->ops->action) in->ops->action(in->ud, k);
			changed(in);
			return;
		}
		break;
	default:
		break;
	}
	s->used = false;
	changed(in);
}

void input_release_all(struct input *in)
{
	stop_repeat(in);
	in->repeat_slot = -1;
	for (int i = 0; i < MOD_COUNT; i++) {
		in->mod[i].st = MS_OFF;
		in->mod[i].held = 0;
		in->mod[i].code = 0;
		in->mod[i].chorded = false;
	}
	for (int i = 0; i < MAX_SLOTS; i++) in->slot[i].used = false;
	in->shift_last_tap_ms = 0;
	if (in->ops && in->ops->release_all) in->ops->release_all(in->ud);
	changed(in);
}

void input_cancel(struct input *in)
{
	input_release_all(in);
}

void input_repeat_fire(struct input *in)
{
	if (in->repeat_slot < 0) return;
	struct slot *s = &in->slot[in->repeat_slot];
	if (!s->used || !s->key || s->key->type != KT_CHAR || !s->inside || !in->repeat_on) {
		in->repeat_slot = -1;
		return;
	}
	tap_char(in, s->key);
	int rate = in->repeat_rate > 0 ? in->repeat_rate : 25;
	arm(in, (uint32_t)(1000 / rate));
}
