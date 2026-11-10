#!/usr/bin/env bash
# Downloads the third-party code into external/ (only what is missing).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p external
cd external

clone() {   # name url tag [extra git args]
    local name=$1 url=$2 tag=$3
    shift 3
    if [ -f "$name/CMakeLists.txt" ]; then
        echo "external/$name: already here"
        return
    fi
    echo "external/$name: downloading $tag"
    git clone -q --depth 1 --branch "$tag" "$@" "$url" "$name"
}

clone JUCE https://github.com/juce-framework/JUCE.git 8.0.9
clone signalsmith-linear https://github.com/Signalsmith-Audio/linear.git 0.6.6
clone signalsmith-stretch https://github.com/Signalsmith-Audio/signalsmith-stretch.git main
clone clap-juce-extensions https://github.com/free-audio/clap-juce-extensions.git main --recurse-submodules --shallow-submodules
