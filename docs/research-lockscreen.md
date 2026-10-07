# Lockscreen research (requirement 7)

Status: **feasible without patching Hyprland or noctalia.** The user has to add one
Hyprland layer rule, plus two noctalia hooks if they want the keyboard to show up
on its own. The session was never locked during this research. Everything below
comes from reading the installed binaries, the user's configs (read-only) and the
Hyprland v0.56.2 source.

## 1. How noctalia locks

- Package: `noctalia 5.0.1-1.1`, a single native C++ binary `/usr/bin/noctalia`
  (cairo/pango/EGL, libpam). It is not Quickshell/QML. Hyprland's autostart runs it
  (`~/.config/hypr/config/autostart.lua` -> `hl.exec_cmd("noctalia")`).
- The lock screen is built in (`LockScreen` class) and uses **ext-session-lock-v1**.
  The binary's strings include `ext_session_lock_manager_v1`,
  `ext_session_lock_surface_v1` and `LockScreen::handleLocked(... ext_session_lock_v1 ...)`.
  PAM handles authentication, with an optional fprintd fingerprint path.
- **noctalia has no on-screen keyboard.** It binds `zwp_virtual_keyboard_manager_v1`
  only for clipboard auto-paste ("clipboard auto-paste failed: Wayland virtual keyboard
  unavailable"). No lockscreen setting mentions a keyboard. The lockscreen settings are:
  enabled, allow-empty-password, blur, blurred-desktop, fingerprint,
  lock-before-suspend, monitors, tint, wallpaper, widgets.
- noctalia is a **text-input-v3 client** (`zwp_text_input_manager_v3`,
  `TextInputClient`, "failed to create text-input-v3 object"). Its text fields probably
  enable text-input, which would also send IME activate events to slatekbd. Whether the
  *lock password box* does this is **unverified**, because it can only be tested by
  locking.
- noctalia has **hooks**: `session_locked` ("when the compositor confirms the session
  lock") and `session_unlocked`. Each hook is a shell command string or an array of
  strings. Hooks can go in **any `*.toml` file under `~/.config/noctalia/`**, so the user
  does not need to touch `config.toml`. (Source: docs.noctalia.dev/noctalia/automation/hooks/
  and the strings `session_locked` / `session_unlocked` in the binary.)
- noctalia does **not** set logind `LockedHint` (`LockedHint=no`, and there is no
  `SetLockedHint` string). Hyprland 0.56.2 sends no socket2 or Lua event for lock.
  Neither is a usable lock signal. **The noctalia hook is the only reliable lock signal.**
- Side note: the lid-switch bind in `~/.config/hypr/hyprland.lua` runs
  `pidof hyprlock || hyprlock`, but **hyprlock is not installed**. Closing the lid while
  undocked therefore does not lock. `noctalia msg ...` (session lock) would be the right
  command. This is outside slatekbd's scope, but the user should be told.

## 2. Hyprland 0.56.2 behaviour while session-locked (source-verified)

Files read: `src/render/Renderer.cpp`, `src/managers/input/InputManager.cpp`,
`src/managers/input/Touch.cpp`, `src/desktop/state/ViewHitTester.cpp`,
`src/desktop/rule/layerRule/*`, `src/config/lua/bindings/LuaBindingsConfigRules.cpp`.

- **Normal layer-shell surfaces (overlay layer included) are NOT shown above the lock.**
  `renderLockscreen()` draws the lock surface first. After that it draws only layers
  whose rule `above_lock` is set (`renderLayer(..., lockscreen=true)` skips every other
  layer). `misc:session_lock_xray` only keeps *workspaces* drawn beneath a translucent
  lock surface. It does not help here (current value: false).
- **Layer rule `above_lock`** (int 0..2; Lua `CLuaConfigInt(0,0,2)`):
  - `1`: draw the layer above the lock surface, but it receives no input.
  - `2`: draw it above the lock surface **and make it interactive**.
    `CViewHitTester::layerSurfaceAt(..., aboveLockscreen=true)` only accepts layers with
    `aboveLock()==2`. `InputManager::mouseMoveUnified` searches those layers for
    pointer/touch focus while locked.
- **Touch**: `onTouchDown` calls `refocus()`. With `above_lock = 2`, a touch on the
  keyboard goes to the keyboard surface. Any other touch goes to the lock surface
  (Touch.cpp: "could have abovelock surface, thus only use lock if no ls found").
  Multi-touch IDs work the same way as when unlocked.
- **Keyboard focus is forced to the lock surface** whenever the session is locked
  (`Desktop::focusState()->rawSurfaceFocus(foundLockSurface)`). A layer surface cannot
  take keyboard focus away from it. This is the behaviour we want.
- **Virtual-keyboard input reaches the lock surface.** `onKeyboardKey` handles
  virtual-keyboard events like hardware keys. They are dropped only when the virtual
  keyboard belongs to the client that holds the *input-method keyboard grab*
  (`shouldIgnoreVirtualKeyboard`). slatekbd must therefore **never call
  `zwp_input_method_v2.grab_keyboard`**. wvkbd does not call it either. Keys then go to
  the focused surface, which is the noctalia password field. Hyprland also switches the
  seat keymap to the virtual keyboard's keymap, as usual.
- Lua syntax (the user's config is Lua: `~/.config/hypr/hyprland.lua` + `config/*.lua`):
  `hl.layer_rule({ name = "slatekbd", match = { namespace = "^slatekbd$" }, above_lock = 2 })`.
  The match key `namespace` exists (`RULE_PROP_NAMESPACE`). A **named** rule is replaced
  in place when it is registered again, so registering it more than once is harmless.
- `hyprctl eval '<lua>'` exists and works (a harmless `hyprctl eval 'local x = 1'`
  returned `ok`). Rules added this way are lost on `hyprctl reload`. This user's config
  reloads often (lid switch, dock/undock), so an eval-only rule is not reliable on its own.
- If the lock client dies, Hyprland shows the "lockdead" screen. Recovery is from a TTY:
  `hyprctl --instance 0 'keyword misc:allow_session_lock_restore 1'`, then restart
  noctalia. This is a general recovery note, not something slatekbd causes.

## 3. Recommendation: what slatekbd implements

1. **Fixed layer namespace `slatekbd`** for the keyboard layer surface. Settings-page
   overlays and the key popup preview must be drawn inside the same surface. Any extra
   layer surface would need its own rule.
   - Layer: `ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY`. `keyboard_interactivity = NONE`
     (never take keyboard focus).
   - Never grab the IME keyboard (see section 2).
2. **Lock-mode CLI and signals** (reuses the existing pidfile mechanism):
   - `slatekbd --lock`: enter lock mode and show the keyboard. The instance remembers
     whether it was visible before. If no instance is running, start one detached
     (double-fork/`setsid`, stdio to /dev/null) that begins in lock mode. **It must
     return immediately**, because noctalia runs hook commands in order through a shell.
   - `slatekbd --unlock`: leave lock mode and restore the previous visibility/auto state.
     Does nothing if no instance is running.
   - Delivery: `SIGRTMIN+1` = lock, `SIGRTMIN+2` = unlock (existing: SIGUSR1 hide,
     SIGUSR2 show, SIGRTMIN toggle).
   - Optional (`lock_apply_rule=true`, default true): on `--lock`, run
     `hyprctl eval 'hl.layer_rule({name="slatekbd",match={namespace="^slatekbd$"},above_lock=2})'`
     with fork/exec, without waiting on the main loop, and ignore failures. **Then destroy
     and recreate** the layer surface so the rule applies to the new surface. This
     protects against a reload that dropped the rule. It does not edit any config file.
     The permanent rule from section 4 is still recommended.
3. **Behaviour in lock mode**:
   - Force **overlay** mode (no exclusive zone) and show the keyboard even if auto-hide is on.
     The lock surface covers the whole output and does not resize, so an exclusive zone
     does nothing. In lock mode, IME deactivate does not hide the keyboard.
   - **Turn off the key-press popup preview** so the password cannot be read over the
     user's shoulder. Also never draw typed text.
   - Keep the gear and the settings page, but **hide or disable**: Quit, display rotation
     buttons, and config writes. This avoids surprise state changes and leaving the user
     at the lock screen with no keyboard. Keep Hide (the next `--lock`, or the hook
     after unlock, brings it back).
   - Release all held keys and modifiers when entering and leaving lock mode. Otherwise a
     latched Ctrl could carry into the password field or into the unlocked desktop.
   - Output: config key `output=` (e.g. `eDP-1`; default: the internal panel if it is
     found by name prefix `eDP`/`LVDS`/`DSI`, otherwise compositor choice). On the
     Surface's 3-monitor dock setup, noctalia locks every output. The keyboard should stay
     on the touch panel (touch is mapped to eDP-1).
4. **Auto mode still helps when unlocked.** If noctalia's password field enables
   text-input-v3, `--auto` will also show the keyboard on the lock screen without any
   hook. This is untested, so the hook is the documented, supported path.
5. Document all of this in `--help` and in the README section "Using slatekbd on the lock screen".

## 4. Config the user needs to add (slatekbd must not do this itself)

Hyprland (Lua config). Put it e.g. in `~/.config/hypr/config/windowrules.lua`:

```lua
-- slatekbd: draw the on-screen keyboard above the session lock and make it touchable
hl.layer_rule({ name = "slatekbd", match = { namespace = "^slatekbd$" }, above_lock = 2 })
```

(Legacy hyprlang syntax, for reference only: `layerrule = above_lock 2, match:namespace ^slatekbd$`.)

noctalia: create a new file `~/.config/noctalia/slatekbd.toml` (any `*.toml` there is merged):

```toml
[hooks]
session_locked   = "slatekbd --lock"
session_unlocked = "slatekbd --unlock"
```

(Use the full path, e.g. `./build/slatekbd`, until it is installed.)

Without the Hyprland rule, the keyboard runs but is invisible under the lock.
Without the hooks, it shows on the lock screen only if noctalia's password field
triggers text-input (auto mode) or if it was already visible when the screen locked.

## 5. Manual test plan (for the user; the workflow must not lock)

1. Add the rule and the hooks, then run `hyprctl reload`. Start `slatekbd`.
2. Keep the Type Cover attached as a fallback. Lock from the noctalia session menu.
3. Expected: the keyboard appears on eDP-1 above the lock. Tapping keys types into the
   password box. Enter unlocks. After unlocking, the keyboard goes back to its earlier state.
4. Check: `hyprctl layers` shows namespace `slatekbd` while locked. Also look in
   `hyprctl rollinglog` for rule errors.
5. If the keyboard is not visible, run `hyprctl eval` with the rule from section 4, then
   `slatekbd --lock` again.

## 6. What would need upstream changes (not needed now)

- An OSK that works with **no** user config would need either noctalia to start an OSK
  itself (it has no such option), or Hyprland to show `above_lock` surfaces by default
  (it does not). Neither is required, because the rule and the hooks cover it.
