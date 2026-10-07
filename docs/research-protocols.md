# slatekbd - protocol and design research

Date: 2026-10-06. Machine: Surface Pro 6, Hyprland 0.56.2 (Lua config: ~/.config/hypr/hyprland.lua),
noctalia 5.0.1 (native C++ binary, not quickshell), eDP-1 2736x1824 scale 2, transform 0.

## 1. Globals advertised by Hyprland 0.56.2 (dumped with a tiny registry client)

Relevant to us:

| global | ver | use |
|---|---|---|
| wl_compositor | 6 | v6: wl_surface.preferred_buffer_scale / preferred_buffer_transform events (bind >= 6) |
| wl_shm | 2 | buffers |
| wl_seat | 9 | wl_touch (multi-touch ids), wl_pointer fallback |
| wl_output | 4 | 3 outputs (eDP-1 + 2 docked externals); v4 gives name/description, geometry transform, mode, scale |
| zxdg_output_manager_v1 | 3 | logical size (already transform+scale applied) - optional |
| zwlr_layer_shell_v1 | 5 | keyboard surface |
| zwp_virtual_keyboard_manager_v1 | 1 | key output |
| zwp_input_method_manager_v2 | 1 | --auto show/hide |
| wp_fractional_scale_manager_v1 | 1 | fractional scale (preferred_scale /120) |
| wp_viewporter | 1 | needed with fractional scale |
| xdg_wm_base | 7 | only if we use xdg_popup for key preview (we will not; see 6) |
| ext_session_lock_manager_v1 | 1 | used by noctalia / hyprlock (we do NOT bind it) |
| hyprland_lock_notifier_v1 | 1 | lets us know when the session locks/unlocks |
| wp_single_pixel_buffer_manager_v1, wp_cursor_shape_manager_v1 | - | not needed |

Also: zwp_text_input_manager_v3 (apps -> compositor -> our input method), zwlr_virtual_pointer_manager_v1.

## 2. Protocol XMLs

System (/usr/share/wayland-protocols, version 1.49) - use via pkg-config `wayland-protocols` pkgdatadir:
- stable/viewporter/viewporter.xml
- staging/fractional-scale/fractional-scale-v1.xml
- stable/xdg-shell/xdg-shell.xml (only needed because wlr-layer-shell references xdg_popup in get_popup;
  generating layer-shell code needs xdg-shell's interface symbol `xdg_popup_interface`, so generate xdg-shell private code too)
- unstable/xdg-output/xdg-output-unstable-v1.xml (optional)
- staging/ext-session-lock/ext-session-lock-v1.xml (not needed)

NOT in wayland-protocols -> vendored (already downloaded from upstream, validated with wayland-scanner) in
protocols/:
- wlr-layer-shell-unstable-v1.xml (wlr-protocols master, v5)
- virtual-keyboard-unstable-v1.xml (wlroots/protocol)
- input-method-unstable-v2.xml (wlroots/protocol)
- hyprland-lock-notify-v1.xml (hyprwm/hyprland-protocols; events `locked`, `unlocked`; optional feature)

Note: input-method-unstable-v1.xml in /usr/share is the OLD weston protocol - do not use it.

meson: `wayland_scanner = find_program(dependency('wayland-scanner', native:true).get_variable('wayland_scanner'))`,
generate `client-header` + `private-code` for each XML via custom_target.

## 3. How wvkbd works (studied source v0.20 in scratch; not reused)

- Layer shell: one layer surface, default layer OVERLAY, namespace "wvkbd", anchor BOTTOM|LEFT|RIGHT, size (0,h),
  keyboard_interactivity NONE. Popup mode = `set_exclusive_zone(h)`; `--non-exclusive` = no exclusive zone (overlay).
  Exclusive zone is only set at creation; changing mode requires recreating (we can instead call
  set_exclusive_zone(h or 0) + commit at runtime - allowed by protocol, applied on next commit).
- To pick height for landscape/portrait it creates a throwaway full-anchored layer surface and roundtrips to learn the
  available size (hacky, double roundtrip on every show). Better: track wl_output mode + transform + scale (or
  xdg_output logical_size) and use the configure size the compositor sends for our own surface.
- Show/hide: destroys and recreates the whole layer surface + viewport + fractional object each time. Simple and robust
  (hidden = no surface at all, so no exclusive zone left over). We should do the same (destroy on hide), it is the
  only clean way to remove the exclusive zone and stop receiving input.
- Rotation: only handled indirectly: if a configure arrives with w/h different from expected it does hide()+show().
- Virtual keyboard: create_virtual_keyboard(seat); keymap = xkb text written to memfd/shm, `keymap(XKB_V1, fd, size)`
  (size includes trailing NUL). Keys sent as evdev codes (KEY_* from linux/input-event-codes.h, NOT +8).
  Modifiers sent with both fake modifier key presses AND `zwp_virtual_keyboard_v1_modifiers(depressed, latched, locked, group)`.
  Uses a temporary keymap re-upload trick (keycode 127 remapped) to type arbitrary unicode.
- Input method: binds zwp_input_method_manager_v2 only with --auto; get_input_method(seat); activate -> show(),
  deactivate -> hide() executed immediately (protocol says state is double-buffered and applies on `done`; wvkbd
  ignores that, so focus changes that send deactivate+activate in one batch cause a hide/show flicker).
  `unavailable` ignored (if another IM exists - e.g. a running wvkbd --auto - it silently never shows).
- Touch: single-touch only. touch_down unpresses the previous key; touch_up releases "the" current key ignoring id;
  touch_cancel and touch_frame are empty (stuck-key risk on cancel). No per-id tracking => holding Shift with one
  finger while tapping with another cannot work.
- Key preview: an xdg_popup parented to the layer surface via zwlr_layer_surface_v1.get_popup, twice the keyboard height,
  input region empty. Complex; popups also get dismissed by compositors on focus changes.
- HiDPI: wl_surface.preferred_buffer_scale (compositor v6) or wp_fractional_scale + viewport destination; draws with
  cairo into shm buffer at scale; Pango fonts.
- Signals: signalfd for SIGUSR1 hide / SIGUSR2 show / SIGRTMIN toggle, poll() on display fd + signalfd.
- No key repeat at all (grep "repeat" = nothing). No settings UI, no config file (compile-time config.h),
  no single instance check, no pidfile.

Weaknesses to avoid: no multi-touch, no key repeat, IM state not double-buffered, unavailable ignored,
throwaway-surface size probing, compile-time-only config, no stuck-key protection on cancel, no lock awareness.

## 4. Implementation guidance for slatekbd

Event loop: poll() on wl_display fd (prepare_read / read_events / dispatch_pending pattern), signalfd
(SIGUSR1, SIGUSR2, SIGRTMIN, SIGINT, SIGTERM, SIGHUP), and one timerfd for key repeat + long-press + double-tap timing.
Redraw only when dirty, throttled by wl_surface.frame callbacks; double-buffer shm (wl_buffer.release).

Layer surface:
- layer OVERLAY (so it sits above noctalia bar/panels and can be raised above lock, see 7), namespace "slatekbd",
  anchor BOTTOM|LEFT|RIGHT, set_size(0, h_logical), keyboard_interactivity NONE (never steal focus).
- popup mode: set_exclusive_zone(h). overlay mode: set_exclusive_zone(0) (0 = "move away from others' zones but
  reserve nothing"; -1 would ignore bar zones - use 0). Switching at runtime: call set_exclusive_zone + commit; no recreate.
- Output: pass NULL (compositor picks focused output) or by default the output named "eDP-1" if present
  (--output NAME, from wl_output.name v4). Docked there are 3 outputs; prefer the touchscreen output (Hyprland
  input:touchdevice:output = eDP-1 here).
- Always use configure width (and our requested height) to lay out; ack_configure before attaching.
  Hyprland sends a new configure when the output transform changes (logical width changes 1368 <-> 912 at scale 2).
  Orientation = configure width > output logical height? Simpler: track wl_output.mode (physical 2736x1824) + geometry
  transform; portrait if transform is 1/3/5/7. Pick height_landscape vs height_portrait, then set_size(0,h) and
  exclusive zone again + commit when orientation flips.
- Layout coordinates in logical (surface-local) px; render buffer at ceil(w*scale) x ceil(h*scale).
  If wp_fractional_scale available: use preferred_scale/120, wp_viewport_set_destination(w,h), buffer_scale 1.
  Else wl_surface.preferred_buffer_scale (bind wl_compositor v6) -> set_buffer_scale. Surface Pro: scale 2.
  preferred_buffer_transform can be ignored (keep buffer transform normal).

Virtual keyboard:
- One zwp_virtual_keyboard_v1 for the life of the process; upload keymap once at startup from
  xkb_keymap_new_from_names(rules evdev, model pc105, layout "us") -> xkb_keymap_get_as_string -> memfd_create
  (MFD_CLOEXEC|MFD_ALLOW_SEALING), write incl. NUL, keymap(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, strlen+1).
  Keymap MUST be sent before any key event (Hyprland otherwise errors/ignores).
- Send real evdev codes (KEY_A=30 etc.). For modifiers: press/release the real modifier keycode
  (KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFTALT, KEY_LEFTSHIFT, KEY_RIGHTALT...) AND keep an xkb_state in sync
  (xkb_state_update_key) then send `modifiers(depressed, latched, locked, group)` from
  xkb_state_serialize_mods - this keeps Hyprland binds (SUPER+...) and app shortcuts correct.
- Shifted symbols on page 2 ("!" etc.): send Shift press + key + Shift release (unless user shift already active).
- Keep a set of currently-pressed keycodes; on hide, settings open, touch cancel, SIGTERM/SIGINT and exit:
  release every pressed key, clear modifiers (modifiers(0,0,0,0)), then wl_display_flush/roundtrip before exit.
  Hyprland device option `release_pressed_on_close` exists but do not rely on it.
- Key repeat: compositor does not auto-repeat virtual keyboard input for us reliably (clients repeat based on
  the keyboard's repeat_info which Hyprland applies to the seat; virtual keyboard keys held down DO get client-side
  repeat in many toolkits). To be deterministic: do NOT hold the key down; send press+release per repeat tick
  from our timerfd (delay default 400ms, rate 25/s), only for repeatable keys (letters, digits, space, backspace,
  arrows, del, enter?). Make repeat configurable/off.

Multi-touch: table of up to 10 slots keyed by wl_touch id {surface-local x,y, key*, down time}. down -> hit-test, press;
motion -> optional slide-off (cancel press if finger leaves key by > margin, or for letters move highlight);
up -> release that slot's key; cancel -> release all slots without emitting the keys (or emit releases for already
pressed codes). Commit visual state on wl_touch.frame. Hold-mode modifiers = modifier key pressed while its finger slot
is down. Toggle mode: tap = one-shot latch (cleared after next non-mod key), double tap (<350ms) = lock, tap = unlock.
Also handle wl_pointer (mouse/pen-as-pointer) as slot "pointer".

Key preview popup: draw the preview INSIDE our own surface (e.g. enlarged key bubble drawn over the row above), or
reserve extra transparent space above the keyboard with an input region limited to the keys
(wl_surface_set_input_region) - but transparent space would also be counted in exclusive zone unless
exclusive_zone = keyboard height only (allowed: zone can be smaller than surface). Recommended: surface height =
kb_h + preview_h, exclusive zone = kb_h, input region = keyboard rect, preview drawn in the top band. Avoids xdg_popup.
Simplest acceptable alternative: draw bubble clamped inside the keyboard rect.

Input method (auto mode):
- Bind zwp_input_method_manager_v2 v1, get_input_method(seat). Accumulate activate/deactivate in pending state;
  apply on `done` (show if active && auto_enabled, hide otherwise). Debounce hide ~150ms via timerfd to avoid flicker
  when focus moves between fields. Count `done` serials only if we ever commit (we don't need commit_string; we type
  via virtual keyboard). Optional: content_type purpose DIGITS/NUMBER/PHONE/PIN -> open page 2 automatically.
- `unavailable`: another input method owns the seat (e.g. wvkbd --auto, fcitx5). Log a clear warning, destroy the
  object, disable auto mode and show it as "unavailable" in settings. Only one IM per seat in Hyprland.
- Manual show/hide via signal/CLI must override until next IM transition.
- Hyprland sends activate for text-input-v3 clients (GTK4, Qt6 with QT_IM_MODULE=wayland or default, Chromium/Electron
  with --enable-wayland-ime), and noctalia (binary links zwp_text_input_v3). XWayland apps never trigger it.

Signals / single instance: pidfile $XDG_RUNTIME_DIR/slatekbd.pid opened O_CREAT|O_RDWR + flock(LOCK_EX|LOCK_NB);
holding the lock = running instance; `--show/--hide/--toggle/--quit` read pid and kill(SIGUSR2/SIGUSR1/SIGRTMIN/SIGTERM),
exit 1 if no instance. Block signals before creating threads/fds, use signalfd.

## 5. Rotation on this machine (important: Hyprland 0.56 uses a LUA config)

- The user's ~/.config/hypr/hyprland.lua computes eDP-1 transform on every `hyprctl reload` from
  $XDG_RUNTIME_DIR/hypr-orientation ("normal"|"left-up"|"bottom-up"|"right-up" -> 0/1/2/3), but only when undocked and
  in tablet mode; scripts/autorotate.sh (iio-sensor-proxy monitor-sensor) writes that file and runs `hyprctl reload`.
  It also sets input.touchdevice/tablet transform to the same value.
- hyprctl in 0.56 has `eval <lua>` (verified: `hyprctl eval 'return 1'` -> "ok") and still lists `keyword`.
  For the settings rotation buttons the most robust command is a Lua eval, e.g. for transform N:
  `hyprctl eval 'hl.monitor({output="eDP-1",mode="preferred",position="1236x1080",scale=2,transform=N}) hl.config({input={touchdevice={transform=N},tablet={transform=N}}})'`
  but position/scale should be read from `hyprctl monitors -j` rather than hardcoded. Fall back to
  `hyprctl keyword monitor eDP-1,preferred,auto,2,transform,N` + `hyprctl keyword input:touchdevice:transform N`
  if eval fails (non-Lua Hyprland). Both are runtime-only; the next `hyprctl reload` (autorotate) re-applies
  the user's config, which is fine. Make the command template configurable (rotate_cmd= in config, %d = transform)
  so the user can instead do e.g. `echo left-up > $XDG_RUNTIME_DIR/hypr-orientation && hyprctl reload`.
  Run via fork/execvp of "sh -c", non-blocking, reap with SIGCHLD or waitpid(WNOHANG).
- Transform mapping: 0 landscape, 1 portrait (90), 2 landscape flipped, 3 portrait flipped (270).
- Do not edit the user's config.

## 6. Lockscreen

- noctalia 5 locks with ext-session-lock-v1 (binary contains ext_session_lock_* and zwp_text_input_v3);
  the user's lid handler also runs hyprlock (also ext-session-lock).
- While session-locked Hyprland renders only lock surfaces, EXCEPT layer surfaces matched by the layer rule
  `above_lock` (LayerRuleEffect ABOVE_LOCK present in 0.56 headers and in the Lua stub `HL.LayerRuleSpec.above_lock:
  integer|boolean`). Values: 1 = render above lock, 2 = render above lock AND interactable (receives touch).
  Keyboard focus stays on the lock surface, so our virtual-keyboard events reach the password field.
- So: lockscreen support is achievable WITHOUT patching anything, but requires the user to add one rule (we must not
  edit their config; document it). In ~/.config/hypr/hyprland.lua (or a sourced file):
  `hl.layer_rule({ name = "slatekbd-above-lock", match = { namespace = "^slatekbd$" }, above_lock = 2 })`
  (can also be applied live for testing with `hyprctl eval '<same line>'`). Verify exact match key syntax against
  /usr/share/hypr/stubs/hl.meta.lua (field `match: table<string,...>`; namespace matcher name `namespace`).
- The keyboard must be visible while locked: if it's hidden by auto mode it never shows unless the lock's password
  field triggers input-method activate (noctalia uses text-input-v3, so it may work). Robust approach: bind
  hyprland_lock_notifier_v1 (vendored XML), get_lock_notification; on `locked` -> if lockscreen=auto|always, force
  show (ignore auto-hide) and on `unlocked` restore previous visibility. Layer must be OVERLAY.
  Settings must be disabled/limited while locked (no Quit/rotate commands from the lock screen - at least hide "Quit"
  and the rotate/command buttons when locked, as anyone at the device could otherwise kill it; it can't escape the lock
  anyway, since it only injects keys).
- The keyboard process must already be running in the user session (autostart). It cannot start itself from the lock.
- Exclusive zone has no effect on lock surfaces (they're full-screen); the lock UI may be covered at the bottom.
  noctalia's password field is centered, so a ~40% height keyboard is fine.

## 7. Misc notes

- wvkbd is installed (wvkbd 0.20, /usr/bin/wvkbd-mobintl) but was NOT running at research time. If it runs with
  --auto it owns the input method and slatekbd's IM gets `unavailable`; document this.
- Fonts: use Pango with a "Sans" family; font size = key_height * text_scale * 0.4-ish in logical px.
- Exclusive zone note: noctalia bar reserves 45px at bottom? (`hyprctl monitors`: reserved 0 25 0 45) - layer
  surfaces at OVERLAY with exclusive_zone>=0 are placed respecting other exclusive zones, so keyboard sits above
  any bottom bar; that is fine.
