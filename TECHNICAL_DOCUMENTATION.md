# slatekbd — technical documentation

Read this before you change slatekbd. It covers how the program is put together and
why, where each piece of behaviour lives, how to build and test it, and the traps that
have already been found. `README.md` is the user manual. `DESIGN.md` is the original
specification (useful background, but parts are superseded; see the end of this file).

## 1. What it is

A touch keyboard for Wayland, in C11 (~5,600 lines in `src/`). It talks to the compositor
directly with `wayland-client`, renders with cairo + pango, and builds its keymap
with xkbcommon. It has no toolkit and spawns no threads.
It targets Hyprland (0.56) with the noctalia shell on a Surface Pro 6, but uses only
standard or widely implemented protocols, except the optional Hyprland lock notifier.

| Protocol | Used for |
|---|---|
| `zwlr_layer_shell_v1` | the keyboard is an overlay-layer surface anchored bottom/left/right, namespace `slatekbd`, `keyboard_interactivity = NONE` |
| `zwp_virtual_keyboard_v1` | sends key events with a custom-uploaded US xkb keymap |
| `zwp_input_method_v2` | auto show/hide (activate/deactivate/content_type). **We never call `grab_keyboard`**, because Hyprland drops virtual-keyboard events from a client that holds the IM grab |
| `wp_fractional_scale_v1` + `wp_viewporter` | crisp rendering at fractional scales (falls back to `preferred_buffer_scale`) |
| `zxdg_output_v1` | output names and logical sizes (orientation detection) |
| `hyprland_lock_notifier_v1` | lock/unlock detection (optional) |

## 2. Repository layout

```
~/.config/slatekbd/
├── config                  runtime config of THIS machine (written by the Settings view; not source)
├── meson.build             build: protocol codegen, core lib, binary, unit tests
├── protocols/              vendored XMLs (layer-shell, virtual-keyboard, input-method-v2, hyprland lock notify)
├── src/                    the program (module map below)
├── tests/unit/             pure-logic tests, run by `meson test`
├── tests/nested/           integration tests in a nested, headless Hyprland
├── docs/research-*.md      research notes from the initial build (protocols, lockscreen, rotation)
├── slatekbd.conf.example   documented config with defaults (installed to /usr/local/share/doc/slatekbd)
├── README.md / DESIGN.md / TECHNICAL_DOCUMENTATION.md
└── build/                  meson build dir (generated; delete freely)
```

The project lives in the same directory as the user's runtime `config` file. The
program only ever touches `config` (and `config.tmp` while saving atomically), so the
two do not collide. Don't add a source file named `config`.

## 3. Build, install, run

```sh
cd ~/.config/slatekbd
meson setup build          # once (or after deleting build/ / moving the tree)
ninja -C build             # must stay warning-free at warning_level=2 (-Wall -Wextra)
meson test -C build        # unit tests
sudo meson install -C build    # -> /usr/local/bin/slatekbd
./build/slatekbd -vv --shown   # run the uninstalled build with debug logging
```

Dependencies: wayland-client, wayland-protocols, wayland-scanner, xkbcommon, cairo,
pango, pangocairo.

**After changing code, reinstall.** The noctalia bar button and lock hooks call
`/usr/local/bin/slatekbd`, not `build/`. If an instance is running, quit it first
(`slatekbd --quit`); a second instance refuses to start (single-instance pidfile).

## 4. Architecture

### 4.1 One struct, one loop

All state is in `struct app` (`src/app.h`), a single instance owned by `main()`.
Modules take `struct app *` and read or modify the fields they own. Nothing is global
except a few small static counters (e.g. child-process tracking in `rotate.c`).

`loop.c` runs a single `poll()` over three fds:

1. **Wayland display fd.** Uses the standard `prepare_read / read_events / dispatch_pending` dance.
2. **signalfd.** Takes SIGUSR1/2, SIGRTMIN..+2, SIGTERM/INT/HUP and SIGCHLD. Signals are blocked
   and read synchronously, so there are no async-signal-safety concerns anywhere.
3. **One timerfd.** Multiplexes `T_COUNT` logical timers (`enum timer_id`). Each has a
   deadline in `a->timer_deadline[]`. `rearm()` programs the fd for the earliest one, and
   `app_on_timer()` dispatches.

Each loop iteration starts with deferred work. First `need_recreate`, which
destroys and recreates the layer surface (used after output changes and after the
lock rule is applied). Then `surface_maybe_draw()`.

### 4.2 Module map

Pure logic with no Wayland calls, built into `slatekbd-core` and unit-tested:

| File | Responsibility |
|---|---|
| `layout.c/h` | **All key tables** (full-width and split), validation, geometry engine (`layout_build`), hit testing |
| `input.c/h` | touch/pointer slots, modifier state machine (toggle/hold), Caps, repeat. Side effects only through `struct input_ops` callbacks |
| `visibility.c/h` | decides show/hide from auto/IM/manual/lock state; returns actions (`VA_SHOW`, `VA_ARM_HIDE`, …) |
| `config.c/h` | defaults, `key = value` parse/serialise, clamping, CLI overrides, atomic save that keeps unknown lines |

Everything else:

| File | Responsibility |
|---|---|
| `main.c` | CLI parsing, client commands (`--toggle`, `--lock`, … signal the running instance), startup/shutdown order |
| `app.c/h` | glue: sizing (`app_update_size`), relayout, page switch, show/hide, settings-change side effects (`app_config_changed`), timers |
| `loop.c` | poll loop, signal handling, timer multiplexing |
| `wayland.c/h` | registry/globals, outputs, seat (touch + pointer), layer surface lifecycle, scale, frame pacing, input routing |
| `shm.c/h` | two memfd-backed ARGB8888 `wl_shm` buffers with busy/release tracking |
| `render.c/h` | cairo/pango drawing: cached **base layer** + per-frame **overlays** |
| `keymap.c/h` | xkb keymap (evdev/pc105/us), memfd keymap upload, `zwp_virtual_keyboard_v1` output, xkb_state mirror, authoritative "keys down" bitset |
| `im.c/h` | `zwp_input_method_v2` events, batched until `done` |
| `settings.c/h` | the in-surface Settings view: tile model, paging, hit testing, steppers, two-tap confirm, applying values |
| `lock.c/h` | lock mode enter/exit, Hyprland lock notifier, lock-rule recreate dance |
| `rotate.c/h` | async child processes: display rotation (`rotate_cmd` → `hyprctl eval` Lua → legacy `hyprctl keyword`) and the `above_lock` layer rule; reaped on SIGCHLD |
| `instance.c/h` | `flock`ed pidfile in `$XDG_RUNTIME_DIR` (`slatekbd[-INSTANCE].pid`), signalling, detached double-fork spawn |

### 4.3 Data flow: a key press

```
wl_touch.down (wayland.c touch_down)
  → route_down: settings open? settings_down() : layout_hit(&a->geo, x, y)
  → input_down(&a->in, id, ptr, keybox, …)            [input.c, pure]
       KT_CHAR: tap_char → ops->key(code,down) press+release immediately (never stuck)
       KT_MOD : mod_down → real modifier keycode held while active
       KT_PAGE/KT_SETTINGS: fire on release via ops->action
  → op_key (app.c) → keymap_key (keymap.c)
       updates the down-bitset + xkb_state, sends zwp_virtual_keyboard_v1.key,
       then .modifiers if the serialised mods changed
  → ops->changed → app_mark_dirty → next loop iteration draws
```

Design points to keep:

- **Characters are sent as press+release at touch-down.** A lost touch-up therefore can't
  leave a letter stuck. Repeat is our own timer (`T_REPEAT`), not the compositor's.
- **Modifiers hold their real keycode down** while latched, locked or held, so
  Ctrl/Alt/Super shortcuts work in every app. `keymap.c` is the single source of truth
  for what is down. `keymap_release_all()` releases everything (keeping Caps Lock) and is
  called on hide, settings open, touch cancel, lock enter/exit and shutdown.
- **Glyphs that need Shift** (`KF_SHIFTED`, e.g. `!` on the symbol page) wrap the key
  in a synthetic LeftShift unless Shift is already active.
- **Multi-touch:** up to 10 touch points plus 1 pointer slot. A slot stays bound to the key
  it started on (slide-off doesn't retarget). Touch down/up are processed immediately
  in arrival order; only motion is coalesced to `wl_touch.frame`.

### 4.4 Modifier state machine (`input.c`)

States: `MS_OFF, MS_LATCHED, MS_LOCKED, MS_HELD`.

- **Toggle mode (default):**
  - A tap moves OFF→LATCHED (one-shot: consumed after the next character key, see
    `consume_latched`).
  - A second tap within `DOUBLE_TAP_MS` (350) moves LATCHED→LOCKED.
  - Tapping again moves it to OFF.
  - Holding a modifier and pressing another key is a chord: the modifier returns to its
    previous state afterwards (`chorded`).
  - Double-tap Shift with `shift_caps` sends a real Caps Lock instead of locking Shift.
- **Hold mode:** a modifier is active only while at least one finger is on it
  (`held` counter, so both Shift keys work together).

`tests/unit/test_input.c` covers these sequences. Extend it when you change them.

### 4.5 Geometry (`layout.c`)

- Each page (`enum page_id`: MAIN, SYM, FN) has 5 rows, so key height never changes
  between pages.
- **Full-width tables** (`main_rows`, `sym_rows`, `fn_rows`): every row sums to exactly
  15u, except that one flex spacer (`width < 0`) may absorb the remainder.
  - `unit = W/15` for rect keys.
  - For square keys, `unit = min(row_h, W/15)`, centred.
- **Split tables** (`smain_rows`, `ssym_rows`, `sfn_rows`): written separately, not derived
  from the full rows.
  - Left half is `SPLIT_L` = 7u, right half is `SPLIT_R` = 8u.
  - `KF_SPLIT_BEFORE` marks the first key of the right half.
  - Every row of a half must sum exactly to that half's width. That makes both gap edges
    and both outer edges straight.
  - Outer function keys are 2u. Space appears once per half (3u).
  - `unit = (W − gap)/15`; the left half starts at the inset and the right half ends at
    `W − inset`.
- `layout_validate()` runs at startup and in the unit tests. It rejects a table whose
  rows don't sum correctly, has no gear key, or overflows `MAX_BOXES`.
  `tests/unit/test_layout.c` also checks every label against the xkb keymap (with and
  without Shift).
- **Preview band:** when key popups are on, the surface is taller than the keyboard by
  `layout_band(H)` (= H/5). The band is transparent, so the popup bubble can draw above
  the top row. It is excluded from the input region and from the exclusive zone.

### 4.6 Rendering (`render.c`)

`render_frame()` keeps a cached **base layer**: all idle keys, or the settings tiles.
The cache is keyed on page, shift, caps, settings-open, locked, theme, buffer size,
scale and a generation counter. `render_invalidate()` bumps the generation.

Each frame copies the base and then draws **overlays**: pressed keys, modifier
highlights and preview bubbles. Drawing is in logical px under `cairo_scale(scale)`.

`surface_maybe_draw()` only draws when all of these hold:
- the surface is configured;
- it is dirty;
- no frame callback is pending (frame pacing);
- a buffer is free.

With fractional scale it renders at `width*scale` and sets the viewport destination.
Otherwise it uses an integer `buffer_scale`.

### 4.7 Surface lifecycle and sizing

- **Show** = `surface_create()`. **Hide** = `surface_destroy()`; the surface is destroyed,
  not unmapped.
- On creation, sizing goes through `app_update_size()`:
  - `H` = the landscape or portrait height from config;
  - `B` = the preview band;
  - `set_size(0, H+B)`;
  - exclusive zone = `H` in popup mode, `0` in overlay mode or while locked.
- The width comes from `layer_configure`.
- **Orientation:** `app_check_orientation()` compares the output's logical size from
  xdg_output. Portrait means `lh > lw`. Hyprland does not re-send `wl_output.geometry` on
  rotation, so the transform is only used for highlighting the current rotation tile.
- **Output choice:** `cfg.output` (default `auto`) means the first output whose name starts
  with `eDP`/`LVDS`/`DSI`, i.e. the touch panel, otherwise the compositor's choice.

### 4.8 Visibility (`visibility.c`, pure)

`vis_want()` priority:

1. **Lock mode.** With `lockscreen=auto`:
   - Stay visible unless manually hidden.
   - Exception: if the lock surface has activated the input method during this lock
     (`lock_im`) and auto is on, follow the IM, so the keyboard auto-hides on the lock
     screen.
   - Until that IM activation is seen, it stays up, because the password must always be
     typeable.
2. **Manual override** (`OV_SHOW`/`OV_HIDE` from the CLI, signals or the Hide button).
   It is cleared by the next IM state change or a fresh `activate`.
3. **Auto:** follow `im_active` when an IM is available.

Show is immediate. A hide caused by the IM is debounced by `HIDE_DEBOUNCE_MS` (150), so
focus moving between two fields doesn't flicker.

Known compositor limitation: Hyprland 0.56 sends no new `activate` when an app re-requests
the keyboard on an already-focused field. So after a manual Hide, tapping the same field
doesn't bring the keyboard back.

### 4.9 Lock screen

Hyprland only draws layer surfaces above an `ext-session-lock` surface when a layer
rule `above_lock = 2` matches them; 2 means drawn and touchable.

- **This machine** has the permanent rule in `~/.config/hypr/config/windowrules.lua`.
- slatekbd also adds it at runtime with `hyprctl eval` when `lock_rule=1`. After adding it,
  it recreates its surface once so the rule applies (`lock_recreate_pending` /
  `T_LOCK_RECREATE`).

Lock is detected two ways:
- `hyprland_lock_notifier_v1`, when an instance is already running;
- noctalia hooks in `~/.config/noctalia/config.toml` (`[hooks] session_locked` /
  `session_unlocked`). These run `slatekbd --lock` / `--unlock`.

`--lock` with no running instance spawns a detached one with `--locked-start`. That
instance **quits on unlock** (`lock_exit`), so the keyboard appears on the lock screen
without being on the desktop at login.

Lock mode forces overlay, disables the key popup (shoulder-surfing), makes the gear
inert, and blocks Quit, rotation and config saving.

**Button mode** (`lock_button=1`, default). When locked, `a->collapsed` is set and
`surface_apply_size()` re-anchors the *same* layer surface to bottom|right at
`BTN_D`×`BTN_D` (36) with margins `BTN_MARGIN` (16) and an extra right margin of
`button_offset × BTN_PITCH` (44). `layer_configure` ignores button-sized configures for
the keyboard width. `route_down/up` treat a tap as "expand". `render_button()` draws the
disc. In lock mode the gear key (`op_action`) calls `app_set_collapsed(a, true)` and is
drawn as a keyboard with a down arrow. `vis_want()` keeps the surface up while locked
(`vis.lock_button`) and the IM is ignored.

**Login screen** (`--greeter`): permanent lock mode. `lock_exit` is a no-op, no lock rule,
no config saving. greetd runs `greeter/slatekbd-greeter-session` (installed to
`/usr/local/bin`). It mirrors `noctalia-greeter-session` (`dbus-run-session`, own
`XDG_RUNTIME_DIR`, here a `mktemp -d` directory) but runs `Hyprland -c
/usr/local/share/slatekbd/greeter-hyprland.lua`. That config makes the greeter
fullscreen, runs `slatekbd --greeter --button-offset 3 -c /dev/null`, and runs
`noctalia-greeter; hyprctl dispatch "hl.dsp.exit()"`, so the session ends when the
greeter does. The stock greeter compositor (`noctalia-greeter-compositor`) only
implements xdg-shell, with no layer-shell, virtual-keyboard or input-method, which is why
it had to be replaced. Logs: `journalctl -t slatekbd-greeter`.

Security rules for the login session (from a review; keep them):
- **Never use `start-hyprland` here.** Its watchdog restarts a crashed Hyprland in
  *safe mode*, which ignores the config and enables default binds (SUPER+R runner,
  SUPER+Q terminal): a pre-login command prompt. Started directly, a crash just ends the
  session and greetd starts a fresh greeter.
- The script falls back to the stock `noctalia-greeter-session` when the config is
  missing or fails `Hyprland --verify-config`.
- The greeter config defines no binds; keep it that way.
- slatekbd never logs keycodes while locked (`keymap.c`), so `-vv` on the lock or login
  screen cannot leak a password into the journal.
- The pidfile fallback directory in `/tmp` is only used if it is owned by us and mode
  0700; pidfiles are opened with `O_NOFOLLOW`.
- Accepted risk: the `above_lock = 2` rule matches *any* layer surface named `slatekbd`,
  so a malicious program already running as the user could draw a fake lock screen
  above the real one. Such a program can add the same rule itself via `hyprctl eval`,
  so this lowers the bar rather than adding a capability. Removing the static rule from
  `windowrules.lua` (relying on `lock_rule=1`) narrows it to while slatekbd runs.

### 4.10 Settings view

The settings view is drawn inside the same surface, opened by the gear key.
`settings.c` holds a fixed tile list (`enum tile_id`) in 4 kinds:
- toggle;
- cycle;
- stepper, with −/+ zones and auto-repeat (`T_SET_REPEAT`);
- action.

The grid is `floor(W/200)` columns and paginates. Quit and Reset need a second tap within
a timeout (`T_CONFIRM`). Changing a value calls `config_set` and then
`app_config_changed(key)`, which applies the side effect live and schedules a debounced
atomic save (`T_SAVE`, 1 s).

### 4.11 Config file

`$XDG_CONFIG_HOME/slatekbd/config` (here `~/.config/slatekbd/config`), `key = value`.

- Unknown lines are preserved on save.
- CLI flags override values for one run only. `cfg.overridden` marks them, and the
  file's original value is written back on save.
- `-c /dev/null` disables saving, which the tests use.

## 5. Common changes, step by step

**Change or add a key / rearrange a page**

1. Edit the full-width row in `layout.c`. The row must still sum to 15u (or use exactly
   one flex spacer).
2. Edit the **split** table for the same page too. Each half must sum to `SPLIT_L` /
   `SPLIT_R`, with exactly one `SB` per row.
3. Use the macros: `LT` (letter), `CH` (char with shift label), `SH` (glyph needing
   Shift), `FK` (function key: width, flags, icon), `MODK`, `PG`, `GEAR`, `SPC`, `SPACE`.
   Flags: `R` repeat, `KF_SMALL` small label, `KF_DIM` modifier colour.
4. Run `meson test -C build`. `test_layout` validates sums and checks labels against xkb;
   `test_geometry` checks alignment, gaps, hit tests and edges.
5. Optionally look at it: `./build/slatekbd --split --dump-geometry 1368 280 main` prints the key boxes,
   or take a nested-compositor screenshot (§6).

**Add a page:** extend `enum page_id`, add both table sets plus a `layout_pages[]` entry,
and add a `PG(...)` key somewhere to reach it.

**Add a setting**

1. `config.h`: add a field to `struct config` and a `CK_*` entry. Append it **before
   `CK_COUNT`** and keep `CK_COUNT ≤ 64`, because `overridden` is a bitmask.
2. `config.c`: add the name, comment, default (`config_defaults`), parse
   (`config_set`), format (`config_format_value`) and clamp.
3. `settings.c`: add a `TL_*` tile, its kind (`tile_kind`), its caption/value
   (`settings_tile_info`) and its apply logic (`fire` / `stepper`).
4. `app.c` `app_config_changed()`: add the live side effect.
5. `main.c`: add a CLI flag (an `OVR(CK_…, value)` entry), and update `--help`,
   `README.md` and `slatekbd.conf.example`.
6. `tests/unit/test_config.c`: add a round-trip case.

**Add a CLI control command:** add a signal in `loop_handle_signals` and map the flag to
it in `main.c` (client mode signals the pid from the pidfile).

## 6. Testing

- **Unit tests:** `meson test -C build`, five suites, no compositor needed. For
  sanitizers:
  ```
  meson setup build-asan -Db_sanitize=address,undefined
  meson test -C build-asan
  ```
- **Integration tests:** `tests/nested/scenarios.sh [BINARY] [scenario numbers…]`.
  - It starts a second Hyprland with its own instance signature and socket, adds a
    headless 2736x1824@2 output `SLATE-1` and removes the visible nested window, so
    **nothing touches the live session**.
  - Helper clients (built by `tests/nested/build.sh` on first use):
    - `target`: a text-input-v3 app that logs the keys it receives;
    - `vclick`: virtual-pointer clicks at coordinates;
    - `locker`: an ext-session-lock client;
    - `probe`.
  - It covers: popup/overlay zones, typing and modifiers, pages, settings, the 4
    rotations, auto show/hide, CLI/signals and single instance, no stuck keys, lock
    screen, and `--lock` spawn/exit.
  - Stop the compositor afterwards with `tests/nested/nest.sh stop`.
  - **Do not `pkill -f` a pattern that appears in your own shell's command line**: it
    kills the shell.
- **Screenshots:** start the nest (`tests/nested/nest.sh start 60`), source
  `tests/nested/build/nest.env`, and export `WAYLAND_DISPLAY=$NSOCK
  HYPRLAND_INSTANCE_SIGNATURE=$NSIG`. Then run `slatekbd -i shot -c /dev/null --shown
  --no-lock-rule [--split]` and `grim out.png`.
- **What only real hardware covers:** real multi-finger touch (the nest only has a
  virtual pointer), hold-mode chords, the auto-rotate script, and the real noctalia lock
  screen password field.

When testing on the live session, use a separate instance name (`-i NAME`) and
`-c /dev/null --no-lock-rule`. Never send synthetic keys into the user's windows, and
never change monitor transforms on the live session.

## 7. Integration on this machine

| What | Where |
|---|---|
| Binary | `/usr/local/bin/slatekbd` (from `sudo meson install -C build`) |
| User config | `~/.config/slatekbd/config` |
| Bar button (keyboard icon, left of the clock) | widget definition `[widget.slatekbd]` in `~/.config/noctalia/config.toml`; placement `center = [ "slatekbd", "clock", … ]` in `~/.local/state/noctalia/settings.toml`. **noctalia's settings GUI stores the bar layout there and it overrides `config.toml`** |
| Button action | `slatekbd --status && slatekbd --quit \|\| setsid -f slatekbd` |
| Lock hooks | `[hooks]` in `~/.config/noctalia/config.toml` → `slatekbd --lock` / `--unlock` |
| Above-lock layer rule | `~/.config/hypr/config/windowrules.lua` (`above_lock = 2`, `no_anim`) |
| Login screen | `/etc/greetd/config.toml` `command = "/usr/local/bin/slatekbd-greeter-session"` (backup `config.toml.bak-slatekbd`; revert to `/usr/bin/noctalia-greeter-session` from a TTY if the login screen ever fails) |
| Autostart | none, on purpose. The keyboard runs only when started from the bar, from a command, or by the lock hook |
| Backups made during setup | `*.bak-slatekbd` next to each edited file |

`noctalia config validate` checks the noctalia side; `hyprctl configerrors` checks Hyprland.

## 8. Gotchas found so far

- **One input method per seat.** If another IM (wvkbd `--auto`, fcitx5) holds the seat,
  `im_unavailable` fires and auto show/hide is greyed out. Tapping the tile retries.
- **Never call `zwp_input_method_v2.grab_keyboard`** (see §1).
- **`hyprctl reload` drops runtime layer rules.** The user's autorotate and dock scripts
  reload often, so the permanent rule in `windowrules.lua` is what really keeps the lock
  screen working.
- **Rotation via the Settings buttons lasts only until the next `hyprctl reload`.** Set
  `rotate_cmd` to integrate with an orientation-aware reload script.
- **Pidfile semantics:** `instance_acquire` verifies it locked the file that is actually
  on disk, and only unlinks it if it is still its own. Don't simplify this; two
  instances could start.
- **Child processes** (hyprctl) are spawned with SIGPIPE reset to default and reaped on
  SIGCHLD. Never `waitpid` blocking in the loop.
- **Electron/Chromium** only drive the input method with `--enable-wayland-ime`.
  XWayland apps never do.

## 9. Relationship to DESIGN.md

`DESIGN.md` is the spec the first version was built from. It is still accurate for the
overall architecture. It is out of date on these points:

- **Split mode:** now uses dedicated tables (§4.5); the stretch algorithm there is
  marked superseded.
- **`--lock`-spawned instances:** now quit on unlock.
- **Lock-screen auto-hide:** now follows the IM once the lock surface has activated it
  (§4.8).
- **Number row:** the MAIN page gained one (Esc in the backtick position).

When the two disagree, the code and this file win.
