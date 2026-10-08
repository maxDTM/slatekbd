# Changelog

All notable changes to slatekbd. Versions follow `MAJOR.MINOR.PATCH`; the version
lives in `meson.build`.

## [Unreleased]

Documentation only:
- TECHNICAL_DOCUMENTATION.md: repository layout, install prefixes and the PKGBUILD
  path rewrite, reference deployment, release process, status and open work.
- Added this changelog.

## [0.1.0] - 2026-10-07

First public release (GitHub tag `v0.1.0`, AUR `slatekbd 0.1.0-1`).

### Keyboard
- Wayland on-screen keyboard in C11: layer-shell surface, `zwp_virtual_keyboard_v1`
  with a US xkb keymap (real keycodes, so Ctrl/Alt/Super shortcuts work), cairo/pango
  rendering with fractional scaling.
- PC-style US layout on three pages (`abc`, `?123`, `Fn` with F1–F12, navigation block
  and inverted-T arrows), with an always-visible settings (gear) key.
- Split mode with its own grid: 7u left / 8u right halves, straight gap and outer
  edges, 2u outer function keys, 3u space on each side.
- Multi-touch with modifier *toggle* (one-shot / double-tap lock) or *hold* mode, Caps
  Lock by double-tapping Shift, key repeat, key-press popups. Keys are never left stuck.
- Popup mode (reserves an exclusive zone) or overlay mode.
- Auto show/hide through `zwp_input_method_v2`, numeric fields open `?123`.
- Follows all four rotations, with separate landscape/portrait heights; Settings has
  buttons to rotate the display (Hyprland).
- In-surface Settings view; changes apply live and are saved to
  `$XDG_CONFIG_HOME/slatekbd/config`.
- Single instance with `--show/--hide/--toggle/--quit/--status` and signals.

### Lock and login screens
- Lock screen (Hyprland `above_lock` layer rule + noctalia hooks): `--lock/--unlock`;
  an instance started by `--lock` quits on unlock. While locked, key popups, settings,
  quit, rotation and saving are disabled.
- `lock_button` (default on): on the lock/login screen only a round keyboard button is
  shown in the bottom-right corner; tapping it opens the keyboard and the gear key folds
  it back.
- `--greeter` / `--button-offset N` and `slatekbd-greeter-session`: runs noctalia-greeter
  inside a minimal Hyprland so the login screen gets the keyboard.

### Security
- The greeter session starts Hyprland directly (not `start-hyprland`, whose safe-mode
  restart enables default binds before login), falls back to the stock greeter if its
  config is missing or invalid, and uses a `mktemp` runtime directory.
- Keycodes are never logged while locked; the `/tmp` pidfile fallback checks ownership
  and pidfiles are opened with `O_NOFOLLOW`.
