# slatekbd — Design

On-screen touch keyboard for Wayland (Hyprland 0.56 on a Surface Pro 6). C11, meson,
wayland-client + xkbcommon + cairo + pango. Original code. wvkbd was used only as a
reference for what to avoid (see docs/research-protocols.md).

Units: **logical px** (surface coordinates) unless noted. **u** = one key-width unit.

---

## 1. Module breakdown

| File | Responsibility |
|---|---|
| `src/main.c` | argv parsing, `--help`, CLI client mode (signal a running instance), startup, `poll` event loop, shutdown |
| `src/app.h` | `struct app` (all runtime state) plus shared enums. One global `struct app` is passed by pointer everywhere. |
| `src/instance.c/.h` | pidfile + `flock`, sending signals to the running instance, spawning a detached instance (`--lock` when none is running) |
| `src/loop.c/.h` | `signalfd` setup and dispatch, timer service (one `timerfd` holding a sorted list of deadlines: repeat, double-tap, hide-debounce, quit-confirm, config-save) |
| `src/wayland.c/.h` | registry, global binding, outputs (wl_output v4 + xdg_output), seat (wl_touch, wl_pointer), layer-surface lifecycle (create/destroy/configure), input region, exclusive zone, scale (fractional/viewport/buffer scale), frame pacing |
| `src/shm.c/.h` | double-buffered ARGB8888 memfd shm buffers, resized on demand, `busy` tracking via `wl_buffer.release` |
| `src/layout.c/.h` | static page tables (all keys), geometry engine (normal / square / split), hit-testing, layout validation |
| `src/render.c/.h` | cairo/pango drawing: cached base layer, pressed highlights, preview bubble, settings page, vector icons (gear, etc.), themes |
| `src/keymap.c/.h` | xkb context/keymap (evdev/pc105/us), keymap upload via memfd, `zwp_virtual_keyboard_v1`, `xkb_state` mirror, `vk_press/vk_release/vk_release_all`, modifier serialisation |
| `src/input.c/.h` | **pure logic** (no Wayland calls; uses callbacks): touch/pointer slots, modifier state machine, caps, repeat scheduling, action dispatch. Unit-testable. |
| `src/settings.c/.h` | settings page model: tile list, pagination, tile hit-testing, applying changes (calls back into app for live apply) |
| `src/config.c/.h` | `struct config`, defaults, key=value parse/serialise, atomic save, CLI override mask |
| `src/im.c/.h` | `zwp_input_method_v2`: buffered activate/deactivate/content_type, applied on `done`, `unavailable` handling, re-acquire |
| `src/visibility.c/.h` | decides visibility from auto/IM, manual, lock and debounce (pure function plus apply step) |
| `src/rotate.c/.h` | rotate-command chain (custom, then `hyprctl eval`, then legacy `--batch`), fork/exec, non-blocking child reaping |
| `src/lock.c/.h` | `hyprland_lock_notifier_v1`, `--lock/--unlock` signals, lock-mode enter/exit, optional runtime layer rule |
| `protocols/*.xml` | vendored: wlr-layer-shell-unstable-v1, virtual-keyboard-unstable-v1, input-method-unstable-v2, hyprland-lock-notify-v1 |
| system protocols | xdg-shell (needed by layer-shell's get_popup), viewporter, fractional-scale-v1, xdg-output-unstable-v1 |
| `tests/unit/*.c` | meson tests: layout, split, hit-test, config, modifier state machine, visibility |
| `tests/nested/` | existing nested-Hyprland harness (probe, target, vclick, run_nested.sh) plus `scenarios.sh` |

meson: one `custom_target` pair (`client-header`, `private-code`) per protocol XML.
System XMLs are located with `dependency('wayland-protocols').get_variable('pkgdatadir')`.
Default build flags are `-Wall -Wextra -Wno-unused-parameter`. Option `-Db_sanitize=address,undefined`
is used for test runs. `_GNU_SOURCE` is defined (memfd_create, signalfd, timerfd).

---

## 2. Core data structures

```c
/* ---- layout.h ---- */
enum key_type {
    KT_CHAR,      /* sends `code`, with optional implied Shift (KF_SHIFTED) */
    KT_MOD,       /* Shift/Ctrl/Alt/Super; `mod` says which, `code` is L or R keycode */
    KT_CAPS,      /* sends KEY_CAPSLOCK press+release */
    KT_PAGE,      /* switches to `page` (PAGE_MAIN/SYM/FN) */
    KT_SETTINGS,  /* gear: opens/closes the settings view */
    KT_SPACER,    /* inert gap; width<0 means flex (absorbs remaining units) */
};
enum key_flag {
    KF_REPEAT      = 1 << 0, /* auto-repeats while held */
    KF_LETTER      = 1 << 1, /* label follows Caps XOR Shift */
    KF_SHIFTED     = 1 << 2, /* the glyph needs Shift (e.g. '!'): Shift is sent around it */
    KF_SPLIT_BEFORE= 1 << 3, /* split mode: this key starts the right half */
    KF_STRETCH     = 1 << 4, /* split mode: this key absorbs slack in its half */
    KF_SPLIT_DUP   = 1 << 5, /* split mode: key is split into two halves (Space) */
    KF_SMALL       = 1 << 6, /* render label at 0.75x (F-keys, PrtSc, …) */
    KF_NO_PREVIEW  = 1 << 7, /* no popup bubble (modifiers, Space, Enter, Bksp, page keys…) */
};
enum mod_id { MOD_SHIFT, MOD_CTRL, MOD_ALT, MOD_SUPER, MOD_COUNT };
enum icon { IC_NONE, IC_GEAR, IC_BKSP, IC_ENTER, IC_SHIFT, IC_TAB, IC_CAPS,
            IC_LEFT, IC_RIGHT, IC_UP, IC_DOWN };

struct key {
    const char *label;        /* normal label (UTF-8) */
    const char *shift_label;  /* label while shifted; NULL = same as label */
    enum key_type type;
    uint16_t code;            /* evdev KEY_* (raw, without +8) */
    uint16_t flags;           /* enum key_flag */
    float width;              /* in u; spacer <0 = flex */
    uint8_t mod;              /* KT_MOD: enum mod_id */
    uint8_t page;             /* KT_PAGE: target page */
    uint8_t icon;             /* enum icon; IC_NONE = draw label text */
};
struct row  { const struct key *keys; int n; };
enum page_id { PAGE_MAIN, PAGE_SYM, PAGE_FN, PAGE_COUNT };
struct page { const char *name; const struct row *rows; int nrows; float units; };

/* Computed geometry (rebuilt when the size, page, split, shape or scale changes). */
struct keybox {
    const struct key *key;
    float x, y, w, h;   /* logical px, relative to the surface */
    uint8_t row;
    uint8_t half;       /* 0 = full/left, 1 = right (split) */
};
#define MAX_BOXES 96
struct geometry {
    struct keybox box[MAX_BOXES]; int n;
    float kb_x, kb_y, kb_w, kb_h;  /* keyboard rect (excludes preview band) */
    float unit, row_h;
    int page; bool split; int width, height; /* inputs it was built for */
};

/* ---- input.h ---- */
enum mod_state { MS_OFF, MS_LATCHED, MS_LOCKED, MS_HELD };
struct modifier {
    enum mod_state st;
    uint8_t held;            /* number of slots currently holding this modifier */
    bool chorded;            /* another key was used while it was held */
    uint32_t last_tap_ms;    /* for double-tap lock */
    uint16_t code;           /* keycode currently "down" for it (L or R), 0 = none */
};
#define MAX_SLOTS 11         /* 10 touch points + 1 pointer */
struct slot {
    bool used; bool is_pointer;
    int32_t id;              /* wl_touch id; -1 for pointer */
    const struct key *key;   /* bound at down; never retargeted */
    struct keybox box;       /* copy: survives relayout and page switch */
    float x, y; bool inside; /* last position; inside = still over box */
    uint32_t down_ms;
    bool sent_shift;         /* implied Shift was pressed for KF_SHIFTED */
};
struct input {
    struct slot slot[MAX_SLOTS];
    struct modifier mod[MOD_COUNT];
    bool caps;               /* mirrored from xkb_state locked mods */
    int repeat_slot;         /* -1 = none */
    uint32_t shift_last_tap_ms;
    const struct input_ops *ops; void *ud;  /* press/release/action/redraw callbacks */
};

/* ---- config.h ---- */
enum disp_mode { MODE_POPUP, MODE_OVERLAY };
enum key_shape { SHAPE_RECT, SHAPE_SQUARE };
enum mod_mode  { MODMODE_TOGGLE, MODMODE_HOLD };
enum theme     { THEME_DARK, THEME_LIGHT };
enum lock_mode { LOCK_AUTO, LOCK_ALWAYS, LOCK_OFF };
enum start_vis { START_AUTO, START_SHOWN, START_HIDDEN };
struct config {
    enum disp_mode mode;
    bool auto_show;
    double font_scale;          /* 0.6 .. 2.0 */
    char font[64];
    enum key_shape shape;
    int radius;                 /* 0 .. 16 logical px */
    bool preview;
    enum mod_mode modmode;
    bool split; int split_gap;  /* gap in % of width, 10 .. 60 */
    bool split_portrait;
    int height_land, height_port;   /* logical px, 150 .. 600 */
    enum theme theme;
    bool repeat; int repeat_delay, repeat_rate; /* ms, Hz */
    bool shift_caps;            /* double-tap Shift toggles Caps Lock */
    bool auto_numpad;           /* numeric fields open the SYM page */
    char output[32];            /* "auto" or a connector name */
    enum lock_mode lockscreen;
    bool lock_rule;             /* re-add the Hyprland above_lock rule at runtime */
    char rotate_cmd[256];       /* "" = built-in chain */
    enum start_vis start;
    uint64_t overridden;        /* bit per key: value came from the CLI */
};
```

`struct app` (app.h) holds the Wayland objects, `struct config cfg`, `struct input in`,
`struct geometry geo`, `struct settings_view sv`, `bool visible, settings_open, locked`,
`int page`, `bool portrait`, `double scale`, `bool dirty`, `bool frame_pending`, and the
IM/visibility state (§8).

---

## 3. Key layouts

Every page (MAIN included) has **5 rows** (4 page rows plus a bottom row) and **15 u** per row,
so `row_h = H/5` and key height does not change when switching pages. Each row
is checked at startup: without a flex spacer it must sum to 15 ± 0.01; a flex spacer gets
the remainder. Labels are in quotes; `(w)` is the width in u, and a missing width means
1. `|S|` marks `KF_SPLIT_BEFORE`, `*` marks `KF_STRETCH`, `~` marks `KF_SPLIT_DUP`,
and `R` marks `KF_REPEAT`.

Letters, digits, punctuation, Space, Bksp, Del, Enter, arrows, PgUp and PgDn repeat.
Modifiers, Esc, Tab, Caps, F-keys, page keys, the gear and media keys do not.

### Page 1 — MAIN ("abc")
```
R1  Esc(1)  1 2 3 4 5 6 |S| 7 8 9 0  -  =  ⌫Bksp(2)*
R2  Tab(1.5)*  q w e r t |S| y u i o p  [  ]  \(1.5)*
R3  Caps(1.75)*  a s d f g |S| h j k l  ;  '  ⏎Enter(2.25)*
R4  ⇧Shift(2.25)*  z x c v b |S| n m  ,  .  /  ⇧Shift(2.75)*
R5  Ctrl(1.25) Super(1.25) Alt(1.25) ?123(1.25) Space(5)~ |S| Alt(1.25) Fn(1.25) ⚙(1.25) Ctrl(1.25)
```
- R1–R4 are the ANSI rows. Esc takes the `` ` `` position, as on 60% boards; `` ` `` and `~` are on ?123.
- Left modifiers use KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_LEFTMETA and KEY_LEFTALT. Right modifiers use KEY_RIGHTSHIFT, KEY_RIGHTALT (plain Alt_R in the us keymap) and KEY_RIGHTCTRL.
- Shifted labels: Q…P etc., and `;`→`:`, `'`→`"`, `,`→`<`, `.`→`>`, `/`→`?`.
- In split mode, Space (`~`) becomes two half-width Space keys: the left one ends the left half and the right one starts the right half. `|S|` on Space means the second copy goes right.
- Split halves are 7|8, 6.5|8.5, 6.75|8.25, 7.25|7.75 and 7.5|7.5.

### Page 2 — SYM ("?123")
```
R1  `  1 2 3 4 5 6 |S| 7 8 9 0  -  =  ⌫Bksp(2)*
R2  Tab(1.5)*  ! @ # $ % ^ |S| & * ( )  [  ]  \(1.5)
R3  Esc(1.75)*  ~ _ + { } |S| | ; : ' "  ⏎Enter(3.25)*
R4  ⇧Shift(2.25)*  , . / < |S| > ? Del(1.5) ←(1.25) →(1.25) ⇧Shift(2.75)*
R5  Ctrl Super Alt abc(→MAIN) Space(5)~ |S| Alt Fn ⚙ Ctrl     (same widths as MAIN R4)
```
- `! @ # $ % ^ & * ( ) ~ _ + { } | : " < > ?` are `KF_SHIFTED` (KEY_1…KEY_0, KEY_GRAVE, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_COMMA, KEY_DOT, KEY_SLASH).
- R1 is the real number row. While Shift is active it shows the shifted symbols, like a physical key cap.
- R2 has the shifted digits where the Q row sits, and `[ ] \` where they are on a PC.
- Split halves are 7|8, 7.5|7.5, 6.75|8.25 and 6.25|8.75.

### Page 3 — FN ("Fn")
```
R1  Esc(1)  F1 F2 F3 F4 F5 F6 |S| F7 F8 F9 F10 F11 F12  ⌫Bksp(2)*
R2  Tab(1.75) PrtSc(1.75) ScrLk(1.75) Pause(1.75) |S| Bri−(1.75) Bri+(1.75) Ins(1.5) Home(1.5) PgUp(1.5)
R3  Caps(1.75) ⏮(1.75) ⏯(1.75) ⏭(1.75) |S| ⏎Enter(3.5)* Del(1.5) End(1.5) PgDn(1.5)
R4  ⇧Shift(2.25) Mute(1.75) Vol−(1.75) Vol+(1.75) |S| Menu(1.75) ⇧Shift(2.75)* ↑(1.5) gap(1.5)
R5  Ctrl(1.25) Super(1.25) Alt(1.25) abc(1.25) Space(3)~ |S| Alt(1.25) ⚙(1.25) ←(1.5) ↓(1.5) →(1.5)
```
- No flex spacers and no gaps except the one beside ↑ (PC inverted T). Enter sits directly left of Del, and the right Shift directly left of ↑, as on a TKL/laptop board. The media and brightness keys use 1.75 u for easier hitting.
- Absolute columns: the Ins/Home/PgUp and Del/End/PgDn block sits at x = 10.5–15 u, directly above ← ↓ → (10.5–15). ↑ is at 12–13.5, above ↓. Together they form the PC inverted-T arrows under the 3×2 navigation block. Split mode keeps them aligned because the right halves are right-aligned (§4).
- Keycodes: KEY_F1…F12, KEY_SYSRQ, KEY_SCROLLLOCK, KEY_PAUSE, KEY_INSERT, KEY_HOME, KEY_PAGEUP, KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_COMPOSE (Menu), KEY_MUTE, KEY_VOLUMEDOWN, KEY_VOLUMEUP, KEY_BRIGHTNESSDOWN, KEY_BRIGHTNESSUP, KEY_PREVIOUSSONG, KEY_PLAYPAUSE, KEY_NEXTSONG, KEY_UP, KEY_DOWN, KEY_LEFT and KEY_RIGHT.
- The right Ctrl is dropped on this page to make room for the arrows; the left Ctrl stays.
- The gear is in the bottom row on every page: MAIN/SYM at x = 12.5–13.75 u, FN at 9.75–11 u.

### Page switching
- `?123` goes to SYM and `Fn` goes to FN. On a non-MAIN page, the key for the current page reads `abc` and returns to MAIN. FN has `abc` in the position of `?123`.
- Switching pages does not change modifier state.
- After a one-shot modifier has been used, the page stays where it is.

### Portrait
Portrait uses **the same three pages and the same 15 u grid**. The decision rests on the
numbers: the portrait logical width is 912 px, so 1 u ≈ 60.8 logical px ≈ 13 mm on the
SP6. That is larger than any phone or tablet key, and the conventional PC ordering
stays intact. The differences in portrait are:
- `height_port` is used (default 340, i.e. 68 px rows), so keys stay comfortably tall.
- Split mode is ignored unless `split_portrait=1`, since the halves would be only ~4 cm wide.
- The settings view uses fewer columns (§6).
The two flipped orientations reuse their base orientation's layout and height.

---

## 4. Geometry engine (layout.c)

Inputs are the surface width W, the keyboard height H (excluding the preview band),
the page, the shape, split, and the split gap %. Output is `struct geometry`.

```
row_h = H / 5
preview band B = cfg.preview && !locked ? round(H/5) : 0     (§5; the bubble rises 0.95*row_h above a top-row key)
kb_y = B, kb_h = H
pad = max(2, round(row_h*0.06))     # inner gap between keys, applied at draw/hit time
```

**Normal (non-split)**
- RECT: `unit = W / 15`, `kb_x = 0`.
- SQUARE: `unit = min(row_h, W/15)`, `kb_w = 15*unit`, `kb_x = (W - kb_w)/2`. The 1 u keys are then square and the block is centred.
- Keys are placed left to right with `x += width*unit`; a flex spacer gets the leftover row units.

**Split** (when `cfg.split && (!portrait || cfg.split_portrait)` and the settings view is closed)

> Revised: split mode no longer derives from the 15u rows. Each page has its own
> `split_rows` table (`layout.c`, SPLIT section): a 7u left half and an 8u right half,
> `KF_SPLIT_BEFORE` marking the first right-half key, with every row of a half summing
> exactly to its width (checked by `layout_validate`). `unit = (W - gap) / 15`
> (square: capped at the row height, centred via a symmetric inset); the left half
> starts at the inset and the right half ends at `W - inset`. Both gap edges and both
> outer edges are therefore straight on every row; outer function keys are 2u, Space
> is 3u per half. The steps below describe the original stretch-based algorithm and
> are superseded.

**Hit test**
- A point (x, y) is mapped to the box whose rect, expanded by pad/2 on every side, contains it. Boxes then tile the row with no dead gaps.
- Points in the split gap or in a spacer return NULL, and nothing happens.
- Rows are found in O(1) as `(y - kb_y)/row_h`, then the row's boxes are scanned linearly.
- Points in the preview band are outside the input region, so they never arrive.

---

## 5. Surface, rendering, HiDPI

**Layer surface**
- Layer OVERLAY, namespace `"slatekbd"`, anchored BOTTOM|LEFT|RIGHT, `keyboard_interactivity = NONE`. The surface is created on the selected output (§9).
- `set_size(0, H + B)`.
- `set_exclusive_zone(mode==POPUP && !locked ? H : 0)`.
- Input region = the keyboard rect `(0, B, W, H)`. The band above it is transparent and passes clicks through.
- Switching mode at runtime calls `set_exclusive_zone` and then commits. Changing H or B calls `set_size`, `set_exclusive_zone` and commit, then waits for `configure`.
- **Hide** destroys the layer surface and the wl_surface, so no exclusive zone is left behind. **Show** creates them again.

**Scale**
- When `wp_fractional_scale_manager_v1` is available, `scale = preferred_scale/120`. The buffer is `round(W*scale) × round((H+B)*scale)`, `wp_viewport.set_destination(W, H+B)`, and buffer_scale is 1.
- Otherwise the scale is the integer `wl_surface.preferred_buffer_scale` (bind `wl_compositor` v6), with `wl_output.scale` as a last fallback, and `set_buffer_scale(n)` is used.
- A scale change invalidates the cache and redraws.
- cairo draws with `cairo_scale(cr, scale, scale)` so all drawing code works in logical px. The pango font size is `row_h * 0.38 * font_scale` logical px; small labels are 0.75× that.

**Drawing**
- `render.c` keeps a **base layer** `cairo_image_surface` (device px) with every key in its idle state for the current (page, shifted-label state, caps, theme, geometry, scale). It is invalidated when any of those change.
- Per frame: copy base → buffer, draw pressed/active overlays (pressed keys, latched/locked modifier indicators), then draw the preview bubble.
- Damage is the whole buffer; it is a few ms at most.
- Frames are drawn only when `dirty`, and only when no frame callback is pending and a free buffer exists. `wl_surface.frame` paces the drawing. There are two shm buffers, and a busy buffer is never overwritten.

**Key visuals**
- Keys are rounded rects with radius `cfg.radius` (0 = sharp corners).
- Modifier states use colour: LATCHED gets an accent outline, LOCKED/HELD/caps an accent fill, and Caps also shows a small dot in the corner of the Shift key.
- Labels are centred, and icons are drawn as vector cairo paths (gear, ⌫, ⏎, ⇧, ⇥, arrows) so they don't depend on font coverage.

**Preview bubble**
- Shown for KT_CHAR keys without KF_NO_PREVIEW while pressed.
- Size is 1.3×key width by 1.1×row_h, centred over the key, with the bottom edge at the key's top edge plus 0.15 row_h overlap.
- It is clamped horizontally to the surface. It may cover keys above it; for the top row it sits in the band.
- Off when `preview=0` and always off while locked.

**Themes** (RGBA)
- dark: background `#1b1d22`, key `#2c3038`, mod key `#23262c`, pressed `#4a5263`, text `#e8eaed`, accent `#5b9dff`, bubble `#3a4150`.
- light: background `#d5d8de`, key `#fbfbfc`, mod key `#bcc1ca`, pressed `#9aa3b2`, text `#15171a`, accent `#1d64d8`, bubble `#ffffff`.
- In both themes the background fills only the keyboard rect; the band stays fully transparent.

---

## 6. Settings view

- Opened by tapping ⚙ (action fires on release while still over the key). It replaces the key area inside the same surface and height, so the exclusive zone does not change.
- Opening it releases all keys and modifiers first (`release_all`).
- While it is open the gear is drawn as `⌨` and closes it. The view never has to scroll: it paginates.

**Grid**
- The bottom nav row is `row_h` tall. Content is the rest: `H - row_h`, split into `rows = clamp(floor(content/48), 2, 6)` tile rows. The defaults give 4 rows in landscape (280) and 5 in portrait (340).
- `cols = clamp(floor(W/200), 2, 8)`: 6 in landscape (1368) and 4 in portrait (912).
- Each page holds `rows*cols` tiles. Tiles fill row-major, and the page index is kept when switching orientation, clamped if needed.
- Split mode does not apply to the settings view; it always uses the full width.

**Tile kinds** (each draws a small caption on top and the value large below)
- TOGGLE: tapping anywhere flips it ("On"/"Off").
- CYCLE: tapping advances to the next enum value.
- STEPPER: the left 30% is a `−` zone, the right 30% is a `+` zone, and the middle shows the value. Holding a zone auto-repeats like key repeat.
- ACTION: a button; destructive ones need a 2-step confirm (the first tap changes the label to "Tap again", which times out after 3 s).

**Tile order** (fixed)
1. Mode: Popup / Overlay
2. Auto show/hide: On / Off / Unavailable (greyed when the IM is unavailable; tapping retries getting the IM)
3. Text size: 0.6–2.0, step 0.1 (STEPPER)
4. Key shape: Rectangle / Square
5. Corner radius: 0–16, step 2
6. Key popup: On / Off
7. Modifiers: Toggle / Hold
8. Split: On / Off
9. Split gap: 10–60 %, step 5
10. Height landscape: 150–600, step 10
11. Height portrait: 150–600, step 10
12. Theme: Dark / Light
13. Key repeat: On / Off
14. Repeat delay: 200–1000 ms, step 50
15. Repeat rate: 5–50 /s, step 5
16. Shift ×2 = Caps: On / Off
17. Numbers for numeric fields: On / Off
18. Lockscreen: Auto / Always / Off
19. Output: cycles "auto" and then each connector name
20. Rotate ⭡ Landscape (transform 0) — ACTION
21. Rotate ⭢ Portrait (transform 1) — ACTION
22. Rotate ⭣ Landscape flipped (2) — ACTION
23. Rotate ⭠ Portrait flipped (3) — ACTION
24. Reset defaults — ACTION, 2-step

**Nav row**: `[◀] [page n/m] [▶] … [Hide] [Quit] [⌨ Keyboard]`
- Hide hides the keyboard (a manual hide, §8). Quit needs the 2-step confirm and then exits cleanly.
- ◀ and ▶ are greyed when there is only one page.
- The current rotation tile is highlighted, using the last known transform (§9).

**Behaviour**
- Every change applies live: relayout, re-render, and a new exclusive zone/size when needed.
- Every change marks the config dirty, and the file is saved 1 s later (debounced, atomic).
- Changing a key in the UI clears its CLI-override bit, so the new value is saved.
- **While locked:** the gear is inert (drawn dimmed with a lock glyph), so the settings view cannot be opened and Quit, rotate and saving are unreachable.

---

## 7. Modifier and touch state machine (input.c)

**Slots**
- `down(id, x, y, t)`: find a free slot (if none, ignore the touch). Hit-test, then bind the key and a copy of its box to the slot. Nothing happens on an empty hit.
- `motion(id, x, y)`: update `inside`. **Never retarget**: a slide does not change keys, which avoids accidental presses. This only matters for actions and the preview, which hides when `!inside`.
- `up(id, t)`: release that slot's key only.
- `cancel()`: release every slot and call `release_all`.
- `frame()`: if anything changed, set `dirty`. Touch down and up are applied immediately, in arrival order (a frame can carry an up and a reused-id down, or a Shift up and a letter down); only motion is coalesced per slot until `wl_touch.frame`. A down for an id that is still tracked ends the old contact first. For the pointer, events are applied on `wl_pointer.frame` (or immediately if version < 5), and button 272 counts as down/up.

**KT_CHAR**
- On **down**: the key is sent at once as press+release (`vk_tap`).
  - If KF_SHIFTED and Shift is not effectively active, Shift_L is pressed before and released after (`sent_shift`).
  - Active modifiers are already physically down (eager, see below), so the tap carries them.
  - Then, if `KF_REPEAT && cfg.repeat`, this slot becomes `repeat_slot` (the newest press wins) and a timer is set for `repeat_delay` that taps again every `1000/repeat_rate` ms.
  - Touching any non-modifier key sets `chorded = true` on every modifier with `held > 0`.
- On **up**: stop repeat if this is the repeat slot. Then consume one-shot modifiers: every modifier in MS_LATCHED that is not held goes to MS_OFF, but only once no other non-modifier slot is still down. The event order is therefore always mods↓ key↓ key↑ … mods↑.
- Sending the key at touch-down gives the lowest latency. Because press and release go out together, a key can never be stuck at the client even if the surface disappears mid-press. Repeat is ours, which avoids client-side repeat from a long touch.

**KT_MOD, "toggle" mode** (default)
| state | event | next | effect |
|---|---|---|---|
| OFF | down | HELD(tmp) | press modcode; held++ ; chorded=false |
| HELD(tmp) | up, chorded | OFF | release modcode (it was a chord, like a physical key) |
| HELD(tmp) | up, !chorded, prev state OFF | LATCHED | keep modcode down; last_tap=t |
| LATCHED | down→up within `double_tap_ms`=350 of last_tap, !chorded | LOCKED | (Shift with `shift_caps`: tap KEY_CAPSLOCK, go to OFF and release Shift instead) |
| LATCHED | down→up later than 350 ms, !chorded | OFF | release modcode |
| LOCKED | down→up, !chorded | OFF | release modcode |
| LATCHED | a char key is used (on its up) | OFF | release modcode |
| LATCHED/LOCKED | down then chord then up | unchanged | (chording does not unlatch/unlock) |

(The HELD(tmp) state stores the prior state so that the "up" transition can choose between these rows.)

**KT_MOD, "hold" mode**
- down: `held++`; when held goes 0→1, press modcode and set state HELD.
- up: `held--`; when held reaches 0, release modcode and set state OFF.
- So the modifier is active only while a finger is on it. Two fingers on the two Shift keys act as one Shift, which stays down until both lift.
- Double-tap Shift (two down/up pairs with no chord, each under 350 ms) toggles Caps Lock when `shift_caps` is on.

**Shared rules**
- Left and right keys map to the same logical modifier. The keycode actually pressed is the one the user touched (`mod.code`), so apps that care about L vs R see the truth.
- *Eager physical modifiers*: a modifier's keycode is down for as long as its state is not OFF. Locked Ctrl therefore works with touchpad clicks, and Hyprland binds see real modifier state.
- After every press or release, `keymap.c` updates the `xkb_state` and, if the serialized (depressed, latched, locked, group) changed, sends `zwp_virtual_keyboard_v1.modifiers`.
- Labels are uppercase when `KF_LETTER && (caps XOR shift_active)`. Other keys show shift_label when `shift_active` (any Shift state other than OFF).
- `release_all()` (on hide, opening settings, lock enter/exit, `touch.cancel`, output loss, SIGTERM/SIGINT/SIGHUP, exit):
  - stop repeat
  - release every sent keycode still marked down: `keymap.c` keeps a 256-bit down-set, which is authoritative
  - set every modifier to OFF with held = 0
  - send `modifiers(0, 0, locked_caps, 0)` (Caps Lock state is preserved)
  - clear all slots
  - flush the display
  - on exit, also `wl_display_roundtrip`

**KT_PAGE, KT_SETTINGS and settings tiles** fire on **up**, and only if `inside`.
**KT_CAPS** sends a KEY_CAPSLOCK tap on down.
**Timers**: repeat (one), the quit/reset confirm timeout, the hide debounce and config save.
Double-tap needs no timer because it is decided on the next tap.

---

## 8. Show/hide logic

**State in `visibility.c`**
```
bool auto_ok        # cfg.auto_show && im_available
bool im_active      # applied state after `done`
enum {OV_NONE, OV_SHOW, OV_HIDE} manual   # set by signal/CLI/settings Hide
bool locked; enum lock_mode
bool lock_hidden    # a manual --hide arrived while locked (cleared on lock enter/exit)
```

**Exact rule**
1. If `locked && cfg.lockscreen==ALWAYS`, the keyboard is visible.
2. If `locked && cfg.lockscreen==AUTO`, it is visible unless `lock_hidden`. Locking always shows it; a manual `--hide` while locked hides it until `--show` or unlock.
3. If `locked && OFF`, the keyboard is hidden and only manual show/hide applies.
4. If not locked and `manual != OV_NONE`, visibility follows `manual`.
5. If not locked, auto is on and `manual == OV_NONE`, visibility follows `im_active`.
6. If not locked and auto is off, the keyboard stays in its current state, which only manual commands change.

**Transitions**
- An IM `done` that changes `im_active` clears `manual` (the newest event wins).
- Show is applied at once. Hide from IM is debounced by 150 ms and cancelled if `activate` arrives meanwhile, which avoids flicker when focus moves between text fields.
- Manual hide/show applies at once.
- On the IM's `done` with `active && cfg.auto_numpad`, a numeric content_type opens the SYM page: purpose DIGITS, NUMBER, PHONE or PIN, or hint none.

**IM** (`im.c`)
- `zwp_input_method_manager_v2.get_input_method(seat)`.
- activate, deactivate and content_type are buffered into `pending` and applied on `done`. surrounding_text is ignored.
- **Never call grab_keyboard**, so keys can reach a locked screen.
- On `unavailable`, log a warning, destroy the object, set `im_available=false`, and show "Unavailable" in settings. Tapping that tile retries.
- When auto is switched on in settings and the IM is unavailable, retry `get_input_method`.

**Startup visibility**
- `start=auto` (default): hidden if auto show is on and the IM is available, otherwise shown.
- `--hidden` and `--shown` force it.

**Signals** (signalfd, so nothing runs in async context)

| Signal | Action |
|---|---|
| SIGUSR1 | hide |
| SIGUSR2 | show |
| SIGRTMIN | toggle |
| SIGRTMIN+1 | lock |
| SIGRTMIN+2 | unlock |
| SIGTERM, SIGINT, SIGHUP | clean quit |
| SIGCHLD | reap rotate children (§9) |

**Single instance** (`instance.c`)
- The pidfile is `$XDG_RUNTIME_DIR/slatekbd[-<instance>].pid`. It is opened with O_CREAT|O_CLOEXEC, locked with `flock(LOCK_EX|LOCK_NB)`, and the pid is written into it. The fd is held for the process lifetime.
- If the lock fails, another instance owns it. A plain `slatekbd` then prints "already running (pid N)" and exits 1.
- CLI client commands read the pid and confirm the owner holds the lock: a `flock` probe on a second open must fail with EWOULDBLOCK. They then `kill()` and exit 0, or exit 1 with "not running".
- `--lock` with no instance running instead double-forks a detached `slatekbd --shown` (setsid, stdio to /dev/null). The new instance enters lock mode itself, because it sees the notifier `locked` event or the command line flag `--locked-start`, and it quits on unlock. The command returns at once.

---

## 9. Outputs, rotation, rotate buttons

**Output selection**
- `output=auto` picks the first wl_output whose name (wl_output v4 `name`) starts with `eDP`, otherwise lets the compositor choose (NULL).
- An explicit name is matched exactly; if that output is missing, NULL is used and a warning is logged.
- When outputs are hotplugged and the keyboard is visible, it moves to the preferred output once that output appears. On `global_remove` of our output, or on `layer_surface.closed`, the surface is destroyed (after `release_all`) and recreated when the keyboard should be visible.

**Orientation detection**
- Hyprland does not re-send geometry/mode on rotation, so the inputs are:
  - (a) `zxdg_output_v1.logical_size` for the chosen output, applied on `wl_output.done`: `portrait = h > w`. This works while hidden.
  - (b) the layer-surface `configure` width.
  - (c) `wl_surface.preferred_buffer_transform` (compositor v6) while mapped, giving the exact transform 0–3. The flip state is used only to highlight the matching rotate tile.
- With output "auto"/NULL before mapping, the eDP xdg_output is used (or the first output).

**Re-layout** happens on a change of `portrait`, or of H/B (height setting, preview toggle, lock):
- Set `H = portrait ? height_port : height_land`.
- If visible, call `set_size(0, H+B)`, `set_exclusive_zone(...)` and commit.
- On the next `configure(serial, w, h)`: `ack_configure`, rebuild geometry for (w, H), reallocate buffers if the device size changed, invalidate the base layer, and redraw.
- Every `configure` is handled the same way, including width-only changes.
- Touches in flight when the layout changes keep their copied box (they are not retargeted), and their release still works.

**Rotate buttons** (`rotate.c`), for transform N on output O (the resolved name, eDP-1 by default)
1. If `rotate_cmd` is non-empty, `/bin/sh -c` runs it with `%o`→O and `%t`→N substituted. That is the whole chain.
2. Otherwise, run `hyprctl eval 'hl.monitor({output="O", transform=N}); hl.config({input={touchdevice={output="O", transform=N}, tablet={output="O", transform=N}}})'`.
3. If step 2 exits non-zero (7 = Lua error, or a legacy config), run `hyprctl --batch "keyword monitor O,preferred,auto,<scale>,transform,N ; keyword input:touchdevice:transform N"`, with `<scale>` taken from the current scale (rounded to 2 decimals).
- Children are started with fork + execvp (stdio → /dev/null). SIGCHLD arrives on the signalfd, and `waitpid(-1, WNOHANG)` reaps the child and drives the fallback step. There is no blocking.
- The change is runtime-only. The user's autorotate `hyprctl reload` re-applies their config later, and the `--help` text says so.
- Rotate tiles are disabled when `HYPRLAND_INSTANCE_SIGNATURE` is unset and `rotate_cmd` is empty.

---

## 10. Lockscreen

- **Detection, two independent sources:**
  - (a) `hyprland_lock_notifier_v1.get_lock_notification`, with `locked` and `unlocked` events
  - (b) `slatekbd --lock` / `--unlock` from noctalia hooks (SIGRTMIN+1, SIGRTMIN+2)
  - Both are idempotent and set `locked = true/false`.
- **Enter lock mode:**
  - save `pre_lock = {visible, settings_open}`
  - `release_all`
  - close settings
  - force overlay (exclusive zone 0)
  - set B = 0 (preview off)
  - turn on `no_save` and disable Quit, rotate and the gear
  - if `cfg.lock_rule && HYPRLAND_INSTANCE_SIGNATURE`, run `hyprctl eval 'hl.layer_rule({name="slatekbd", match={namespace="^slatekbd$"}, above_lock=2})'` asynchronously, then destroy and recreate the surface after the child exits (or after 300 ms at most) so the rule applies at map
  - evaluate visibility (§8)
- **Exit lock mode:** `release_all`, restore `pre_lock` visibility, and restore the mode, preview and gear from cfg (live). The surface is recreated only if it is visible.
- `lock_rule=1` (default) also runs the eval once at startup, since it changes only runtime compositor state and no files. With `lock_rule=0` this is skipped and the user's own config line is relied on.
- **Keys reach noctalia's password field** because Hyprland forces keyboard focus to the lock surface, and the virtual keyboard is not tied to an IM grab.
- **Docs to add in README** (not applied by us):
  - the Lua `hl.layer_rule` line
  - `~/.config/noctalia/slatekbd.toml` with the `session_locked` / `session_unlocked` hooks
  - autostarting `slatekbd` in the Hyprland config
- Unverified on a real lock until the user tests it: the manual test steps from `docs/research-lockscreen.md` go into README.

---

## 11. Config file

Path: `$XDG_CONFIG_HOME/slatekbd/config` (fallback `~/.config/slatekbd/config`), overridable with `-c`.

**Format**
- One `key = value` per line, with whitespace trimmed. `#` starts a comment line.
- Unknown keys warn once and are kept verbatim when saving. Bad values warn and fall back to the default.
- Booleans accept `1/0/true/false/yes/no/on/off`.
- Save writes the file in the canonical order with a header comment. It writes `config.tmp` and then calls `rename()`.
- Directories are created with mkdir -p. Nothing is saved while locked.

```
mode            = popup        # popup | overlay
auto            = 1            # auto show/hide via input-method-v2
font            = Sans
font_scale      = 1.0          # 0.6 .. 2.0
key_shape       = rect         # rect | square
key_radius      = 6            # 0 .. 16
preview         = 1
modifiers       = toggle       # toggle | hold
shift_caps      = 1            # double-tap Shift toggles Caps Lock
split           = 0
split_gap       = 30           # percent of width
split_portrait  = 0
height_landscape= 280          # logical px
height_portrait = 340
theme           = dark         # dark | light
repeat          = 1
repeat_delay    = 400          # ms
repeat_rate     = 25           # per second
auto_numpad     = 1
output          = auto         # auto | connector name (eDP-1, DP-3, ...)
lockscreen      = auto         # auto | always | off
lock_rule       = 1            # add the Hyprland above_lock layer rule at runtime
rotate_cmd      =              # empty = built-in hyprctl chain; %o output, %t transform 0-3
start           = auto         # auto | shown | hidden
```

---

## 12. CLI

```
slatekbd [options]               start the keyboard (single instance)
slatekbd --show|--hide|--toggle  control the running instance (exit 1 if none)
slatekbd --quit                  stop the running instance
slatekbd --lock|--unlock         lock-mode hooks (--lock starts an instance if none)
slatekbd --status                print pid and exit 0 if running, else exit 1

Options (override the config for this run; not saved unless changed in Settings):
  -c, --config FILE          config path
  -m, --mode popup|overlay
  -a, --auto / -A, --no-auto
  -L, --height-landscape PX  (alias -H)
  -P, --height-portrait PX
  -s, --split / -S, --no-split   [--split-gap PCT]
      --shape rect|square    --radius PX
  -f, --font-scale X         --font NAME
      --modifiers toggle|hold
      --preview / --no-preview
      --repeat / --no-repeat [--repeat-delay MS] [--repeat-rate HZ]
  -t, --theme dark|light
  -o, --output NAME|auto
      --lockscreen auto|always|off   --no-lock-rule
      --hidden / --shown     initial visibility
  -i, --instance NAME        separate pidfile slatekbd-NAME.pid (for tests)
      --no-save              never write the config file
  -v, --verbose              log to stderr (repeat for debug)
  -h, --help / -V, --version
```

`--help` also prints the signals, the config path, and the lock-screen setup lines.

---

## 13. Event loop (main.c / loop.c)

```
fds: [0] wl_display fd, [1] signalfd, [2] timerfd
loop:
  while (wl_display_prepare_read(d) != 0) wl_display_dispatch_pending(d);
  wl_display_flush(d)            # on EAGAIN also poll POLLOUT
  poll(fds, 3, -1)
  display readable ? wl_display_read_events : wl_display_cancel_read
  wl_display_dispatch_pending
  handle signalfd (drain), timerfd (fire due timers, re-arm the earliest)
  apply visibility changes; if dirty && can_draw: render + attach + damage + frame cb + commit
  if quit: release_all, roundtrip, destroy everything, unlink nothing (flock released on exit), return
```

Error handling:
- A missing layer-shell or virtual keyboard manager is fatal, with a clear message.
- A missing IM manager disables auto mode.
- A missing fractional scale falls back to integer scale.
- A missing lock notifier is fine, since the CLI hooks still work.

---

## 14. Test plan

Everything runs on the build in `build/`. Nothing runs on the live display, and the
nested Hyprland harness is always used with `--instance test`.

**Unit tests** (`meson test`, built with `-Db_sanitize=address,undefined`; no Wayland needed)
1. `test_layout`: every row of every page sums to 15 u (or has one flex spacer); no box overlaps; every keycode is valid in the US xkb keymap; every KF_SHIFTED glyph matches xkb's keysym with Shift; there is a gear on each page.
2. `test_geometry`: RECT, SQUARE and split for W = 1368/912 and H = 280/340. Checks: the halves don't cross the gap; outer edges are flush; FN's ↑ is centred over ↓ and Del/End/PgDn are aligned over ← ↓ → in both normal and split; hit-tests at key centres return the right key; points in the gap or spacers return NULL.
3. `test_input`: scripted sequences against the pure state machine, with a fake ops recorder asserting the exact press/release/modifier stream. The sequences cover:
   - toggle latch, then a key (one-shot consumed)
   - double-tap lock, then tap to unlock
   - chord in toggle mode
   - hold mode with two fingers on two modifiers
   - two Shifts at once
   - Shift double-tap toggles Caps
   - KF_SHIFTED with and without Shift active
   - repeat start/stop using timer injection
   - cancel and release_all leave an empty down-set
   - 11 simultaneous touches (the 11th is ignored)
   - slide-off cancels an action
4. `test_config`: parse defaults, then values, junk and unknown keys; check the save → parse round-trip; check that a CLI-override key keeps the file value on save.
5. `test_visibility`: a truth table over auto/IM/manual/lock/lockscreen mode, plus the debounce cancel.

**Integration** (`tests/nested/scenarios.sh`, which wraps `run_nested.sh` with `CLIENT="build/slatekbd -i test -v"`)
1. Launch with auto off. Check that `hyprctl layers -j` shows namespace slatekbd on the overlay layer with height 280 and the exclusive zone set (popup). Then `--mode overlay` gives zone 0.
2. Click `h` with vclick at its centre (computed from the same geometry by a `slatekbd --dump-geometry W H page` debug flag). target.log must show `key 35 (h) down/up` and the typed text "h".
3. Click Shift and then `h`, expecting "H". Click Ctrl and then `a`, expecting a ctrl-modified key event in target.log.
4. Click ?123 and then `!`, expecting "!". Click Fn and then ↑, expecting KEY_UP.
5. Open the gear and take a screenshot. Click Split, then close, take a screenshot, and check geometry against the dump. Click Theme and take a light screenshot.
6. Rotation: the harness cycles transforms 1, 2, 3, 0. After each step, the layer height must equal the portrait/landscape height and the width must equal the output's logical width. Take screenshots `shot-t{0..3}.png`.
7. Auto: with `-a` and `TARGET_TOGGLE_TI=1`, the target toggles text-input. The keyboard appears within 100 ms of activate and disappears within ~200 ms after deactivate.
8. Signals: `slatekbd -i test --toggle`, `--hide`, `--show`, `--status`, `--quit`. Check layer presence and exit codes. A second `slatekbd -i test` exits 1.
9. No stuck keys: run `--hide` while vclick holds Shift (press without release). target.log must show a release for every press, and the modifiers must end at 0.
10. Lock (nested): run `hyprctl eval` for the layer rule and lock with a small ext-session-lock helper (added to the harness if time permits), then check that the layer remains listed and that clicks reach it. Otherwise this is documented as a manual test.
11. Leaks: the ASan build runs scenarios 1–9 with `ASAN_OPTIONS=detect_leaks=1`, and must log no leak reports at exit.

**Manual on hardware** (README checklist): multi-touch chords, hold mode, real auto-rotate, lockscreen with the rule and hooks.
