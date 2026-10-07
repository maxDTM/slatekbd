#!/bin/sh
# Nested Hyprland test harness (no live-session changes).
# Starts a 2nd Hyprland instance (Wayland backend), adds a 2736x1824@2 headless output "SLATE-1",
# removes the visible nested window, starts the focusable "target" client (text-input-v3 + key log),
# then runs $CLIENT (default: build/probe; e.g. CLIENT="../../build/slatekbd") and cycles transforms 1,2,3,0.
# Env: NEST_SECS (default 40), CLIENT, CLICK="x y" (logical coords to vclick), SHOT=1 (grim screenshots per transform)
# Output: build/target.log (keys/commit_strings the app received), build/client.log, build/shot-*.png
D=$(dirname "$(readlink -f "$0")"); B=$D/build; CLIENT=${CLIENT:-$B/probe 18}
before=$(hyprctl instances -j | grep -o '"instance": "[^"]*"' | sort)
env -u HYPRLAND_INSTANCE_SIGNATURE timeout "${NEST_SECS:-40}" Hyprland -c "$D/test.lua" >"$B/nested.log" 2>&1 &
NPID=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
  sleep 0.5
  SIG=$(printf '%s\n%s\n' "$before" "$(hyprctl instances -j | grep -o '"instance": "[^"]*"' | sort)" | sort | uniq -u | head -1 | sed 's/.*: "//;s/"//')
  [ -n "$SIG" ] && break
done
[ -n "$SIG" ] || { echo "nested Hyprland did not start"; exit 1; }
SOCK=$(hyprctl instances -j | tr -d '\n ' | sed "s/.*\"instance\":\"$SIG\",[^}]*\"wl_socket\":\"\([^\"]*\)\".*/\1/")
echo "nested instance $SIG socket $SOCK"
H="hyprctl --instance $SIG"
$H output create headless SLATE-1 >/dev/null
$H eval 'hl.monitor({output="SLATE-1", mode="2736x1824", position="0x0", scale=2})' >/dev/null
$H output remove WAYLAND-1 >/dev/null
sleep 0.5
export WAYLAND_DISPLAY=$SOCK HYPRLAND_INSTANCE_SIGNATURE=$SIG  # hyprctl calls by the client must hit the nested instance
"$B/target" 30 >"$B/target.log" 2>&1 &
sleep 1
$CLIENT >"$B/client.log" 2>&1 &
CPID=$!
sleep 3
[ -n "$CLICK" ] && "$B/vclick" $CLICK 1368 912 && echo "clicked $CLICK"
[ -n "$SHOT" ] && grim "$B/shot-0.png"
for t in 1 2 3 0; do
  $H eval "hl.monitor({output=\"SLATE-1\", transform=$t}); hl.config({input={touchdevice={output=\"SLATE-1\", transform=$t}}})" >/dev/null
  sleep 1.5
  echo "transform $t:"
  $H layers -j | python3 -c 'import json,sys
for mon,v in json.load(sys.stdin).items():
  for lvl,ls in v["levels"].items():
    for l in ls: print("  layer",l["namespace"],"lvl",lvl,"x",l["x"],"y",l["y"],"w",l["w"],"h",l["h"])'
  $H monitors -j | python3 -c 'import json,sys;[print("  reserved",m["reserved"]) for m in json.load(sys.stdin)]'
  [ -n "$SHOT" ] && grim "$B/shot-t$t.png"
done
kill $CPID 2>/dev/null; sleep 0.5; kill $NPID 2>/dev/null; wait 2>/dev/null
echo "--- client.log"; cat "$B/client.log"; echo "--- target.log"; cat "$B/target.log"
