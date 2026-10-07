/* layout.c - static key tables, geometry engine (normal / square / split), hit testing. */
#include "layout.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NP KF_NO_PREVIEW
#define R  KF_REPEAT

/* letter: lower/upper label, code, extra flags */
#define LT(l, u, c, f)            { l, u, KT_CHAR, c, R | KF_LETTER | (f), 1, 0, 0, IC_NONE }
/* plain printable char with shift label */
#define CH(l, s, c, f)            { l, s, KT_CHAR, c, R | (f), 1, 0, 0, IC_NONE }
/* glyph that needs Shift */
#define SH(l, c, f)               { l, NULL, KT_CHAR, c, R | KF_SHIFTED | (f), 1, 0, 0, IC_NONE }
/* function key (no preview), width, flags, icon */
#define FK(l, c, w, f, ic)        { l, NULL, KT_CHAR, c, NP | KF_DIM | (f), w, 0, 0, ic }
#define MODK(l, c, m, w, f, ic)   { l, NULL, KT_MOD, c, NP | KF_DIM | (f), w, m, 0, ic }
#define CAPS(w, f)                { "Caps", NULL, KT_CAPS, KEY_CAPSLOCK, NP | KF_DIM | (f), w, 0, 0, IC_NONE }
#define PG(l, p, w, f)            { l, NULL, KT_PAGE, 0, NP | KF_DIM | (f), w, 0, p, IC_NONE }
#define GEAR(w)                   { "", NULL, KT_SETTINGS, 0, NP | KF_DIM, w, 0, 0, IC_GEAR }
#define SPC(w)                    { "", NULL, KT_SPACER, 0, NP, w, 0, 0, IC_NONE }
#define SPACE(w)                  { "", NULL, KT_CHAR, KEY_SPACE, R | NP, w, 0, 0, IC_NONE }

#define ROW(a) { a, (int)(sizeof(a) / sizeof(a[0])) }
#define SB KF_SPLIT_BEFORE

/* ------------------------------------------------------------------ MAIN */
/* Number row as on a 60% board: Esc takes the ` position (` and ~ are on ?123). */
static const struct key main_r0[] = {
	FK("Esc", KEY_ESC, 1, 0, IC_NONE),
	CH("1", "!", KEY_1, 0), CH("2", "@", KEY_2, 0), CH("3", "#", KEY_3, 0),
	CH("4", "$", KEY_4, 0), CH("5", "%", KEY_5, 0), CH("6", "^", KEY_6, 0),
	CH("7", "&", KEY_7, 0), CH("8", "*", KEY_8, 0), CH("9", "(", KEY_9, 0), CH("0", ")", KEY_0, 0),
	CH("-", "_", KEY_MINUS, 0), CH("=", "+", KEY_EQUAL, 0),
	FK("Bksp", KEY_BACKSPACE, 2, R, IC_BKSP),
};
static const struct key main_r1[] = {
	FK("Tab", KEY_TAB, 1.5f, 0, IC_TAB),
	LT("q", "Q", KEY_Q, 0), LT("w", "W", KEY_W, 0), LT("e", "E", KEY_E, 0), LT("r", "R", KEY_R, 0), LT("t", "T", KEY_T, 0),
	LT("y", "Y", KEY_Y, 0), LT("u", "U", KEY_U, 0), LT("i", "I", KEY_I, 0), LT("o", "O", KEY_O, 0), LT("p", "P", KEY_P, 0),
	CH("[", "{", KEY_LEFTBRACE, 0), CH("]", "}", KEY_RIGHTBRACE, 0),
	{ "\\", "|", KT_CHAR, KEY_BACKSLASH, R, 1.5f, 0, 0, IC_NONE },
};
static const struct key main_r2[] = {
	CAPS(1.75f, 0),
	LT("a", "A", KEY_A, 0), LT("s", "S", KEY_S, 0), LT("d", "D", KEY_D, 0), LT("f", "F", KEY_F, 0), LT("g", "G", KEY_G, 0),
	LT("h", "H", KEY_H, 0), LT("j", "J", KEY_J, 0), LT("k", "K", KEY_K, 0), LT("l", "L", KEY_L, 0),
	CH(";", ":", KEY_SEMICOLON, 0), CH("'", "\"", KEY_APOSTROPHE, 0),
	FK("Enter", KEY_ENTER, 2.25f, R, IC_ENTER),
};
static const struct key main_r3[] = {
	MODK("Shift", KEY_LEFTSHIFT, MOD_SHIFT, 2.25f, 0, IC_SHIFT),
	LT("z", "Z", KEY_Z, 0), LT("x", "X", KEY_X, 0), LT("c", "C", KEY_C, 0), LT("v", "V", KEY_V, 0), LT("b", "B", KEY_B, 0),
	LT("n", "N", KEY_N, 0), LT("m", "M", KEY_M, 0),
	CH(",", "<", KEY_COMMA, 0), CH(".", ">", KEY_DOT, 0), CH("/", "?", KEY_SLASH, 0),
	MODK("Shift", KEY_RIGHTSHIFT, MOD_SHIFT, 2.75f, 0, IC_SHIFT),
};
#define BOTTOM_LEFT(pagekey)                                                   \
	MODK("Ctrl", KEY_LEFTCTRL, MOD_CTRL, 1.25f, 0, IC_NONE),                   \
	MODK("Super", KEY_LEFTMETA, MOD_SUPER, 1.25f, 0, IC_NONE),                 \
	MODK("Alt", KEY_LEFTALT, MOD_ALT, 1.25f, 0, IC_NONE),                      \
	pagekey
static const struct key main_r4[] = {
	BOTTOM_LEFT(PG("?123", PAGE_SYM, 1.25f, 0)),
	SPACE(5),
	MODK("Alt", KEY_RIGHTALT, MOD_ALT, 1.25f, 0, IC_NONE),
	PG("Fn", PAGE_FN, 1.25f, 0),
	GEAR(1.25f),
	MODK("Ctrl", KEY_RIGHTCTRL, MOD_CTRL, 1.25f, 0, IC_NONE),
};
static const struct row main_rows[] = { ROW(main_r0), ROW(main_r1), ROW(main_r2), ROW(main_r3), ROW(main_r4) };

/* ------------------------------------------------------------------- SYM */
static const struct key sym_r1[] = {
	CH("`", "~", KEY_GRAVE, 0), CH("1", "!", KEY_1, 0), CH("2", "@", KEY_2, 0), CH("3", "#", KEY_3, 0),
	CH("4", "$", KEY_4, 0), CH("5", "%", KEY_5, 0), CH("6", "^", KEY_6, 0),
	CH("7", "&", KEY_7, 0), CH("8", "*", KEY_8, 0), CH("9", "(", KEY_9, 0), CH("0", ")", KEY_0, 0),
	CH("-", "_", KEY_MINUS, 0), CH("=", "+", KEY_EQUAL, 0),
	FK("Bksp", KEY_BACKSPACE, 2, R, IC_BKSP),
};
static const struct key sym_r2[] = {
	FK("Tab", KEY_TAB, 1.5f, 0, IC_TAB),
	SH("!", KEY_1, 0), SH("@", KEY_2, 0), SH("#", KEY_3, 0), SH("$", KEY_4, 0), SH("%", KEY_5, 0), SH("^", KEY_6, 0),
	SH("&", KEY_7, 0), SH("*", KEY_8, 0), SH("(", KEY_9, 0), SH(")", KEY_0, 0),
	CH("[", "{", KEY_LEFTBRACE, 0), CH("]", "}", KEY_RIGHTBRACE, 0),
	{ "\\", "|", KT_CHAR, KEY_BACKSLASH, R, 1.5f, 0, 0, IC_NONE },
};
static const struct key sym_r3[] = {
	FK("Esc", KEY_ESC, 1.75f, 0, IC_NONE),
	SH("~", KEY_GRAVE, 0), SH("_", KEY_MINUS, 0), SH("+", KEY_EQUAL, 0), SH("{", KEY_LEFTBRACE, 0), SH("}", KEY_RIGHTBRACE, 0),
	SH("|", KEY_BACKSLASH, 0), CH(";", ":", KEY_SEMICOLON, 0), SH(":", KEY_SEMICOLON, 0),
	CH("'", "\"", KEY_APOSTROPHE, 0), SH("\"", KEY_APOSTROPHE, 0),
	FK("Enter", KEY_ENTER, 3.25f, R, IC_ENTER),
};
static const struct key sym_r4[] = {
	MODK("Shift", KEY_LEFTSHIFT, MOD_SHIFT, 2.25f, 0, IC_SHIFT),
	CH(",", "<", KEY_COMMA, 0), CH(".", ">", KEY_DOT, 0), CH("/", "?", KEY_SLASH, 0), SH("<", KEY_COMMA, 0),
	SH(">", KEY_DOT, 0), SH("?", KEY_SLASH, 0),
	FK("Del", KEY_DELETE, 1.5f, R, IC_NONE),
	FK("", KEY_LEFT, 1.25f, R, IC_LEFT), FK("", KEY_RIGHT, 1.25f, R, IC_RIGHT),
	MODK("Shift", KEY_RIGHTSHIFT, MOD_SHIFT, 2.75f, 0, IC_SHIFT),
};
static const struct key sym_r5[] = {
	BOTTOM_LEFT(PG("abc", PAGE_MAIN, 1.25f, 0)),
	SPACE(5),
	MODK("Alt", KEY_RIGHTALT, MOD_ALT, 1.25f, 0, IC_NONE),
	PG("Fn", PAGE_FN, 1.25f, 0),
	GEAR(1.25f),
	MODK("Ctrl", KEY_RIGHTCTRL, MOD_CTRL, 1.25f, 0, IC_NONE),
};
static const struct row sym_rows[] = { ROW(sym_r1), ROW(sym_r2), ROW(sym_r3), ROW(sym_r4), ROW(sym_r5) };

/* -------------------------------------------------------------------- FN */
#define FNK(l, c, f) FK(l, c, 1, KF_SMALL | (f), IC_NONE)
static const struct key fn_r1[] = {
	FK("Esc", KEY_ESC, 1, 0, IC_NONE),
	FNK("F1", KEY_F1, 0), FNK("F2", KEY_F2, 0), FNK("F3", KEY_F3, 0), FNK("F4", KEY_F4, 0), FNK("F5", KEY_F5, 0), FNK("F6", KEY_F6, 0),
	FNK("F7", KEY_F7, 0), FNK("F8", KEY_F8, 0), FNK("F9", KEY_F9, 0), FNK("F10", KEY_F10, 0), FNK("F11", KEY_F11, 0), FNK("F12", KEY_F12, 0),
	FK("Bksp", KEY_BACKSPACE, 2, R, IC_BKSP),
};
/* Right half mirrors a TKL: Ins/Home/PgUp over Del/End/PgDn over the inverted-T arrows,
 * with Enter and right Shift flush against that block. The left half holds the
 * media/brightness keys at a wider, easier-to-hit width. */
static const struct key fn_r2[] = {
	FK("Tab", KEY_TAB, 1.75f, 0, IC_TAB),
	FK("PrtSc", KEY_SYSRQ, 1.75f, KF_SMALL, IC_NONE), FK("ScrLk", KEY_SCROLLLOCK, 1.75f, KF_SMALL, IC_NONE),
	FK("Pause", KEY_PAUSE, 1.75f, KF_SMALL, IC_NONE),
	FK("Bri−", KEY_BRIGHTNESSDOWN, 1.75f, KF_SMALL, IC_NONE), FK("Bri+", KEY_BRIGHTNESSUP, 1.75f, KF_SMALL, IC_NONE),
	FK("Ins", KEY_INSERT, 1.5f, KF_SMALL, IC_NONE), FK("Home", KEY_HOME, 1.5f, KF_SMALL, IC_NONE),
	FK("PgUp", KEY_PAGEUP, 1.5f, KF_SMALL | R, IC_NONE),
};
static const struct key fn_r3[] = {
	CAPS(1.75f, 0),
	FK("Prev", KEY_PREVIOUSSONG, 1.75f, KF_SMALL, IC_NONE), FK("Play", KEY_PLAYPAUSE, 1.75f, KF_SMALL, IC_NONE),
	FK("Next", KEY_NEXTSONG, 1.75f, KF_SMALL, IC_NONE),
	FK("Enter", KEY_ENTER, 3.5f, R, IC_ENTER),
	FK("Del", KEY_DELETE, 1.5f, KF_SMALL | R, IC_NONE), FK("End", KEY_END, 1.5f, KF_SMALL, IC_NONE),
	FK("PgDn", KEY_PAGEDOWN, 1.5f, KF_SMALL | R, IC_NONE),
};
static const struct key fn_r4[] = {
	MODK("Shift", KEY_LEFTSHIFT, MOD_SHIFT, 2.25f, 0, IC_SHIFT),
	FK("Mute", KEY_MUTE, 1.75f, KF_SMALL, IC_NONE), FK("Vol−", KEY_VOLUMEDOWN, 1.75f, KF_SMALL, IC_NONE),
	FK("Vol+", KEY_VOLUMEUP, 1.75f, KF_SMALL, IC_NONE),
	FK("Menu", KEY_COMPOSE, 1.75f, KF_SMALL, IC_NONE),
	MODK("Shift", KEY_RIGHTSHIFT, MOD_SHIFT, 2.75f, 0, IC_SHIFT),
	FK("", KEY_UP, 1.5f, R, IC_UP), SPC(1.5f),
};
static const struct key fn_r5[] = {
	BOTTOM_LEFT(PG("abc", PAGE_MAIN, 1.25f, 0)),
	SPACE(3),
	MODK("Alt", KEY_RIGHTALT, MOD_ALT, 1.25f, 0, IC_NONE),
	GEAR(1.25f),
	FK("", KEY_LEFT, 1.5f, R, IC_LEFT), FK("", KEY_DOWN, 1.5f, R, IC_DOWN), FK("", KEY_RIGHT, 1.5f, R, IC_RIGHT),
};
static const struct row fn_rows[] = { ROW(fn_r1), ROW(fn_r2), ROW(fn_r3), ROW(fn_r4), ROW(fn_r5) };

/* ----------------------------------------------------------------- SPLIT */
/* Split mode has its own tables: a 7u left half and an 8u right half. Every row of a
 * half has the same width, so both the gap edge and the outer edge are straight, and
 * the outer function keys are a uniform 2u. SB marks the first key of the right half.
 * Space appears once in each half. */
#define SPLIT_L 7
#define SPLIT_R 8
#define F3 (5.0f / 3.0f)
#define SBOTTOM(pagekey)                                                       \
	MODK("Ctrl", KEY_LEFTCTRL, MOD_CTRL, 1, 0, IC_NONE),                       \
	MODK("Super", KEY_LEFTMETA, MOD_SUPER, 1, 0, IC_NONE),                     \
	MODK("Alt", KEY_LEFTALT, MOD_ALT, 1, 0, IC_NONE),                          \
	pagekey,                                                                   \
	SPACE(3),                                                                  \
	{ "", NULL, KT_CHAR, KEY_SPACE, R | NP | SB, 3, 0, 0, IC_NONE },           \
	MODK("Alt", KEY_RIGHTALT, MOD_ALT, 1.25f, 0, IC_NONE)

static const struct key smain_r0[] = {
	FK("Esc", KEY_ESC, 1, 0, IC_NONE),
	CH("1", "!", KEY_1, 0), CH("2", "@", KEY_2, 0), CH("3", "#", KEY_3, 0),
	CH("4", "$", KEY_4, 0), CH("5", "%", KEY_5, 0), CH("6", "^", KEY_6, 0),
	CH("7", "&", KEY_7, SB), CH("8", "*", KEY_8, 0), CH("9", "(", KEY_9, 0), CH("0", ")", KEY_0, 0),
	CH("-", "_", KEY_MINUS, 0), CH("=", "+", KEY_EQUAL, 0),
	FK("Bksp", KEY_BACKSPACE, 2, R, IC_BKSP),
};
static const struct key smain_r1[] = {
	FK("Tab", KEY_TAB, 2, 0, IC_TAB),
	LT("q", "Q", KEY_Q, 0), LT("w", "W", KEY_W, 0), LT("e", "E", KEY_E, 0), LT("r", "R", KEY_R, 0), LT("t", "T", KEY_T, 0),
	LT("y", "Y", KEY_Y, SB), LT("u", "U", KEY_U, 0), LT("i", "I", KEY_I, 0), LT("o", "O", KEY_O, 0), LT("p", "P", KEY_P, 0),
	CH("[", "{", KEY_LEFTBRACE, 0), CH("]", "}", KEY_RIGHTBRACE, 0), CH("\\", "|", KEY_BACKSLASH, 0),
};
static const struct key smain_r2[] = {
	CAPS(2, 0),
	LT("a", "A", KEY_A, 0), LT("s", "S", KEY_S, 0), LT("d", "D", KEY_D, 0), LT("f", "F", KEY_F, 0), LT("g", "G", KEY_G, 0),
	LT("h", "H", KEY_H, SB), LT("j", "J", KEY_J, 0), LT("k", "K", KEY_K, 0), LT("l", "L", KEY_L, 0),
	CH(";", ":", KEY_SEMICOLON, 0), CH("'", "\"", KEY_APOSTROPHE, 0),
	FK("Enter", KEY_ENTER, 2, R, IC_ENTER),
};
static const struct key smain_r3[] = {
	MODK("Shift", KEY_LEFTSHIFT, MOD_SHIFT, 2, 0, IC_SHIFT),
	LT("z", "Z", KEY_Z, 0), LT("x", "X", KEY_X, 0), LT("c", "C", KEY_C, 0), LT("v", "V", KEY_V, 0), LT("b", "B", KEY_B, 0),
	LT("n", "N", KEY_N, SB), LT("m", "M", KEY_M, 0),
	CH(",", "<", KEY_COMMA, 0), CH(".", ">", KEY_DOT, 0), CH("/", "?", KEY_SLASH, 0),
	MODK("Shift", KEY_RIGHTSHIFT, MOD_SHIFT, 3, 0, IC_SHIFT),
};
static const struct key smain_r4[] = {
	SBOTTOM(PG("?123", PAGE_SYM, 1, 0)),
	PG("Fn", PAGE_FN, 1.25f, 0),
	GEAR(1.25f),
	MODK("Ctrl", KEY_RIGHTCTRL, MOD_CTRL, 1.25f, 0, IC_NONE),
};
static const struct row smain_rows[] = { ROW(smain_r0), ROW(smain_r1), ROW(smain_r2), ROW(smain_r3), ROW(smain_r4) };

static const struct key ssym_r0[] = {
	CH("`", "~", KEY_GRAVE, 0), CH("1", "!", KEY_1, 0), CH("2", "@", KEY_2, 0), CH("3", "#", KEY_3, 0),
	CH("4", "$", KEY_4, 0), CH("5", "%", KEY_5, 0), CH("6", "^", KEY_6, 0),
	CH("7", "&", KEY_7, SB), CH("8", "*", KEY_8, 0), CH("9", "(", KEY_9, 0), CH("0", ")", KEY_0, 0),
	CH("-", "_", KEY_MINUS, 0), CH("=", "+", KEY_EQUAL, 0),
	FK("Bksp", KEY_BACKSPACE, 2, R, IC_BKSP),
};
static const struct key ssym_r1[] = {
	FK("Tab", KEY_TAB, 2, 0, IC_TAB),
	SH("!", KEY_1, 0), SH("@", KEY_2, 0), SH("#", KEY_3, 0), SH("$", KEY_4, 0), SH("%", KEY_5, 0),
	SH("^", KEY_6, SB), SH("&", KEY_7, 0), SH("*", KEY_8, 0), SH("(", KEY_9, 0), SH(")", KEY_0, 0),
	CH("[", "{", KEY_LEFTBRACE, 0), CH("]", "}", KEY_RIGHTBRACE, 0), CH("\\", "|", KEY_BACKSLASH, 0),
};
static const struct key ssym_r2[] = {
	FK("Esc", KEY_ESC, 2, 0, IC_NONE),
	SH("~", KEY_GRAVE, 0), SH("_", KEY_MINUS, 0), SH("+", KEY_EQUAL, 0), SH("{", KEY_LEFTBRACE, 0), SH("}", KEY_RIGHTBRACE, 0),
	SH("|", KEY_BACKSLASH, SB), CH(";", ":", KEY_SEMICOLON, 0), SH(":", KEY_SEMICOLON, 0),
	CH("'", "\"", KEY_APOSTROPHE, 0), SH("\"", KEY_APOSTROPHE, 0),
	FK("Enter", KEY_ENTER, 3, R, IC_ENTER),
};
static const struct key ssym_r3[] = {
	MODK("Shift", KEY_LEFTSHIFT, MOD_SHIFT, 2, 0, IC_SHIFT),
	CH(",", "<", KEY_COMMA, 0), CH(".", ">", KEY_DOT, 0), CH("/", "?", KEY_SLASH, 0), SH("<", KEY_COMMA, 0), SH(">", KEY_DOT, 0),
	SH("?", KEY_SLASH, SB), FK("Del", KEY_DELETE, 1, R, IC_NONE),
	FK("", KEY_LEFT, 1, R, IC_LEFT), FK("", KEY_DOWN, 1, R, IC_DOWN), FK("", KEY_UP, 1, R, IC_UP), FK("", KEY_RIGHT, 1, R, IC_RIGHT),
	MODK("Shift", KEY_RIGHTSHIFT, MOD_SHIFT, 2, 0, IC_SHIFT),
};
static const struct key ssym_r4[] = {
	SBOTTOM(PG("abc", PAGE_MAIN, 1, 0)),
	PG("Fn", PAGE_FN, 1.25f, 0),
	GEAR(1.25f),
	MODK("Ctrl", KEY_RIGHTCTRL, MOD_CTRL, 1.25f, 0, IC_NONE),
};
static const struct row ssym_rows[] = { ROW(ssym_r0), ROW(ssym_r1), ROW(ssym_r2), ROW(ssym_r3), ROW(ssym_r4) };

/* Right half keeps the TKL block: Ins/Home/PgUp over Del/End/PgDn over the arrows. */
static const struct key sfn_r0[] = {
	FK("Esc", KEY_ESC, 1, 0, IC_NONE),
	FNK("F1", KEY_F1, 0), FNK("F2", KEY_F2, 0), FNK("F3", KEY_F3, 0), FNK("F4", KEY_F4, 0), FNK("F5", KEY_F5, 0), FNK("F6", KEY_F6, 0),
	FNK("F7", KEY_F7, SB), FNK("F8", KEY_F8, 0), FNK("F9", KEY_F9, 0), FNK("F10", KEY_F10, 0), FNK("F11", KEY_F11, 0), FNK("F12", KEY_F12, 0),
	FK("Bksp", KEY_BACKSPACE, 2, R, IC_BKSP),
};
static const struct key sfn_r1[] = {
	FK("Tab", KEY_TAB, 2, 0, IC_TAB),
	FK("PrtSc", KEY_SYSRQ, F3, KF_SMALL, IC_NONE), FK("ScrLk", KEY_SCROLLLOCK, F3, KF_SMALL, IC_NONE),
	FK("Pause", KEY_PAUSE, F3, KF_SMALL, IC_NONE),
	FK("Bri−", KEY_BRIGHTNESSDOWN, 1.75f, KF_SMALL | SB, IC_NONE), FK("Bri+", KEY_BRIGHTNESSUP, 1.75f, KF_SMALL, IC_NONE),
	FK("Ins", KEY_INSERT, 1.5f, KF_SMALL, IC_NONE), FK("Home", KEY_HOME, 1.5f, KF_SMALL, IC_NONE),
	FK("PgUp", KEY_PAGEUP, 1.5f, KF_SMALL | R, IC_NONE),
};
static const struct key sfn_r2[] = {
	CAPS(2, 0),
	FK("Prev", KEY_PREVIOUSSONG, F3, KF_SMALL, IC_NONE), FK("Play", KEY_PLAYPAUSE, F3, KF_SMALL, IC_NONE),
	FK("Next", KEY_NEXTSONG, F3, KF_SMALL, IC_NONE),
	FK("Enter", KEY_ENTER, 3.5f, R | SB, IC_ENTER),
	FK("Del", KEY_DELETE, 1.5f, KF_SMALL | R, IC_NONE), FK("End", KEY_END, 1.5f, KF_SMALL, IC_NONE),
	FK("PgDn", KEY_PAGEDOWN, 1.5f, KF_SMALL | R, IC_NONE),
};
static const struct key sfn_r3[] = {
	MODK("Shift", KEY_LEFTSHIFT, MOD_SHIFT, 2, 0, IC_SHIFT),
	FK("Mute", KEY_MUTE, F3, KF_SMALL, IC_NONE), FK("Vol−", KEY_VOLUMEDOWN, F3, KF_SMALL, IC_NONE),
	FK("Vol+", KEY_VOLUMEUP, F3, KF_SMALL, IC_NONE),
	FK("Menu", KEY_COMPOSE, 1.5f, KF_SMALL | SB, IC_NONE), SPC(1.5f),
	MODK("Shift", KEY_RIGHTSHIFT, MOD_SHIFT, 2, 0, IC_SHIFT),
	FK("", KEY_UP, 1.5f, R, IC_UP), SPC(1.5f),
};
static const struct key sfn_r4[] = {
	MODK("Ctrl", KEY_LEFTCTRL, MOD_CTRL, 1, 0, IC_NONE),
	MODK("Super", KEY_LEFTMETA, MOD_SUPER, 1, 0, IC_NONE),
	MODK("Alt", KEY_LEFTALT, MOD_ALT, 1, 0, IC_NONE),
	PG("abc", PAGE_MAIN, 1, 0),
	SPACE(3),
	{ "Alt", NULL, KT_MOD, KEY_RIGHTALT, NP | KF_DIM | SB, 1.75f, MOD_ALT, 0, IC_NONE },
	GEAR(1.75f),
	FK("", KEY_LEFT, 1.5f, R, IC_LEFT), FK("", KEY_DOWN, 1.5f, R, IC_DOWN), FK("", KEY_RIGHT, 1.5f, R, IC_RIGHT),
};
static const struct row sfn_rows[] = { ROW(sfn_r0), ROW(sfn_r1), ROW(sfn_r2), ROW(sfn_r3), ROW(sfn_r4) };

const struct page layout_pages[PAGE_COUNT] = {
	[PAGE_MAIN] = { "abc", main_rows, 5, 15, smain_rows, SPLIT_L, SPLIT_R },
	[PAGE_SYM]  = { "?123", sym_rows, 5, 15, ssym_rows, SPLIT_L, SPLIT_R },
	[PAGE_FN]   = { "Fn", fn_rows, 5, 15, sfn_rows, SPLIT_L, SPLIT_R },
};

/* ------------------------------------------------------------- validation */
static float row_fixed_units(const struct row *r, int *nflex)
{
	float s = 0;
	*nflex = 0;
	for (int i = 0; i < r->n; i++) {
		if (r->keys[i].width < 0) (*nflex)++;
		else s += r->keys[i].width;
	}
	return s;
}

bool layout_validate(char *err, int errlen)
{
	for (int p = 0; p < PAGE_COUNT; p++) {
		const struct page *pg = &layout_pages[p];
		int gears = 0, boxes = 0;
		if (pg->nrows > MAX_ROWS) {
			snprintf(err, errlen, "page %s: too many rows", pg->name);
			return false;
		}
		for (int r = 0; r < pg->nrows; r++) {
			const struct row *row = &pg->rows[r];
			int nflex;
			float s = row_fixed_units(row, &nflex);
			if (nflex > 1) {
				snprintf(err, errlen, "page %s row %d: %d flex spacers", pg->name, r + 1, nflex);
				return false;
			}
			if (nflex == 0 && fabsf(s - pg->units) > 0.01f) {
				snprintf(err, errlen, "page %s row %d: sums to %.2f u, expected %.2f", pg->name, r + 1, s, pg->units);
				return false;
			}
			if (nflex == 1 && s > pg->units + 0.01f) {
				snprintf(err, errlen, "page %s row %d: %.2f u exceeds %.2f", pg->name, r + 1, s, pg->units);
				return false;
			}
			for (int i = 0; i < row->n; i++) {
				const struct key *k = &row->keys[i];
				if (k->type == KT_SETTINGS) gears++;
				if (k->type != KT_SPACER) boxes++;
				if (k->type == KT_MOD && k->mod >= MOD_COUNT) {
					snprintf(err, errlen, "page %s row %d: bad modifier", pg->name, r + 1);
					return false;
				}
			}
		}
		int sgears = 0, sboxes = 0;
		for (int r = 0; r < pg->nrows; r++) {
			const struct row *row = &pg->split_rows[r];
			float sum[2] = { 0, 0 };
			int half = 0, nsplit = 0;
			for (int i = 0; i < row->n; i++) {
				const struct key *k = &row->keys[i];
				if (k->flags & KF_SPLIT_BEFORE) { half = 1; nsplit++; }
				if (k->width < 0) {
					snprintf(err, errlen, "page %s split row %d: flex key", pg->name, r + 1);
					return false;
				}
				sum[half] += k->width;
				if (k->type == KT_SETTINGS) sgears++;
				if (k->type != KT_SPACER) sboxes++;
			}
			if (nsplit != 1 || fabsf(sum[0] - pg->split_l) > 0.01f || fabsf(sum[1] - pg->split_r) > 0.01f) {
				snprintf(err, errlen, "page %s split row %d: %d markers, halves %.2f|%.2f u (want %.2f|%.2f)",
				         pg->name, r + 1, nsplit, sum[0], sum[1], pg->split_l, pg->split_r);
				return false;
			}
		}
		if (sgears < 1 || sboxes > MAX_BOXES) {
			snprintf(err, errlen, "page %s: split layout needs a settings key and <= %d keys", pg->name, MAX_BOXES);
			return false;
		}
		if (gears < 1) {
			snprintf(err, errlen, "page %s: no settings key", pg->name);
			return false;
		}
		if (boxes > MAX_BOXES) {
			snprintf(err, errlen, "page %s: too many keys", pg->name);
			return false;
		}
	}
	return true;
}

float layout_band(float height)
{
	/* The preview bubble rises 0.95 row heights above a top-row key; every page
	 * has 5 rows. */
	return roundf(height / 5.0f);
}

float layout_row_h(int page, float height)
{
	if (page < 0 || page >= PAGE_COUNT) page = 0;
	return height / (float)layout_pages[page].nrows;
}

/* --------------------------------------------------------------- geometry */
static void add_box(struct geometry *g, const struct key *k, int row, uint8_t half, float x, float w)
{
	if (g->n >= MAX_BOXES) return;
	struct keybox *b = &g->box[g->n++];
	b->key = k;
	b->x = x;
	b->y = g->kb_y + row * g->row_h;
	b->w = w;
	b->h = g->row_h;
	b->row = (uint8_t)row;
	b->half = half;
}

int layout_build(struct geometry *g, const struct geo_params *p)
{
	if (p->page < 0 || p->page >= PAGE_COUNT || p->width <= 0 || p->height <= 0) return -1;
	const struct page *pg = &layout_pages[p->page];
	memset(g, 0, sizeof(*g));
	g->page = p->page;
	g->split = p->split;
	g->shape = p->shape;
	g->width = p->width;
	g->height = p->height;
	g->band = p->band;
	g->gap_pct = p->gap_pct;
	g->nrows = pg->nrows;
	g->row_h = p->height / pg->nrows;
	g->pad = fmaxf(2.0f, roundf(g->row_h * 0.06f));
	g->kb_y = p->band;
	g->kb_h = p->height;
	const float W = p->width;

	if (!p->split) {
		if (p->shape == GEO_SQUARE) {
			g->unit = fminf(g->row_h, W / pg->units);
			g->kb_w = pg->units * g->unit;
			g->kb_x = (W - g->kb_w) / 2;
		} else {
			g->unit = W / pg->units;
			g->kb_w = W;
			g->kb_x = 0;
		}
		for (int r = 0; r < pg->nrows; r++) {
			const struct row *row = &pg->rows[r];
			int nflex;
			float flex = pg->units - row_fixed_units(row, &nflex);
			float x = g->kb_x;
			g->row_start[r] = g->n;
			for (int i = 0; i < row->n; i++) {
				const struct key *k = &row->keys[i];
				float w = (k->width < 0 ? fmaxf(flex, 0) : k->width) * g->unit;
				if (k->type != KT_SPACER) add_box(g, k, r, 0, x, w);
				x += w;
			}
		}
		g->row_start[pg->nrows] = g->n;
		return 0;
	}

	/* ---- split: fixed-width halves against the outer edges, straight gap edges ---- */
	float gap = W * (float)p->gap_pct / 100.0f;
	float units = pg->split_l + pg->split_r;
	if (W - gap <= 0) return -1;
	g->unit = (W - gap) / units;
	if (p->shape == GEO_SQUARE) g->unit = fminf(g->unit, g->row_h);
	float inset = (W - gap - units * g->unit) / 2;
	g->kb_x = inset;
	g->kb_w = W - 2 * inset;
	float right_x = W - inset - pg->split_r * g->unit;
	for (int r = 0; r < pg->nrows; r++) {
		const struct row *row = &pg->split_rows[r];
		float x = inset;
		uint8_t half = 0;
		g->row_start[r] = g->n;
		for (int i = 0; i < row->n; i++) {
			const struct key *k = &row->keys[i];
			if (k->flags & KF_SPLIT_BEFORE) {
				half = 1;
				x = right_x;
			}
			float w = k->width * g->unit;
			if (k->type != KT_SPACER) add_box(g, k, r, half, x, w);
			x += w;
		}
	}
	g->row_start[pg->nrows] = g->n;
	return 0;
}

const struct keybox *layout_hit(const struct geometry *g, float x, float y)
{
	if (g->nrows <= 0 || g->row_h <= 0) return NULL;
	if (y < g->kb_y || y >= g->kb_y + g->kb_h) return NULL;
	int r = (int)((y - g->kb_y) / g->row_h);
	if (r < 0) r = 0;
	if (r >= g->nrows) r = g->nrows - 1;
	for (int i = g->row_start[r]; i < g->row_start[r + 1]; i++) {
		const struct keybox *b = &g->box[i];
		if (x >= b->x && x < b->x + b->w) return b;
	}
	return NULL;
}
