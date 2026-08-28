#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the geometry script into KWin and turns it on.
#
# A Wayland client cannot see other clients' windows, so this script is not an optional
# extra: without it DragonPerch draws pets that have nothing to stand on but the floor.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
name=dragonperch-geometry
target="${XDG_DATA_HOME:-$HOME/.local/share}/kwin/scripts/$name"

echo "installing to $target"
rm -rf "$target"
mkdir -p "$(dirname "$target")"
cp -r "$script_dir/$name" "$target"

# The script has to be listed in kwinrc before KWin will consider it, and the group name is
# the plugin id with "Enabled" appended.
kwriteconfig6 --file kwinrc --group Plugins --key "${name}Enabled" true

# Reloading the scripting subsystem rather than restarting KWin: restarting the compositor
# on Wayland takes the whole session's windows with it.
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.stop >/dev/null 2>&1 || true
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1 || true

echo "installed and enabled."
echo
echo "Check it is talking, with DragonPerch running:"
echo "    ./build/linux-x64/src/linux/Debug/dragonperch-wl --dump-world --hold"
echo
echo "(--dump-world is a diagnostic, so it is in the Debug build. For a release binary"
echo " that still has it, configure with -D DRAGONPERCH_DIAGNOSTICS=ON.)"
echo
echo "If nothing arrives, the script's own output is in the journal:"
echo "    journalctl --user -f -t kwin_wayland"
