# slatekbd

An on-screen touch keyboard for Wayland, built for 2-in-1 tablets and convertibles.
It uses standard protocols (wlr-layer-shell, virtual-keyboard-v1, input-method-v2),
and has extra integration for Hyprland: the lock screen, the login screen and
display rotation.

- **Familiar layout.** It uses a PC-style US QWERTY layout. Esc, Tab, Caps, Shift,
  Ctrl, Super and Alt are where you expect them, and the keys send real keycodes,
  so shortcuts like Ctrl+C and Super+… work.
- **Three pages:** `abc`, `?123` (numbers and symbols) and `Fn` (F-keys, the
  navigation block, arrows and media keys).
- **Popup or overlay mode.** Popup mode reserves screen space so windows resize
  above the keyboard. Overlay mode floats over windows.
- **Auto show/hide.** The keyboard appears when a text field is focused and hides
  when focus leaves, using input-method-v2. You can also drive it from the CLI,
  signals, a keybind or a bar button.
- **Settings inside the keyboard.** The ⚙ key opens settings for text size, key
  shape, key popup, modifier toggle/hold, split keyboard, heights, theme, key
  repeat, lock screen behaviour, output and rotation. Changes are saved
  automatically.
- **Rotation aware.** It follows all four orientations, with separate heights for
  landscape and portrait.
- **Lock and login screen.** On Hyprland it can stay usable above the session lock,
  and it ships a greetd session for typing on the login screen.
- **Safe multi-touch.** Modifiers can be held while another finger types. Every key
  is released on hide, cancel and exit, so nothing gets stuck.

## Requirements

- A Wayland compositor with `wlr-layer-shell` and `virtual-keyboard-unstable-v1`
  (Hyprland, Sway, river, labwc, Wayfire, …).
- For auto show/hide: `input-method-unstable-v2` in the compositor, and apps that
  use `text-input-v3`.
- Lock screen, login screen and the built-in rotate buttons are **Hyprland-only**.
  They were developed against Hyprland 0.56.

slatekbd is developed and tested on Hyprland with a Surface Pro 6. Reports from
other compositors and devices are welcome.

## Install

### Arch Linux (AUR)

```sh
paru -S slatekbd      # or yay -S slatekbd, or build it with makepkg
```

### From source

Build dependencies: `meson`, `ninja`, a C11 compiler, `wayland` (client and
scanner), `wayland-protocols`, `libxkbcommon`, `cairo` and `pango`.

```sh
meson setup build
meson compile -C build
meson test -C build            # unit tests, no Wayland session needed
sudo meson install -C build    # installs to /usr/local by default
```

Use `meson setup build --prefix=/usr` to install to `/usr` instead.

The install contains:

| Path | What |
|---|---|
| `bin/slatekbd` | the keyboard |
| `bin/slatekbd-greeter-session` | greetd session for the login screen (Hyprland + noctalia-greeter) |
| `share/slatekbd/greeter-hyprland.lua` | the Hyprland config used by that session |
| `share/doc/slatekbd/slatekbd.conf.example` | annotated sample config |

## Quick start

```sh
slatekbd                 # start (single instance; auto show/hide is on by default)
slatekbd --shown         # start visible
slatekbd --toggle        # show/hide the running instance (also --show, --hide)
slatekbd --status
slatekbd --quit
```

Tap ⚙ on the keyboard to change settings. They are saved to
`$XDG_CONFIG_HOME/slatekbd/config` (by default `~/.config/slatekbd/config`).

## Hyprland setup

The examples use Hyprland's Lua config. A hyprlang equivalent is shown below each
one.

### Autostart

```lua
-- inside your hl.on("hyprland.start", ...) block
hl.exec_cmd("slatekbd")
```

```ini
exec-once = slatekbd
```

Make sure `slatekbd` is on the `PATH` that Hyprland itself sees. `/usr/bin` and
`/usr/local/bin` usually are; `~/.local/bin` often is not. Otherwise, use the full
path.

If you are replacing another on-screen keyboard (for example `wvkbd --auto`) or
running an IME such as fcitx5, stop it first. Only one input method can own a seat
(see [Auto show/hide](#auto-showhide)).

### Layer rules

The first rule is required for the lock screen; the second is optional.

```lua
-- Draw slatekbd above the session lock and let it take touches there.
hl.layer_rule({ name = "slatekbd", match = { namespace = "^slatekbd$" }, above_lock = 2 })
-- Optional: no slide/fade animation when the keyboard appears and disappears.
hl.layer_rule({ name = "slatekbd-noanim", match = { namespace = "^slatekbd$" }, no_anim = true })
```

```ini
layerrule = above_lock 2, match:namespace ^slatekbd$
```

The layer namespace is always `slatekbd`. Don't add `blur` or `dim_around`: the
key popup band is transparent and would be blurred.

### A toggle key, gesture or bar button

```lua
-- a key
hl.bind(mainMod .. " + K", hl.dsp.exec_cmd("slatekbd --toggle"))

-- a 4-finger swipe up on the touchpad
hl.gesture({ fingers = 4, direction = "up", action = function() hl.exec_cmd("slatekbd --toggle") end })
```

`hl.gesture` only handles touchpad swipes. In tablet mode, the most reliable toggle
is a button in your bar (Waybar, noctalia, …) that runs `slatekbd --toggle`. The
Hide button in Settings and auto show/hide also work without any binding.

## Auto show/hide

slatekbd registers as an input method, so the keyboard appears when a text field
gains focus and hides when focus leaves. Apps need text-input-v3 support: GTK3/4,
Qt6, and Chromium/Electron started with `--enable-wayland-ime`. XWayland apps never
trigger it.

Only one input method can own a seat. If another one is running (`wvkbd --auto`,
fcitx5, …), slatekbd logs a warning and Settings shows *Auto show/hide:
Unavailable*. Stop the other program, then tap that tile to retry. Manual show/hide
always works.

If you hide the keyboard while a field is focused, it stays hidden until focus
leaves the field or you show it again. Hyprland 0.56 does not re-send `activate`
when you tap an already focused field, so tapping the same field again does not
bring the keyboard back.

## Layout

```
abc   Esc 1 2 3 4 5 6 7 8 9 0 - =      Bksp
      Tab  q w e r t y u i o p [ ]     \
      Caps a s d f g h j k l ; '       Enter
      Shift z x c v b n m , . /        Shift
      Ctrl Super Alt ?123 [  Space  ] Alt Fn ⚙ Ctrl

?123  ` 1 2 3 4 5 6 7 8 9 0 - =        Bksp
      Tab  ! @ # $ % ^ & * ( ) [ ]     \
      Esc  ~ _ + { } | ; : ' "         Enter
      Shift , . / < > ? Del ← →        Shift
      Ctrl Super Alt abc [  Space  ] Alt Fn ⚙ Ctrl

Fn    Esc F1 … F12                     Bksp
      Tab PrtSc ScrLk Pause  Bri− Bri+      Ins  Home PgUp
      Caps Prev Play Next      Enter        Del  End  PgDn
      Shift Mute Vol− Vol+     Menu Shift        ↑
      Ctrl Super Alt abc [ Space ] Alt ⚙   ←    ↓    →
```

- **Shift** shows uppercase letters and the shifted symbols, the same as a physical
  keycap. Number and punctuation keys show their shifted symbol as a small hint in
  the corner.
- **Caps Lock:** use the Caps key, or double-tap Shift (setting *Shift ×2 = Caps*).
- **Modifiers, Toggle mode (default):**
  - a tap latches the modifier for the next key
  - a double-tap within 350 ms locks it
  - a further tap releases it
  - holding the modifier while tapping another key works as a normal chord
- **Modifiers, Hold mode:** the modifier is active only while a finger is on it.
- **Key repeat:** letters, digits, punctuation, Space, Bksp, Del, Enter, arrows,
  PgUp and PgDn repeat while held. The delay and rate are configurable.
- **Key popup:** a preview bubble is drawn above the pressed key, in a transparent
  band that touches pass through. The band does not count toward the reserved
  space.
- **Split:** a 7u left half and an 8u right half sit at the screen edges, with an
  empty middle (*Split gap*, % of the width). It applies in landscape; set
  `split_portrait = 1` to also split in portrait.
- **Key shape:** *Rectangle* stretches keys to the full width. *Square* makes 1u
  keys square and centres the keyboard.
- **Portrait** uses the same layout with its own height. Every page has 5 rows, so
  the height stays the same when you switch pages.

## Settings

Tap ⚙ (bottom row, every page) to open these tiles:

- Mode
- Auto show/hide
- Text size
- Key shape and corner radius
- Key popup
- Modifiers
- Split keyboard and split gap
- Height (landscape and portrait)
- Theme
- Key repeat, repeat delay and repeat rate
- Shift ×2 = Caps
- Numbers for numeric fields
- Lockscreen
- Output
- Rotate (the four display orientations)
- Reset defaults (tap twice)

The −/+ steppers repeat while held. In the bottom row, ◀ / ▶ change the settings
page, **Hide** hides the keyboard, **Quit** quits (tap twice) and ⌨ returns to the
keys. Changes apply immediately and are saved a second later.

## Rotation

The keyboard follows the output's orientation by itself, so it works with any
autorotate setup without configuration.

On Hyprland, the four **Rotate** buttons in Settings rotate the output the keyboard
is on. They also rotate the touchscreen and tablet mapping, so touch stays aligned.
The change is runtime-only: the next `hyprctl reload` re-applies your config. To
route the buttons through your own rotation script instead, set `rotate_cmd`. In
it, `%t` is replaced by the transform (0–3) and `%o` by the output name:

```
rotate_cmd = echo %t > $XDG_RUNTIME_DIR/hypr-orientation && hyprctl reload
```

## Lock screen (Hyprland)

Hyprland only draws layer surfaces above an ext-session-lock if a layer rule with
`above_lock` matches them; `above_lock = 2` also lets them take touches. Keyboard
focus stays on the lock surface, so keys typed with slatekbd go to the password
field.

**What slatekbd does by itself:**

- It detects the lock through `hyprland_lock_notifier_v1` and switches to lock
  mode:
  - overlay mode, no key popup (so the password can't be read from the bubbles)
  - Settings is disabled
  - the config file is not written
  - all keys are released when entering and leaving lock mode
- With `lock_rule = 1` (the default) it adds the `above_lock` rule to the running
  Hyprland through `hyprctl eval` at startup and on lock. No files are edited.
  `hyprctl reload` drops the rule, so adding it to your config as well (see
  [Layer rules](#layer-rules)) is recommended. Turn this off with `--no-lock-rule`
  or `lock_rule = 0`.
- With `lock_button = 1` (the default), only a small round keyboard button is shown
  in the bottom-right corner while locked. Tapping it opens the keyboard, and the
  key in the ⚙ position folds it back. With `lock_button = 0`, the full keyboard is
  shown according to `lockscreen` (`auto` | `always` | `off`).
- An instance started by `slatekbd --lock` (when none was running) quits on unlock.

**Lock hooks (optional).** If your locker or shell can run commands on
lock/unlock, use these as a second lock signal. With noctalia, for example, create
`~/.config/noctalia/slatekbd.toml`:

```toml
[hooks]
session_locked   = "slatekbd --lock"
session_unlocked = "slatekbd --unlock"
```

**Status:** in a nested Hyprland 0.56 test with an ext-session-lock client,
slatekbd was drawn above the lock and taps reached the lock surface. It has not yet
been verified against every real lock screen (noctalia, hyprlock, …), so keep a
hardware keyboard nearby the first time you rely on it to unlock. Running
`slatekbd -v` shows the lock-mode messages.

## Login screen (greetd)

`slatekbd-greeter-session` runs `noctalia-greeter` inside a minimal Hyprland
together with slatekbd in greeter mode (`--greeter`). It requires greetd, Hyprland
and noctalia-greeter. To use it, point greetd at it in `/etc/greetd/config.toml`:

```toml
[default_session]
command = "/usr/bin/slatekbd-greeter-session"   # /usr/local/bin/... for a source install
```

If the bundled Hyprland config is missing or fails `Hyprland --verify-config`, the
script falls back to the stock `noctalia-greeter-session`. To revert, set
`command` back to your previous greeter. `--button-offset N` moves the keyboard
button N slots to the left so it sits next to the greeter's own buttons.

## Command-line reference

Control commands talk to the running instance and exit. They exit with code 1 if
no instance is running.

| Command | Effect |
|---|---|
| `--show`, `--hide`, `--toggle` | show / hide / toggle the keyboard |
| `--quit` | stop the running instance cleanly (all keys released) |
| `--lock`, `--unlock` | lock hooks; `--lock` starts a detached instance if none is running |
| `--status` | print `running (pid N)` and exit 0, or exit 1 |

Startup options override the config file for that run only. They are not saved
unless you change the same setting in Settings.

| Option | Meaning |
|---|---|
| `-c`, `--config FILE` | config file (default `$XDG_CONFIG_HOME/slatekbd/config`) |
| `-m`, `--mode popup\|overlay` | popup reserves screen space, overlay floats over windows |
| `-a`, `--auto` / `-A`, `--no-auto` | auto show/hide when a text field is focused |
| `-L`, `-H`, `--height-landscape PX` | height in landscape, 150–600 (default 280) |
| `-P`, `--height-portrait PX` | height in portrait, 150–600 (default 340) |
| `-s`, `--split` / `-S`, `--no-split` | split keyboard |
| `--split-gap PCT` | empty middle in % of the width, 10–60 (default 30) |
| `--split-portrait` / `--no-split-portrait` | also split in portrait |
| `--shape rect\|square` | key shape |
| `--radius PX` | key corner radius, 0–16 |
| `-f`, `--font-scale X` | text size, 0.6–2.0 |
| `--font NAME` | Pango font family (default `Sans`) |
| `--modifiers toggle\|hold` | modifier behaviour (see [Layout](#layout)) |
| `--shift-caps` / `--no-shift-caps` | double-tap Shift toggles Caps Lock |
| `--preview` / `--no-preview` | key press popup |
| `--repeat` / `--no-repeat` | key repeat |
| `--repeat-delay MS`, `--repeat-rate HZ` | repeat timing (200–1000 ms, 5–50 /s) |
| `--numpad` / `--no-numpad` | numeric text fields open the `?123` page |
| `-t`, `--theme dark\|light` | colour theme |
| `-o`, `--output NAME\|auto` | output to show on (`auto` = the internal panel) |
| `--lockscreen auto\|always\|off` | visibility while the session is locked |
| `--no-lock-rule` | do not add the Hyprland `above_lock` rule at runtime |
| `--lock-button` / `--no-lock-button` | while locked, keep the keyboard behind a round button |
| `--greeter` | login-screen mode: permanently locked, button shown |
| `--button-offset N` | move the lock button N slots to the left |
| `--rotate-cmd CMD` | custom command for the Rotate buttons (`%o` output, `%t` transform) |
| `--hidden` / `--shown` | initial visibility |
| `-i`, `--instance NAME` | separate pidfile, for running a second independent instance |
| `--no-save` | never write the config file |
| `-v`, `--verbose` | log to stderr; `-vv` for debug output |
| `--dump-geometry W H PAGE` | print key rectangles (`main`, `sym`, `fn`) and exit |
| `-h`, `--help` / `-V`, `--version` | help / version |

Invalid values are rejected with a message and exit code 2.

**Signals:**

| Signal | Action |
|---|---|
| `SIGUSR1` | hide |
| `SIGUSR2` | show |
| `SIGRTMIN` | toggle |
| `SIGRTMIN+1` | lock |
| `SIGRTMIN+2` | unlock |
| `SIGTERM`, `SIGINT`, `SIGHUP` | clean quit |

The pidfile is `$XDG_RUNTIME_DIR/slatekbd.pid`, or `slatekbd-NAME.pid` with
`-i NAME`.

## Configuration

The config file is plain `key = value`, one per line, with `#` comments.
slatekbd writes it when you change Settings and keeps keys it doesn't know. Every
key is documented in
[`slatekbd.conf.example`](slatekbd.conf.example), which is installed to
`share/doc/slatekbd/`:

`mode`, `auto`, `font`, `font_scale`, `key_shape`, `key_radius`, `preview`,
`modifiers`, `shift_caps`, `split`, `split_gap`, `split_portrait`,
`height_landscape`, `height_portrait`, `theme`, `repeat`, `repeat_delay`,
`repeat_rate`, `auto_numpad`, `output`, `lockscreen`, `lock_rule`, `lock_button`,
`rotate_cmd`, `start`.

## Development

- [`TECHNICAL_DOCUMENTATION.md`](TECHNICAL_DOCUMENTATION.md): architecture, how to
  change things, and tests.
- [`DESIGN.md`](DESIGN.md): the original design spec.
- [`CHANGELOG.md`](CHANGELOG.md): release history.

Releasing (GitHub tag + AUR package) and the list of known open work are in
TECHNICAL_DOCUMENTATION.md §8 and §9.

### Source layout

| File | Role |
|---|---|
| `src/main.c` | CLI, client mode (signals the running instance), startup/shutdown |
| `src/app.c`, `app.h` | shared state, sizing, visibility, live settings apply, timers |
| `src/loop.c` | poll loop, signalfd, timerfd deadline table |
| `src/instance.c` | flock'd pidfile, signalling, detached spawn for `--lock` |
| `src/wayland.c` | registry, outputs (xdg-output), seat (multi-touch + pointer), layer surface, fractional scale, frame pacing |
| `src/shm.c` | double-buffered memfd shm buffers |
| `src/layout.c` | key tables, geometry engine (normal / square / split), hit testing |
| `src/render.c` | cairo/pango rendering, cached base layer, vector icons, preview bubbles, themes |
| `src/keymap.c` | xkb keymap upload, virtual keyboard, authoritative down-set, modifier sync |
| `src/input.c` | pure touch/modifier state machine (unit tested) |
| `src/settings.c` | settings tiles, pagination, steppers, confirm, apply |
| `src/config.c` | key=value config, CLI overrides, atomic save |
| `src/im.c` | input-method-v2 (double-buffered state, unavailable handling) |
| `src/visibility.c` | show/hide rules (unit tested) |
| `src/rotate.c` | async rotate command chain and lock layer rule |
| `src/lock.c` | lock notifier and lock mode |
| `protocols/` | vendored wlr-layer-shell, virtual-keyboard, input-method-v2, hyprland-lock-notify |

### Tests

- `meson test -C build` runs five unit suites: layout, geometry, input state
  machine, config and visibility. Configure with `-Db_sanitize=address,undefined`
  to run them under the sanitizers.
- `tests/nested/scenarios.sh [binary]` runs integration tests in a nested,
  headless Hyprland. Build the helpers with `tests/nested/build.sh`, and start and
  stop the nested session with `tests/nested/nest.sh start|stop`. The tests cover:
  - popup and overlay zones
  - typing, modifiers, pages and key repeat
  - settings
  - all four rotations
  - auto show/hide
  - signals, CLI and single instance
  - no stuck keys on hide
  - the session-lock path
- Real multi-touch can't be tested in the nested session, because there is no
  virtual touch protocol. Check multi-touch chords and Hold mode by hand on a
  touchscreen.

## Acknowledgements

slatekbd studied [wvkbd](https://github.com/jjsullivan5196/wvkbd)'s architecture;
its code and layouts are its own.

## License

MIT. See [`LICENSE`](LICENSE).
