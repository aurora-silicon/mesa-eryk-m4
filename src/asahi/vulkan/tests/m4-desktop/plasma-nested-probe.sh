#!/bin/sh
set -eu
export XDG_CONFIG_HOME="$(dirname "$MESA_SHADER_CACHE_DIR")/plasma-config"
export XDG_DATA_HOME="$(dirname "$MESA_SHADER_CACHE_DIR")/plasma-data"
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
exec dbus-run-session -- setpriv --no-new-privs /home/e/.local/libexec/kwin-probe/kwin_wayland --wayland-display "$WAYLAND_DISPLAY" --socket m4-plasma-test --width 1280 --height 800 --exit-with-session /home/e/.local/libexec/plasma-nested-shell.sh
