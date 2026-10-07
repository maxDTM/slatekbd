#!/bin/sh
# Builds the nested-test helper clients (probe, target, vclick) into tests/nested/build/.
set -e
D=$(dirname "$(readlink -f "$0")"); P=$D/../../protocols; W=$(pkg-config --variable=pkgdatadir wayland-protocols)
B=$D/build; mkdir -p "$B"
for x in "$P/wlr-layer-shell-unstable-v1.xml" "$P/virtual-keyboard-unstable-v1.xml" "$P/input-method-unstable-v2.xml" \
         "$D/wlr-virtual-pointer-unstable-v1.xml" "$W/stable/xdg-shell/xdg-shell.xml" "$W/stable/viewporter/viewporter.xml" \
         "$W/unstable/text-input/text-input-unstable-v3.xml" "$W/staging/fractional-scale/fractional-scale-v1.xml" \
         "$W/unstable/xdg-output/xdg-output-unstable-v1.xml" "$W/staging/ext-session-lock/ext-session-lock-v1.xml"; do
  b=$(basename "$x" .xml); wayland-scanner client-header "$x" "$B/$b-client.h"; wayland-scanner private-code "$x" "$B/$b.c"
done
CF="-std=c11 -O1 -g -I$B $(pkg-config --cflags wayland-client xkbcommon)"; LF="$(pkg-config --libs wayland-client xkbcommon)"
cc $CF -o "$B/probe" "$D/probe.c" "$B/wlr-layer-shell-unstable-v1.c" "$B/xdg-shell.c" "$B/virtual-keyboard-unstable-v1.c" \
   "$B/input-method-unstable-v2.c" "$B/fractional-scale-v1.c" "$B/viewporter.c" "$B/xdg-output-unstable-v1.c" $LF
cc $CF -o "$B/target" "$D/target.c" "$B/xdg-shell.c" "$B/text-input-unstable-v3.c" $LF
cc $CF -D_DEFAULT_SOURCE -o "$B/vclick" "$D/vclick.c" "$B/wlr-virtual-pointer-unstable-v1.c" $LF
cc $CF -o "$B/locker" "$D/locker.c" "$B/ext-session-lock-v1.c" $LF
echo "built: $B/probe $B/target $B/vclick $B/locker"
