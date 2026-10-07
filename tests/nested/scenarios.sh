#!/bin/bash
# scenarios.sh - integration tests for slatekbd inside a nested Hyprland (tests/nested/nest.sh).
# Usage: tests/nested/scenarios.sh [KBD_BINARY] [scenario numbers...]
# Nothing touches the live session: every command uses the nested instance signature/socket,
# the keyboard runs as `-i test` with its own config file.
set -u
D=$(dirname "$(readlink -f "$0")"); ROOT=$(readlink -f "$D/../.."); B=$D/build
KBD=${1:-$ROOT/build/slatekbd}; shift || true
ONLY="$*"
OUT=$B/scen; mkdir -p "$OUT"
[ -x "$B/target" ] && [ -x "$B/vclick" ] && [ -x "$B/locker" ] || "$D/build.sh" >/dev/null || exit 1
[ -f "$B/nest.env" ] || "$D/nest.sh" start 900 >/dev/null || exit 1
. "$B/nest.env"
export WAYLAND_DISPLAY=$NSOCK HYPRLAND_INSTANCE_SIGNATURE=$NSIG XDG_CONFIG_HOME=$OUT/xdg
mkdir -p "$XDG_CONFIG_HOME"
H="hyprctl --instance $NSIG"
PASS=0; FAIL=0; FAILED=""
ok() { PASS=$((PASS+1)); echo "  ok   $*"; }
bad() { FAIL=$((FAIL+1)); FAILED="$FAILED|$*"; echo "  FAIL $*"; }
check() { local d=$1; shift; if "$@"; then ok "$d"; else bad "$d"; fi; }
want() { case " $ONLY " in "  ") return 0;; *" $1 "*) return 0;; esac; return 1; }

layer() { # prints "x y w h" of the slatekbd layer (empty if none)
	$H layers -j | python3 -c 'import json,sys
for mon,v in json.load(sys.stdin).items():
  for lvl,ls in v["levels"].items():
    for l in ls:
      if l["namespace"]=="slatekbd": print(l["x"],l["y"],l["w"],l["h"],lvl)'
}
reserved_bottom() { $H monitors -j | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["reserved"][3])'; }
mon_logical() { $H monitors -j | python3 -c 'import json,sys
m=json.load(sys.stdin)[0]; w,h=m["width"]/m["scale"],m["height"]/m["scale"]
if m["transform"]%2: w,h=h,w
print(int(round(w)),int(round(h)),m["transform"])'; }
set_transform() { $H eval "hl.monitor({output=\"SLATE-1\", transform=$1}); hl.config({input={touchdevice={output=\"SLATE-1\", transform=$1}}})" >/dev/null; }

KPID=; TPID=
CONF=$OUT/test.conf
start_kbd() { # args...
	"$KBD" -i test -c "$CONF" -vv --no-lock-rule "$@" >>"$OUT/client.log" 2>&1 &
	KPID=$!
	sleep 1.5
}
stop_kbd() {
	[ -n "$KPID" ] || return 0
	"$KBD" -i test --quit >/dev/null 2>&1
	for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$KPID" 2>/dev/null || break; sleep 0.2; done
	if kill -0 "$KPID" 2>/dev/null; then kill -9 "$KPID"; bad "keyboard did not exit on --quit"; fi
	wait "$KPID" 2>/dev/null; KRC=$?
	KPID=
}
start_target() { # env vars may be passed before
	"$B/target" "${1:-120}" >"$OUT/target.log" 2>&1 &
	TPID=$!
	sleep 1
}
stop_target() { [ -n "$TPID" ] && kill "$TPID" 2>/dev/null; wait "$TPID" 2>/dev/null; TPID=; }

GEO_OPTS=""
# click NAME PAGE [hold_ms] [nth]: tap a key by its dump-geometry name on the running keyboard
click() {
	local name=$1 page=$2 hold=${3:-60} nth=${4:-1}
	read -r lx ly lw lh _ <<<"$(layer)"
	[ -n "$lw" ] || { echo "    (no layer for click $name)"; return 1; }
	local kbh=$((lh - BAND))
	local line
	line=$("$KBD" -c "$CONF" $GEO_OPTS --dump-geometry "$lw" "$kbh" "$page" | awk -v n="$name" '$1=="key" && $3==n' | sed -n "${nth}p")
	[ -n "$line" ] || { echo "    (no key $name on $page)"; return 1; }
	local cx cy
	cx=$(awk '{print $(NF-2)}' <<<"$line"); cy=$(awk '{print $NF}' <<<"$line")
	read -r MW MH _ <<<"$(mon_logical)"
	local X Y
	X=$(python3 -c "print(int($lx+$cx))"); Y=$(python3 -c "print(int($ly+$cy))")
	"$B/vclick" "$X" "$Y" "$MW" "$MH" "$hold" >/dev/null 2>&1
	[ -n "${DOUBLE:-}" ] && "$B/vclick" "$X" "$Y" "$MW" "$MH" 30 >/dev/null 2>&1
	sleep 0.25
}
# tap at surface-local coordinates
click_xy() {
	read -r lx ly lw lh _ <<<"$(layer)"
	read -r MW MH _ <<<"$(mon_logical)"
	"$B/vclick" "$(python3 -c "print(int($lx+$1))")" "$(python3 -c "print(int($ly+$2))")" "$MW" "$MH" "${3:-60}" >/dev/null 2>&1
	sleep 0.3
}
# settings tile centre (landscape defaults: 6 cols x 4 rows, nav row = H/5)
tile_xy() { # tile index -> "x y" for current layer
	read -r lx ly lw lh _ <<<"$(layer)"
	python3 -c "
W=$lw; H=$lh-$BAND; B=$BAND; t=$1
navh=round(H/5); content=H-navh
rows=max(2,min(6,int(content//48))); cols=max(2,min(8,int(W//200)))
per=rows*cols; i=t%per
w=W/cols; h=content/rows
print(int((i%cols+0.5)*w), int(B+(i//cols+0.5)*h))"
}
nav_xy() { # nav name -> x y
	read -r lx ly lw lh _ <<<"$(layer)"
	python3 -c "
W=$lw; H=$lh-$BAND; B=$BAND; u=W/12; navh=round(H/5); y=B+H-navh/2
c={'prev':0.75,'next':4.75,'hide':7.375,'quit':9.125,'close':11}['$1']
print(int(c*u), int(y))"
}
shot() { grim "$OUT/$1.png" 2>/dev/null; }
tlog() { cat "$OUT/target.log"; }

: >"$OUT/client.log"
rm -f "$CONF"
"$KBD" -i test --quit >/dev/null 2>&1
set_transform 0
BAND=56

# ---------------------------------------------------------------- 1
if want 1; then
echo "[1] launch popup / overlay"
start_target 60
start_kbd --no-auto
read -r x y w h lvl <<<"$(layer)"
check "layer mapped on overlay layer (lvl $lvl)" [ "$lvl" = "3" ]
check "size ${w}x${h} == 1368x336 (280 + 56 band)" [ "$w" = 1368 -a "$h" = 336 ]
check "popup reserves 280 (got $(reserved_bottom))" [ "$(reserved_bottom)" = 280 ]
shot shot-main
stop_kbd
check "clean exit code 0 (got $KRC)" [ "$KRC" = 0 ]
check "no layer after quit" [ -z "$(layer)" ]
check "pidfile removed on clean exit" [ ! -e "${XDG_RUNTIME_DIR:-/tmp}/slatekbd-test.pid" ]
check "reserved back to 0" [ "$(reserved_bottom)" = 0 ]
start_kbd --no-auto --mode overlay
check "overlay reserves 0 (got $(reserved_bottom))" [ "$(reserved_bottom)" = 0 ]
check "overlay layer present" [ -n "$(layer)" ]
stop_kbd
stop_target
fi

# ---------------------------------------------------------------- 2-4 typing
if want 2 || want 3 || want 4; then
echo "[2-4] typing, modifiers, pages"
start_target 120
start_kbd --no-auto
: >"$OUT/target.log.mark"
click h main
check "h typed (key 35 (h) down/up)" grep -q "key 35 (h) down" "$OUT/target.log"
check "h released" grep -q "key 35 (h) up" "$OUT/target.log"
click 1 main
check "number row on abc: 1 (key 2)" grep -q "key 2 (1) down" "$OUT/target.log"
click Shift main
click h main
check "Shift+h gives H" grep -q "key 35 (H) down" "$OUT/target.log"
check "one-shot Shift released after the key" bash -c "grep -E 'key 42 .* up' '$OUT/target.log' >/dev/null"
click Ctrl main
click a main
check "Ctrl modifier sent (dep=4)" grep -q "modifiers dep=4 " "$OUT/target.log"
check "Ctrl+a key 30" bash -c "grep -A3 'modifiers dep=4 ' '$OUT/target.log' | grep -q 'key 30'"
check "Ctrl released after one-shot" grep -q 'key 29 (Control_L) up' "$OUT/target.log"
click '?123' main
shot shot-sym
click '!' sym
check "! via implied Shift (exclam)" grep -q "key 2 (exclam) down" "$OUT/target.log"
click 5 sym
check "5 on number row" grep -q "key 6 (5) down" "$OUT/target.log"
click Fn sym
shot shot-fn
click up fn
check "Fn page Up arrow (key 103)" grep -q "key 103 (Up) down" "$OUT/target.log"
click F5 fn
check "F5 (key 63)" grep -q "key 63 (F5) down" "$OUT/target.log"
click PgDn fn
check "PgDn (key 109)" grep -q "key 109 (Next) down" "$OUT/target.log"
click abc fn
click q main
check "back on main: q" grep -q "key 16 (q) down" "$OUT/target.log"
# double-tap Ctrl locks, two keys keep it, tap unlocks
DOUBLE=1 click Ctrl main 30
click x main; click c main
check "locked Ctrl spans two keys" bash -c "grep -E 'key (45|46)' '$OUT/target.log' | wc -l | grep -q 4"
click Ctrl main
sleep 0.2
check "modifiers 0 at end" bash -c "grep 'modifiers' '$OUT/target.log' | tail -1 | grep -q 'dep=0 lat=0'"
# key repeat: hold Bksp ~1s -> several presses
n0=$(grep -c "key 14 (BackSpace) down" "$OUT/target.log")
click Bksp main 1100
n1=$(grep -c "key 14 (BackSpace) down" "$OUT/target.log")
check "key repeat on Bksp hold ($((n1-n0)) presses)" [ $((n1-n0)) -ge 10 ]
check "every press released" bash -c "[ \$(grep -c ' down$' '$OUT/target.log') = \$(grep -c ' up$' '$OUT/target.log') ]"
stop_kbd
stop_target
fi

# ---------------------------------------------------------------- 5 settings
if want 5; then
echo "[5] settings view"
start_target 120
start_kbd --no-auto --shown
click gear main
shot shot-settings
check "settings open logged" grep -q "settings opened" "$OUT/client.log"
read -r tx ty <<<"$(tile_xy 7)"   # Split
click_xy "$tx" "$ty"
read -r cx cy <<<"$(nav_xy close)"
click_xy "$cx" "$cy"
check "settings closed" grep -q "settings closed" "$OUT/client.log"
shot shot-split
sleep 1.3
check "config saved with split=1" grep -qE "^split +=? *1" "$CONF"
check "--shown is not saved as start=shown" bash -c "! grep -qE '^start +=? *shown' '$CONF'"
GEO_OPTS="--split"
click h main
check "split: h typed" bash -c "grep -c 'key 35 (h) down' '$OUT/target.log' | grep -q 1"
click space main 60 2
check "split: right space half types space" grep -q "key 57 (space) down" "$OUT/target.log"
GEO_OPTS=""
click gear main
read -r tx ty <<<"$(tile_xy 11)"  # Theme
click_xy "$tx" "$ty"
read -r tx ty <<<"$(tile_xy 7)"   # Split off again
click_xy "$tx" "$ty"
shot shot-settings-light
read -r tx ty <<<"$(tile_xy 9)"   # Height landscape stepper: tap '+' zone
read -r lx ly lw lh _ <<<"$(layer)"
click_xy "$(python3 -c "print(int($tx + $lw/6*0.35))")" "$ty"
sleep 0.3
read -r x y w h _ <<<"$(layer)"
check "height +10 applied live (h=$h, want 348)" [ "$h" -ge 347 -a "$h" -le 349 ]
check "popup zone follows height ($(reserved_bottom))" [ "$(reserved_bottom)" = 290 ]
read -r tx ty <<<"$(tile_xy 0)"   # Mode -> overlay
click_xy "$tx" "$ty"
sleep 0.3
check "mode overlay live: reserved 0" [ "$(reserved_bottom)" = 0 ]
click_xy "$tx" "$ty"
sleep 0.3
check "mode popup again: reserved 290" [ "$(reserved_bottom)" = 290 ]
read -r tx ty <<<"$(tile_xy 9)"
click_xy "$(python3 -c "print(int($tx - $lw/6*0.35))")" "$ty"
read -r cx cy <<<"$(nav_xy close)"
click_xy "$cx" "$cy"
shot shot-light
sleep 1.3
check "theme=light saved" grep -qE "^theme +=? *light" "$CONF"
check "height back to 280 saved" grep -qE "^height_landscape += 280" "$CONF"
# Hide button
click gear main
read -r cx cy <<<"$(nav_xy hide)"
click_xy "$cx" "$cy"
sleep 0.3
check "Hide button hides" [ -z "$(layer)" ]
"$KBD" -i test --show
sleep 0.5
# Quit needs two taps
click gear main
read -r cx cy <<<"$(nav_xy quit)"
click_xy "$cx" "$cy"
sleep 0.3
check "first Quit tap does not quit" kill -0 "$KPID"
click_xy "$cx" "$cy"
sleep 0.8
check "second Quit tap quits" bash -c "! kill -0 $KPID 2>/dev/null"
wait "$KPID" 2>/dev/null; KPID=
sed -i 's/^theme .*/theme = dark/' "$CONF"
stop_target
fi

# ---------------------------------------------------------------- 6 rotation
if want 6; then
echo "[6] rotation"
start_target 120
start_kbd --no-auto
for t in 1 2 3 0; do
	set_transform $t
	sleep 1.2
	read -r MW MH TR <<<"$(mon_logical)"
	read -r x y w h _ <<<"$(layer)"
	if [ $((t % 2)) = 1 ]; then eh=$((340 + 68)); ez=340; else eh=336; ez=280; fi
	check "transform $t: layer ${w}x${h} (want ${MW}x${eh}), zone $(reserved_bottom)=$ez" [ "$w" = "$MW" -a "$h" = "$eh" -a "$(reserved_bottom)" = "$ez" ]
	shot "shot-t$t"
	if [ "$t" = 1 ]; then
		BAND=68
		click j main
		check "portrait: j typed" grep -q "key 36 (j) down" "$OUT/target.log"
		BAND=56
	fi
done
# rotate buttons in settings drive hyprctl on the nested instance
click gear main
read -r tx ty <<<"$(tile_xy 20)"   # Rotate -> Portrait (transform 1); tile 20 is on page 1 in landscape
click_xy "$tx" "$ty"
sleep 1.5
read -r MW MH TR <<<"$(mon_logical)"
check "Rotate Portrait button -> transform 1 (got $TR)" [ "$TR" = 1 ]
check "touchdevice transform follows" bash -c "$H getoption input:touchdevice:transform | grep -q 'int: 1'"
BAND=68
read -r x y w h _ <<<"$(layer)"
shot shot-settings-portrait
# portrait: 4 cols x 5 rows = 20 per page -> tile 19 (Rotate Landscape) is on page 1
read -r tx ty <<<"$(tile_xy 19)"
click_xy "$tx" "$ty"
sleep 1.5
read -r MW MH TR <<<"$(mon_logical)"
check "Rotate Landscape button -> transform 0 (got $TR)" [ "$TR" = 0 ]
BAND=56
stop_kbd
set_transform 0
stop_target
fi

# ---------------------------------------------------------------- 7 auto
if want 7; then
echo "[7] auto show/hide via input-method-v2"
start_kbd --auto
check "auto: hidden at start (no focused text field yet)" [ -z "$(layer)" ]
TARGET_TOGGLE_TI=5 start_target 30
t0=$(date +%s.%N)
for i in $(seq 1 30); do [ -n "$(layer)" ] && break; sleep 0.05; done
check "auto: shown on text-input activate" [ -n "$(layer)" ]
check "auto: logged activation" grep -q "input method activated" "$OUT/client.log"
for i in $(seq 1 120); do [ -z "$(layer)" ] && break; sleep 0.05; done
check "auto: hidden after text-input disable" [ -z "$(layer)" ]
"$KBD" -i test --show; sleep 0.4
check "manual --show overrides auto" [ -n "$(layer)" ]
"$KBD" -i test --hide; sleep 0.4
check "manual --hide" [ -z "$(layer)" ]
stop_kbd
stop_target
echo "[7b] hidden while focused, then the field asks again (re-enable -> activate)"
start_kbd --auto
TARGET_REENABLE_TI=4 start_target 30
for i in $(seq 1 30); do [ -n "$(layer)" ] && break; sleep 0.05; done
check "auto: shown for the focused field" [ -n "$(layer)" ]
"$KBD" -i test --hide; sleep 0.4
check "manual --hide while focused" [ -z "$(layer)" ]
for i in $(seq 1 80); do [ -n "$(layer)" ] && break; sleep 0.05; done
# Informational only: Hyprland 0.56 answers a re-sent enable with a bare `done` (no
# `activate`), so there is nothing to tell it apart from an ordinary text-input commit.
# slatekbd clears a manual hide whenever an `activate` arrives (unit-tested in test_visibility).
if [ -n "$(layer)" ]; then echo "  info re-sent text_input.enable shows it again"
else echo "  info re-sent text_input.enable: compositor sent no activate, stays hidden (expected on Hyprland)"; fi
stop_kbd
stop_target
fi

# ---------------------------------------------------------------- 8 signals / single instance
if want 8; then
echo "[8] signals, CLI, single instance"
start_target 60
start_kbd --no-auto
"$KBD" -i test --status >/dev/null; check "--status exit 0" [ $? = 0 ]
"$KBD" -i test -c "$CONF" --no-auto >/dev/null 2>&1; check "second instance exits 1" [ $? = 1 ]
"$KBD" -i test --toggle; sleep 0.4; check "--toggle hides" [ -z "$(layer)" ]
"$KBD" -i test --toggle; sleep 0.4; check "--toggle shows" [ -n "$(layer)" ]
"$KBD" -i test --hide; sleep 0.4; check "--hide" [ -z "$(layer)" ]
"$KBD" -i test --show; sleep 0.4; check "--show" [ -n "$(layer)" ]
kill -USR1 "$KPID"; sleep 0.4; check "SIGUSR1 hides" [ -z "$(layer)" ]
kill -USR2 "$KPID"; sleep 0.4; check "SIGUSR2 shows" [ -n "$(layer)" ]
stop_kbd
"$KBD" -i test --status >/dev/null; check "--status exit 1 after quit" [ $? = 1 ]
"$KBD" -i test --hide 2>/dev/null; check "--hide with no instance exits 1" [ $? = 1 ]
start_kbd --no-auto
kill -TERM "$KPID"; sleep 0.5
check "SIGTERM exits cleanly" bash -c "! kill -0 $KPID 2>/dev/null"
wait "$KPID"; rc=$?; check "exit code 0 after SIGTERM ($rc)" [ "$rc" = 0 ]
KPID=
stop_target
fi

# ---------------------------------------------------------------- 9 stuck keys
if want 9; then
echo "[9] no stuck keys on hide while held"
start_target 60
start_kbd --no-auto --modifiers hold
read -r lx ly lw lh _ <<<"$(layer)"
line=$("$KBD" -c "$CONF" --dump-geometry "$lw" 280 main | awk '$1=="key" && $3=="Shift"' | head -1)
cx=$(awk '{print $(NF-2)}' <<<"$line"); cy=$(awk '{print $NF}' <<<"$line")
read -r MW MH _ <<<"$(mon_logical)"
"$B/vclick" "$(python3 -c "print(int($lx+$cx))")" "$(python3 -c "print(int($ly+$cy))")" "$MW" "$MH" 2000 >/dev/null 2>&1 &
VP=$!
sleep 0.8
check "Shift held (dep=1)" bash -c "grep 'modifiers' '$OUT/target.log' | tail -1 | grep -q 'dep=1'"
"$KBD" -i test --hide
sleep 0.4
check "Shift released on hide" bash -c "grep -E 'key 42 .* up' '$OUT/target.log' >/dev/null"
check "modifiers 0 after hide" bash -c "grep 'modifiers' '$OUT/target.log' | tail -1 | grep -q 'dep=0'"
wait $VP
check "presses == releases" bash -c "[ \$(grep -c ' down$' '$OUT/target.log') = \$(grep -c ' up$' '$OUT/target.log') ]"
stop_kbd
stop_target
fi

# ---------------------------------------------------------------- 10 lock
if want 10; then
echo "[10] lock screen (nested ext-session-lock)"
start_target 90
"$KBD" -i test -c "$CONF" -vv --no-auto >>"$OUT/client.log" 2>&1 &   # lock_rule on: adds the rule to the NESTED instance
KPID=$!
sleep 1.5
"$B/locker" 12 >"$OUT/locker.log" 2>&1 &
LP=$!
sleep 1
"$KBD" -i test --lock
sleep 1.2
read -r x y w h lvl <<<"$(layer)"
read -r MW MH _ <<<"$(mon_logical)"
check "locked: only the round button (${w}x${h} at $x,$y)" [ "$w" = 36 -a "$h" = 36 -a "$((x + w))" = "$((MW - 16))" -a "$((y + h))" = "$((MH - 16))" ]
shot shot-lock-button
click_xy 18 18
sleep 0.8
read -r x y w h lvl <<<"$(layer)"
check "tap on the button opens the keyboard" [ -n "$w" ]
check "lock mode: no band, height 280" [ "$h" = 280 ]
check "lock mode: no exclusive zone" [ "$(reserved_bottom)" = 0 ]
BAND=0; GEO_OPTS="--no-preview"
click p main
sleep 0.3
check "key reaches the lock surface (lock key 25)" grep -q "lock key 25 down" "$OUT/locker.log"
check "target app did not get it" bash -c "! grep -q 'key 25 (p)' '$OUT/target.log'"
shot shot-locked
click gear main
sleep 0.6
check "gear does not open settings while locked" bash -c "! grep -q 'settings opened' <(sed -n '/signal: lock/,\$p' '$OUT/client.log')"
read -r x y w h lvl <<<"$(layer)"
check "gear folds the keyboard back into the button (${w}x${h})" [ "$w" = 36 -a "$h" = 36 ]
"$KBD" -i test --unlock
wait $LP
sleep 0.5
BAND=56; GEO_OPTS=""
read -r x y w h lvl <<<"$(layer)"
check "after unlock: back to popup with band (h=$h)" [ "$h" = 336 -a "$(reserved_bottom)" = 280 ]
n=$(awk '/session locked|signal: lock/{on=1} /signal: unlock|session unlocked/{on=0} on && /surface created/' "$OUT/client.log" | wc -l)
check "lock rule: surface recreated exactly once while locked ($n)" [ "$n" = 1 ]
stop_kbd
stop_target
fi

# ---------------------------------------------------------------- 11 --lock auto-spawn
if want 11; then
echo "[11] slatekbd --lock with no instance, then --unlock"
CONF2=$OUT/test-lockspawn.conf
printf 'auto = 0\n' >"$CONF2"
"$KBD" -i test -c "$CONF2" --lock
check "--lock spawns an instance" bash -c "for i in \$(seq 1 20); do '$KBD' -i test --status >/dev/null && exit 0; sleep 0.2; done; exit 1"
sleep 1.2
check "spawned instance shows in lock mode" [ -n "$(layer)" ]
"$KBD" -i test --unlock
sleep 0.6
check "after unlock it does not stay on the desktop" [ -z "$(layer)" ]
for i in $(seq 1 20); do "$KBD" -i test --status >/dev/null || break; sleep 0.2; done
check "lock-started instance exits on unlock" bash -c "! '$KBD' -i test --status >/dev/null"
check "config untouched by the run (no start=shown)" bash -c "! grep -q shown '$CONF2'"
rm -f "$CONF2"
fi

# ---------------------------------------------------------------- 12 greeter mode
if want 12; then
echo "[12] --greeter --button-offset 3 (login screen)"
start_target 60
"$KBD" -i test -c "$CONF" -vv --greeter --button-offset 3 >>"$OUT/client.log" 2>&1 &
KPID=$!
sleep 1.5
read -r x y w h lvl <<<"$(layer)"
read -r MW MH _ <<<"$(mon_logical)"
check "greeter: button left of three 44px slots (${w}x${h} at $x,$y)" [ "$w" = 36 -a "$((x + w))" = "$((MW - 16 - 3 * 44))" -a "$((y + h))" = "$((MH - 16))" ]
click_xy 18 18
sleep 0.8
read -r x y w h lvl <<<"$(layer)"
check "greeter: tap opens the keyboard (h=$h)" [ "$h" = 280 ]
"$KBD" -i test --unlock
sleep 0.5
check "greeter: --unlock does not end greeter mode" "$KBD" -i test --status
read -r x y w h lvl <<<"$(layer)"
check "greeter: still in lock layout (h=$h)" [ "$h" = 280 ]
stop_kbd
stop_target
fi

echo
echo "PASS $PASS  FAIL $FAIL"
[ -n "$FAILED" ] && echo "$FAILED" | tr '|' '\n'
[ "$FAIL" = 0 ]
