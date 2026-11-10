#!/usr/bin/env bash
# Adds OKTAKT to the Linux app menu with its icon (~/.local/share, no sudo).
#   ./scripts/install-desktop.sh [path/to/OKTAKT]   default: ./build/OKTAKT_artefacts/Release/Standalone/OKTAKT
#
# GNOME (and other Wayland desktops) take a window's icon from the .desktop file whose
# StartupWMClass matches the window, not from the icon the window carries itself.
# Without this entry the dock and Alt+Tab show a generic icon, even when OKTAKT is started from a terminal.
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="$(realpath "${1:-build/OKTAKT_artefacts/Release/Standalone/OKTAKT}")"
[ -x "$BIN" ] || { echo "Not found: $BIN"; exit 1; }

DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
mkdir -p "$DATA/applications" "$DATA/icons/hicolor/256x256/apps"
cp resources/icon/icon-256.png "$DATA/icons/hicolor/256x256/apps/oktakt.png"

cat > "$DATA/applications/oktakt.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=OKTAKT
Comment=Sampler and step sequencer
Exec="$BIN"
Icon=oktakt
Terminal=false
Categories=AudioVideo;Audio;Music;
StartupWMClass=OKTAKT
DESKTOP

command -v update-desktop-database >/dev/null && update-desktop-database -q "$DATA/applications" || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$DATA/icons/hicolor" || true
echo "App menu entry: $DATA/applications/oktakt.desktop -> $BIN"
