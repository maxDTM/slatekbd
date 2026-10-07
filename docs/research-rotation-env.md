# Research: rotation (req. 5) and test environment

Researched 2026-10-06 on the target machine (Surface Pro 6, Hyprland 0.56.2, commit efb5099).
No live monitor or input settings were changed. Everything marked **verified** was run in a nested
Hyprland instance (see section D).

## A. How rotation works on this machine today

| Item | Value |
|---|---|
| Built-in panel | `eDP-1`, mode 2736x1824@59.96, **scale 2**, transform 0, position 1236x1080, logical 1368x912 |
| External (docked) | `DP-3`, `DP-4`: BenQ 1920x1080, scale 1 |
| Touchscreen | `ipts-045e:001f-touchscreen` (the only device in `touch`); `tablets` is empty (the pen is a tablet tool only while in use) |
| Switches | `Lid Switch`, `Microsoft Surface Type Cover Tablet Mode Switch` |
| Accelerometer | `iio-sensor-proxy` service is running (`/usr/lib/iio-sensor-proxy`) |
| Auto-rotate | **user script** `~/.config/hypr/scripts/autorotate.sh` (started from `hyprland.lua`). It reads `monitor-sensor --accel`, writes `normal\|left-up\|bottom-up\|right-up` to `$XDG_RUNTIME_DIR/hypr-orientation` and runs **`hyprctl reload`**. |
| Who decides the transform | `~/.config/hypr/hyprland.lua` on every config load: map `normal 0, left-up 1, bottom-up 2, right-up 3`; applied only when undocked **and** in tablet mode (cover detached or tablet switch on), otherwise 0. The same value goes to `hl.monitor{output=eDP-1, transform=…}` and `hl.config{input={touchdevice={output=eDP-1, transform=…}, tablet={…}}}`. |
| Current touch options | `input:touchdevice:transform` = 0, `input:touchdevice:output` = `eDP-1` |

Consequences for slatekbd:
- Rotation changes reach slatekbd as normal output changes (sections C/D); nothing special is needed to follow auto-rotate.
- **The user's config is Lua.** `hyprctl keyword …` is rejected: `keyword can't work with non-legacy parsers. Use eval.`
- Any rotation slatekbd applies is temporary: the next `hyprctl reload` from the cover/lid/sensor/hotplug hooks puts back whatever `hyprland.lua` computes. That is acceptable for a convenience button, but it should be documented. The settings page should say that auto-rotate may override it.
- `hyprland.lua` locks with `pidof hyprlock || hyprlock`, but **hyprlock is not installed**. This matters for the lockscreen research, not for rotation.

## B. Commands to rotate the display while keeping touch aligned

Transform values follow `wl_output.transform`: 0 normal, 1 = 90°, 2 = 180°, 3 = 270°. On this device, 1 is portrait
"left-up" and 3 is portrait "right-up", according to the user's own mapping.

### Hyprland with Lua config (this machine), **verified**
```sh
hyprctl eval 'hl.monitor({output="eDP-1", transform=1}); hl.config({input={touchdevice={output="eDP-1", transform=1}, tablet={output="eDP-1", transform=1}}})'
```
- A **partial** `hl.monitor` spec (output + transform only) keeps the mode, scale and position. It was verified on a headless
  output: 2736x1824, scale 2, position 0,0 were all kept and transform went 0→1. It takes effect immediately and needs no reload.
- `hl.config({input={touchdevice={transform=N}}})` updates the option at once. Check it with `hyprctl getoption input:touchdevice:transform`.
- Exit status: `ok` and rc 0 on success. A Lua error prints `error: [string …]` and returns **rc 7**, so check rc.
- `hyprctl reload` reverts both settings (verified).
- Per-device alternative (not verified): `hl.device({name="ipts-045e:001f-touchscreen", transform=N})`.
- Reading state: `hyprctl monitors -j` (fields `name`, `transform`, `scale`, `width`, `height`, `focused`), or
  `hyprctl repl 'local m=hl.get_monitors()[1]; return m.name..":"..m.transform'`.

### Legacy hyprlang config (other users), fallback
```sh
hyprctl --batch "keyword monitor eDP-1,preferred,auto,2,transform,1 ; keyword input:touchdevice:transform 1 ; keyword input:touchdevice:output eDP-1"
```
(In legacy mode `monitor` needs the full rule. Rebuild it from `hyprctl monitors -j`: `WxH@R,XxY,scale,transform,N`.)

### Recommended implementation (`src/rotate.c`)
1. Output name: the name of the output the keyboard surface is on (`wl_output.name`, v4), or else the config key `rotate_output`
   (default `eDP-1`).
2. If config key `rotate_cmd` is set, run it with `%t` → 0..3 and `%o` → the output name. This is the escape hatch for
   users whose own scripts own rotation, for example writing `$XDG_RUNTIME_DIR/hypr-orientation` and reloading.
3. Otherwise run `hyprctl eval '<lua above>'`. If its output contains `non-legacy`/`Unknown`, or rc≠0, run the legacy
   `--batch` form instead. If both fail, show a toast/log. Use `fork`/`execvp` (no shell), argv-quoted, and do not block the event loop
   (handle SIGCHLD with `waitpid(WNOHANG)`).
4. Labels: "Landscape" (0), "Portrait" (1), "Landscape flipped" (2), "Portrait flipped" (3).

## C. How a layer-shell client learns about rotation (Hyprland 0.56), **verified**

Live globals of interest: `zwlr_layer_shell_v1 v5`, `zwp_virtual_keyboard_manager_v1 v1`,
`zwp_input_method_manager_v2 v1` (currently **free**: binding it did not produce `unavailable`; wvkbd is not running right now),
`wp_fractional_scale_manager_v1 v1`, `wp_viewporter v1`, `zxdg_output_manager_v1 v3`, `wl_output v4`, `wl_compositor v6`,
`wl_seat v9`, `ext_session_lock_manager_v1`, `hyprland_lock_notifier_v1`, `zwlr_virtual_pointer_manager_v1 v2`.

Sequence observed after `transform 0→1→2→3→0` with a bottom-anchored (L|R|B) surface, size 0x200 and exclusive zone 200:
```
xdg_output logical_size 912x1368      <- updated on every rotation (portrait = h > w)
layer_surface configure 912x200       <- new width; height stays what we asked for
output done
surface preferred_buffer_transform 1  <- wl_surface v6 event: exact transform (0..3)
```
- **`wl_output.geometry` (with its transform field) and `wl_output.mode` are NOT re-sent on rotation**. Only `wl_output.done`
  arrives. So do **not** rely on `wl_output.geometry.transform`.
- Reliable signals:
  1. `zxdg_output_v1.logical_size` (bind xdg-output v3 for each output) → orientation = `h > w ? portrait : landscape`.
  2. `wl_surface.preferred_buffer_transform` (bind wl_compositor v6) → exact 0..3. Use it to tell "flipped" apart. It also lets
     the client render pre-rotated if desired, but that is not needed: just render upright with buffer_transform 0.
  3. `layer_surface.configure(width, …)` → width to lay out for.
- Re-layout flow: on a logical_size/orientation change → choose `height_landscape` or `height_portrait` →
  `zwlr_layer_surface_v1_set_size(0, h)` + `set_exclusive_zone(popup ? h : 0)` + `wl_surface_commit` → wait for `configure` →
  ack → render. Exclusive zone keeps working after rotation (`reserved [0,0,0,200]` in all 4 transforms) and the surface stays
  bottom-anchored on the rotated output.
- Scale: `wl_output.scale 2`, `wl_surface.preferred_buffer_scale 2`, `wp_fractional_scale_v1.preferred_scale 240` (=2.0).
  Recommended: if fractional-scale + viewporter are present, render a buffer of `ceil(w*s/120)` x `ceil(h*s/120)` and
  `wp_viewport_set_destination(w, h)`. Otherwise use `preferred_buffer_scale` (v6) or the output scale, with `wl_surface_set_buffer_scale`.
  If you do none of this, the result is blurry (seen in the test screenshot).
- Configure width can be 0 before the first configure. Always lay out from the configure values, never from the output mode.
- Touch/pointer coordinates arrive in surface-local **logical** coordinates, already rotated by the compositor. Multiply by the
  scale only for hit-testing in buffer pixels.
- input-method-v2 **verified**: text-input-v3 `enable` in the focused app → `activate`+`done` (show). `disable` → `deactivate`+`done` (hide).
  `commit_string` + `commit(serial)` reached the app. virtual-keyboard keys (with a US xkb keymap uploaded) arrived as `key 35 (h)`.
  Pointer clicks reach the layer surface (`wl_pointer` enter at 684,100 surface-local). **Recommendation: slatekbd should handle
  `wl_pointer` as well as `wl_touch`**. It is useful with the type-cover touchpad and it is the only way to automate presses
  in tests: there is no virtual-touch protocol.

## D. Automated test environment

Installed compositors: **only Hyprland** (no sway/labwc/weston/cage). No wev, wtype, wayland-info or valgrind; `grim` is present.
Hyprland **cannot start purely headless** (without `WAYLAND_DISPLAY` it aborts with `CBackend::create() failed!`). It **can** run
nested on the Wayland backend. Inside it, `hyprctl output create headless` adds a headless monitor, and removing `WAYLAND-1`
closes the visible nested window (it flashes for under a second). That instance supports layer-shell, virtual-keyboard,
input-method-v2, text-input-v3, virtual-pointer, xdg-output, fractional-scale and screencopy (grim works).

Harness (committed in this repo, standalone, not part of meson):
- `tests/nested/build.sh` builds `tests/nested/build/{probe,target,vclick}` from `protocols/` + system wayland-protocols
  (+ a vendored `wlr-virtual-pointer-unstable-v1.xml`).
  - `probe [secs]`: reference layer-shell client that logs every output/scale/transform/IM event. `PROBE_IM_ONLY=1` only checks whether the IM slot is free.
  - `target [secs]`: focused xdg_toplevel that enables text-input-v3 and logs `key N (sym) down/up`, `modifiers dep=…`,
    `text_input commit_string '…'`. `TARGET_TOGGLE_TI=N` disables text-input after N s (tests auto-hide).
  - `vclick X Y W H [hold_ms]`: virtual-pointer absolute move + left click (layout extent W x H; use 1368 912).
- `tests/nested/run_nested.sh` starts nested Hyprland (`tests/nested/test.lua`), output `SLATE-1` 2736x1824@2, starts `target`,
  then `$CLIENT`, optional `CLICK`, cycles transforms 1,2,3,0 via `hyprctl --instance SIG eval`, prints layer geometry/reserved
  per transform, saves `SHOT=1` screenshots, and dumps `build/client.log` + `build/target.log`.

Recipe for slatekbd:
```sh
cd slatekbd && meson setup build && ninja -C build
tests/nested/build.sh
# keyboard visible, tap the key at logical (x,y), screenshots per orientation:
SHOT=1 CLICK="60 800" CLIENT="$PWD/build/slatekbd --config /dev/null" tests/nested/run_nested.sh
#   -> verify tests/nested/build/target.log contains e.g. "key 30 (a) down"/"up"
#   -> view tests/nested/build/shot-0.png, shot-t1.png … (2736x1824 / 1824x2736)
# auto show/hide: TARGET_TOGGLE_TI=8 (target disables text-input at 8 s) and check slatekbd hides
```
- To aim a click, compute the key centre from the layer position printed by the harness (layer y = 912-height in landscape) plus the
  key geometry. Consider a `SLATEKBD_DEBUG_LAYOUT=1` env that prints the key rects to stderr.
- To control a running nested instance by hand: `hyprctl instances` → `hyprctl --instance <sig> …`, `WAYLAND_DISPLAY=wayland-2`.
- Signals/CLI (`--toggle/--show/--hide`) can be tested in the same session. Give the nested run its own `XDG_RUNTIME_DIR`-independent
  pidfile name, or stop any live slatekbd first, so the single-instance check does not hit a live instance.
- Do not run untested builds on the live session's `wayland-1`: an overlay/exclusive-zone surface covers part of the user's screen.
