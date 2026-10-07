#!/bin/sh
# nest.sh - manage a nested, headless-output Hyprland for slatekbd tests.
#   nest.sh start [SECS]   start (auto-exits after SECS, default 300); writes build/nest.env
#   nest.sh stop           stop it
#   nest.sh env            print the env file (WAYLAND_DISPLAY / HYPRLAND_INSTANCE_SIGNATURE of the NESTED instance)
# The live session is never modified: the nested compositor gets its own instance signature and socket,
# and its visible window (output WAYLAND-1) is removed right after a headless output SLATE-1 is created.
D=$(dirname "$(readlink -f "$0")"); B=$D/build; mkdir -p "$B"
ENVF=$B/nest.env
case "$1" in
start)
	[ -f "$ENVF" ] && "$0" stop >/dev/null 2>&1
	before=$(hyprctl instances -j | grep -o '"instance": "[^"]*"' | sort)
	env -u HYPRLAND_INSTANCE_SIGNATURE timeout "${2:-300}" Hyprland -c "$D/test.lua" >"$B/nested.log" 2>&1 &
	NPID=$!
	SIG=
	for i in $(seq 1 20); do
		sleep 0.5
		SIG=$(printf '%s\n%s\n' "$before" "$(hyprctl instances -j | grep -o '"instance": "[^"]*"' | sort)" | sort | uniq -u | head -1 | sed 's/.*: "//;s/"//')
		[ -n "$SIG" ] && break
	done
	[ -n "$SIG" ] || { echo "nested Hyprland did not start"; kill $NPID 2>/dev/null; exit 1; }
	SOCK=$(hyprctl instances -j | tr -d '\n ' | sed "s/.*\"instance\":\"$SIG\",[^}]*\"wl_socket\":\"\([^\"]*\)\".*/\1/")
	H="hyprctl --instance $SIG"
	$H output create headless SLATE-1 >/dev/null
	$H eval 'hl.monitor({output="SLATE-1", mode="2736x1824", position="0x0", scale=2})' >/dev/null
	$H output remove WAYLAND-1 >/dev/null
	sleep 0.5
	printf 'NPID=%s\nNSIG=%s\nNSOCK=%s\n' "$NPID" "$SIG" "$SOCK" >"$ENVF"
	echo "nested instance $SIG socket $SOCK pid $NPID"
	;;
stop)
	[ -f "$ENVF" ] || exit 0
	. "$ENVF"
	kill "$NPID" 2>/dev/null
	pkill -f "Hyprland -c $D/test.lua" 2>/dev/null
	sleep 0.5
	rm -f "$ENVF"
	;;
env)
	cat "$ENVF"
	;;
*)
	echo "usage: $0 start [secs] | stop | env"; exit 2;;
esac
