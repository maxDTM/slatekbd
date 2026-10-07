/* test_input: scripted sequences against the modifier/touch state machine. */
#include <linux/input-event-codes.h>
#include <string.h>

#include "input.h"
#include "t.h"

static char logbuf[4096];
static int down_count[512];
static uint32_t timer_ms;
static int timer_calls;
static const struct key *last_action;

static void op_key(void *ud, uint16_t code, bool down)
{
	(void)ud;
	char tmp[16];
	snprintf(tmp, sizeof tmp, "%s%c%u", logbuf[0] ? " " : "", down ? '+' : '-', code);
	strncat(logbuf, tmp, sizeof logbuf - strlen(logbuf) - 1);
	down_count[code] += down ? 1 : -1;
}
static void op_release_all(void *ud)
{
	for (int c = 0; c < 512; c++) {
		while (down_count[c] > 0) op_key(ud, (uint16_t)c, false);
	}
	strncat(logbuf, logbuf[0] ? " R" : "R", sizeof logbuf - strlen(logbuf) - 1);
}
static void op_action(void *ud, const struct key *k) { (void)ud; last_action = k; }
static void op_timer(void *ud, uint32_t ms) { (void)ud; timer_ms = ms; timer_calls++; }
static void op_changed(void *ud) { (void)ud; }
static const struct input_ops ops = { op_key, op_release_all, op_action, op_timer, op_changed };

static const struct key *K(int page, uint16_t code, int type, int nth)
{
	const struct page *pg = &layout_pages[page];
	for (int r = 0; r < pg->nrows; r++)
		for (int i = 0; i < pg->rows[r].n; i++) {
			const struct key *k = &pg->rows[r].keys[i];
			if (k->code == code && k->type == type && nth-- == 0) return k;
		}
	return NULL;
}

static struct keybox box(const struct key *k)
{
	struct keybox b;
	memset(&b, 0, sizeof b);
	b.key = k;
	b.x = 0; b.y = 0; b.w = 50; b.h = 50;
	return b;
}

static struct input in;
static void reset(bool hold)
{
	logbuf[0] = 0;
	memset(down_count, 0, sizeof down_count);
	timer_ms = 0;
	timer_calls = 0;
	last_action = NULL;
	input_init(&in, &ops, NULL);
	in.hold_mode = hold;
}
static void tap(const struct key *k, int id, uint32_t t)
{
	struct keybox b = box(k);
	input_down(&in, id, false, &b, 10, 10, t);
	input_up(&in, id, false, t + 40);
}
static void down(const struct key *k, int id, uint32_t t)
{
	struct keybox b = box(k);
	input_down(&in, id, false, &b, 10, 10, t);
}
static void up(int id, uint32_t t) { input_up(&in, id, false, t); }
#define EXPECT(s) CHECKF(!strcmp(logbuf, s), "expected '%s' got '%s'", s, logbuf)

static bool all_up(void)
{
	for (int c = 0; c < 512; c++) if (down_count[c] != 0) return false;
	return true;
}

int main(void)
{
	const struct key *lshift = K(PAGE_MAIN, KEY_LEFTSHIFT, KT_MOD, 0), *rshift = K(PAGE_MAIN, KEY_RIGHTSHIFT, KT_MOD, 0);
	const struct key *lctrl = K(PAGE_MAIN, KEY_LEFTCTRL, KT_MOD, 0), *a = K(PAGE_MAIN, KEY_A, KT_CHAR, 0);
	const struct key *c = K(PAGE_MAIN, KEY_C, KT_CHAR, 0), *bang = NULL, *one = K(PAGE_SYM, KEY_1, KT_CHAR, 0);
	const struct key *sym = NULL, *esc = K(PAGE_MAIN, KEY_ESC, KT_CHAR, 0);
	/* '!' is the shifted KEY_1 on SYM row 2 */
	for (int i = 0; i < layout_pages[PAGE_SYM].rows[1].n; i++)
		if (layout_pages[PAGE_SYM].rows[1].keys[i].code == KEY_1) bang = &layout_pages[PAGE_SYM].rows[1].keys[i];
	for (int i = 0; i < layout_pages[PAGE_MAIN].rows[4].n; i++)
		if (layout_pages[PAGE_MAIN].rows[4].keys[i].type == KT_PAGE) { sym = &layout_pages[PAGE_MAIN].rows[4].keys[i]; break; }
	CHECK(lshift && rshift && lctrl && a && c && bang && one && sym && esc);

	/* 1. toggle: latch then a key consumes the one-shot */
	reset(false);
	tap(lshift, 1, 0);
	CHECK(in.mod[MOD_SHIFT].st == MS_LATCHED);
	tap(a, 2, 1000);
	EXPECT("+42 +30 -30 -42");
	CHECK(in.mod[MOD_SHIFT].st == MS_OFF && all_up());

	/* 2. double-tap lock, keys keep it, tap unlocks */
	reset(false);
	tap(lctrl, 1, 0);
	tap(lctrl, 1, 200);
	CHECK(in.mod[MOD_CTRL].st == MS_LOCKED);
	tap(a, 2, 600);
	tap(c, 2, 700);
	CHECK(in.mod[MOD_CTRL].st == MS_LOCKED);
	tap(lctrl, 1, 2000);
	EXPECT("+29 +30 -30 +46 -46 -29");
	CHECK(in.mod[MOD_CTRL].st == MS_OFF && all_up());

	/* 2b. a late second tap unlatches instead of locking */
	reset(false);
	tap(lctrl, 1, 0);
	tap(lctrl, 1, 900);
	CHECK(in.mod[MOD_CTRL].st == MS_OFF);
	EXPECT("+29 -29");

	/* 3. chord in toggle mode behaves like a physical key */
	reset(false);
	down(lctrl, 1, 0);
	down(c, 2, 100);
	up(2, 150);
	up(1, 200);
	EXPECT("+29 +46 -46 -29");
	CHECK(in.mod[MOD_CTRL].st == MS_OFF && all_up());

	/* 4. hold mode: two fingers on two modifiers, third types */
	reset(true);
	down(lshift, 1, 0);
	down(lctrl, 2, 10);
	CHECK(in.mod[MOD_SHIFT].st == MS_HELD && in.mod[MOD_CTRL].st == MS_HELD);
	down(a, 3, 20);
	up(3, 60);
	up(2, 100);
	CHECK(in.mod[MOD_CTRL].st == MS_OFF && in.mod[MOD_SHIFT].st == MS_HELD);
	up(1, 120);
	EXPECT("+42 +29 +30 -30 -29 -42");
	CHECK(all_up() && !input_any_mod(&in));

	/* 5. two Shifts at once act as one */
	reset(true);
	down(lshift, 1, 0);
	down(rshift, 2, 10);
	up(1, 1000);
	CHECK(in.mod[MOD_SHIFT].st == MS_HELD);
	up(2, 1100);
	EXPECT("+42 -42");

	/* 6. Shift double-tap toggles Caps Lock (toggle mode) */
	reset(false);
	tap(lshift, 1, 0);
	tap(lshift, 1, 200);
	EXPECT("+42 -42 +58 -58");
	CHECK(in.caps && in.mod[MOD_SHIFT].st == MS_OFF);
	/* 6b. hold mode double-tap */
	reset(true);
	tap(lshift, 1, 0);
	tap(lshift, 1, 150);
	EXPECT("+42 -42 +42 -42 +58 -58");
	CHECK(in.caps);

	/* 7. KF_SHIFTED with and without Shift active */
	reset(false);
	tap(bang, 1, 0);
	EXPECT("+42 +2 -2 -42");
	reset(false);
	tap(lshift, 1, 0);
	tap(bang, 2, 1000);
	EXPECT("+42 +2 -2 -42");
	CHECK(in.mod[MOD_SHIFT].st == MS_OFF);
	reset(false);
	tap(one, 2, 0);
	EXPECT("+2 -2");

	/* 8. repeat start/stop */
	reset(false);
	down(a, 1, 0);
	CHECK(timer_ms == 400);
	input_repeat_fire(&in);
	CHECK(timer_ms == 40);
	input_repeat_fire(&in);
	up(1, 1000);
	CHECK(timer_ms == 0);
	EXPECT("+30 -30 +30 -30 +30 -30");
	/* non-repeating key arms nothing */
	reset(false);
	tap(esc, 1, 0);
	CHECK(timer_calls == 0);

	/* 9. cancel / release_all leave an empty down-set */
	reset(false);
	tap(lctrl, 1, 0);
	tap(lctrl, 1, 100); /* locked */
	down(lshift, 2, 500);
	input_cancel(&in);
	CHECK(all_up() && !input_any_mod(&in) && input_active_slots(&in) == 0);

	/* 10. 11 simultaneous touches: the 11th is ignored, the pointer still works */
	reset(false);
	for (int i = 0; i < 11; i++) down(lctrl, 100 + i, 0);
	CHECK(input_active_slots(&in) == 10);
	CHECK(in.mod[MOD_CTRL].held == 10);
	struct keybox pb = box(a);
	input_down(&in, -1, true, &pb, 1, 1, 10);
	CHECK(input_active_slots(&in) == 11);
	input_release_all(&in);
	CHECK(all_up());

	/* 11. slide-off cancels an action; release inside fires it */
	reset(false);
	struct keybox sb = box(sym);
	input_down(&in, 5, false, &sb, 10, 10, 0);
	input_motion(&in, 5, false, 200, 200);
	input_up(&in, 5, false, 50);
	CHECK(last_action == NULL);
	input_down(&in, 5, false, &sb, 10, 10, 100);
	input_motion(&in, 5, false, 20, 20);
	input_up(&in, 5, false, 150);
	CHECK(last_action == sym);
	CHECK(logbuf[0] == 0); /* page keys send nothing */

	/* 12. one-shot is held while another key is still down (two fingers typing) */
	reset(false);
	tap(lshift, 1, 0);
	down(a, 2, 1000);
	down(c, 3, 1010);
	up(2, 1050);
	CHECK(in.mod[MOD_SHIFT].st == MS_LATCHED);
	up(3, 1060);
	CHECK(in.mod[MOD_SHIFT].st == MS_OFF && all_up());
	T_DONE();
}
