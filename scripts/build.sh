#!/usr/bin/env bash
# Builds OKTAKT (Standalone, VST3, CLAP) in ./build.
#   ./scripts/build.sh            Release build, copies VST3 to ~/.vst3 and CLAP to ~/.clap
#   ./scripts/build.sh --no-install   same, without copying the plugins
#   ./scripts/build.sh --check    also builds and runs the offline self-check
set -euo pipefail
cd "$(dirname "$0")/.."

INSTALL=ON
CHECK=OFF
for arg in "$@"; do
    case "$arg" in
        --no-install) INSTALL=OFF ;;
        --check) CHECK=ON ;;
        *) echo "unknown option: $arg"; exit 2 ;;
    esac
done

missing=()
for tool in cmake g++ pkg-config git; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if command -v pkg-config >/dev/null; then
    for pc in alsa jack freetype2 fontconfig x11 xrandr xinerama xcursor xext; do
        pkg-config --exists "$pc" || missing+=("$pc")
    done
fi
if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing: ${missing[*]}"
    echo
    echo "Install the build dependencies first:"
    echo "  sudo apt update"
    echo "  sudo apt install build-essential cmake ninja-build pkg-config git libasound2-dev libjack-jackd2-dev \\"
    echo "       libfreetype-dev libfontconfig-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \\"
    echo "       libxcomposite-dev libxext-dev"
    exit 1
fi

./scripts/fetch-deps.sh
[ -d resources/demo-kit ] || python3 scripts/make-demo-kit.py

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DOKTAKT_INSTALL_PLUGINS=$INSTALL -DOKTAKT_TESTS=$CHECK
cmake --build build --parallel "$(nproc)"

if [ "$CHECK" = ON ]; then
    ./build/OKTAKT_Check_artefacts/Release/OKTAKT_Check resources/demo-kit build/check-shots \
        build/OKTAKT_artefacts/Release/VST3/OKTAKT.vst3
fi

[ "$INSTALL" = ON ] && ./scripts/install-desktop.sh

echo
echo "Standalone: ./build/OKTAKT_artefacts/Release/Standalone/OKTAKT"
echo "VST3:       ./build/OKTAKT_artefacts/Release/VST3/OKTAKT.vst3$( [ "$INSTALL" = ON ] && echo "  (copied to ~/.vst3)")"
echo "CLAP:       ./build/OKTAKT_artefacts/Release/CLAP/OKTAKT.clap$( [ "$INSTALL" = ON ] && echo "  (copied to ~/.clap)")"
