/* input.h - touch/pointer slots and the modifier state machine.
 * Pure logic: every side effect goes through `struct input_ops`. */
#ifndef SLATEKBD_INPUT_H
#define SLATEKBD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "layout.h"

enum mod_state { MS_OFF, MS_LATCHED, MS_LOCKED, MS_HELD };

struct modifier {
	enum mod_state st;
	enum mod_state prev; /* state before the current hold (toggle mode) */
	uint8_t held;        /* slots currently holding it */
	bool chorded;        /* a key was used while held */
	uint32_t last_tap_ms;
	uint16_t code;       /* keycode currently down for it, 0 = none */
};

#define MAX_SLOTS 11
#define DOUBLE_TAP_MS 350

struct slot {
	bool used;
	bool is_pointer;
	int32_t id;
	const struct key *key;
	struct keybox box;   /* copy, survives relayout */
	float x, y;
	bool inside;
	uint32_t down_ms;
};

struct input_ops {
	void (*key)(void *ud, uint16_t code, bool down);   /* physical key to the virtual keyboard */
	void (*release_all)(void *ud);                     /* release every down keycode (keep Caps lock) */
	void (*action)(void *ud, const struct key *k);     /* KT_PAGE / KT_SETTINGS fired on release */
	void (*timer)(void *ud, uint32_t ms);              /* arm repeat timer, 0 cancels */
	void (*changed)(void *ud);                         /* visual state changed */
};

struct input {
	struct slot slot[MAX_SLOTS];
	struct modifier mod[MOD_COUNT];
	bool caps;
	int repeat_slot;
	uint32_t shift_last_tap_ms;
	/* behaviour */
	bool hold_mode;
	bool repeat_on;
	int repeat_delay, repeat_rate;
	bool shift_caps;
	const struct input_ops *ops;
	void *ud;
};

void input_init(struct input *in, const struct input_ops *ops, void *ud);
/* box may be NULL (empty hit): the touch is ignored */
void input_down(struct input *in, int32_t id, bool ptr, const struct keybox *box, float x, float y, uint32_t t);
void input_motion(struct input *in, int32_t id, bool ptr, float x, float y);
void input_up(struct input *in, int32_t id, bool ptr, uint32_t t);
/* touch.cancel: drop every slot and release everything */
void input_cancel(struct input *in);
/* release keys, modifiers to OFF, clear slots (Caps lock preserved) */
void input_release_all(struct input *in);
/* repeat timer fired */
void input_repeat_fire(struct input *in);
bool input_shift_active(const struct input *in);
bool input_has_slot(const struct input *in, int32_t id, bool ptr);
int input_active_slots(const struct input *in);
/* true if any modifier is not OFF */
bool input_any_mod(const struct input *in);

#endif
