# OKTAKT

A sample sequencer for building drum patterns. Runs as a standalone app or as a VST3 / CLAP plugin (plus Audio Unit on macOS) on Linux, Windows and macOS.

- Up to 16 tracks. Each track has its own grid, so straight, triplet and quintuplet tracks can play side by side.
- Per-hit settings: velocity, timing offset, pan, pitch, stretch, filter, drive and more.
- Each pattern has four parts, A–D. Switch between them while it plays: now, at the end of the bar, or at the end of the part.
- Variation on every pass: play conditions (for example every 2nd pass), chance, retrigger, and randomised hits and timing.
- Slice loops by hits, into equal parts, or by hand, then drop slices onto the grid.
- Time-stretch that keeps pitch, or repitch like a tape.
- In the plugin, each track can go to its own output (Main or Out 2–16).

This is an early version (0.1).

## Download

Installers for Windows and macOS are on the [Releases](https://github.com/jambulatb/oktakt/releases) page. For Linux, build from source (see below).

The installers are not signed, so the system warns you the first time:

- **Windows:** SmartScreen shows a warning. Click *More info*, then *Run anyway*.
- **macOS:** the installer is blocked. Go to *System Settings → Privacy & Security* and click *Open Anyway*.

## Quick start

1. Drag samples from the browser on the left onto the grid. Each sample becomes a track. The app opens on a demo kit; to use your own samples, click **Source…** and pick a folder.
2. Click a cell to add a hit. Press **Space** to play or stop.
3. Click a track name to open its settings: the rows of per-hit values, the panel on the right and the SOUND panel at the bottom.
4. Set up audio under the gear icon → **Settings**. In a DAW, the tempo and transport come from the host.

Patterns are saved to `Documents/OKTAKT/Patterns` (Ctrl+S).

## Build from source

The build script downloads the dependencies (JUCE 8, Signalsmith Stretch, clap-juce-extensions) into `external/`. The first build takes a few minutes.

### Linux (Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build pkg-config git libasound2-dev libjack-jackd2-dev \
     libfreetype-dev libfontconfig-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
     libxcomposite-dev libxext-dev
./scripts/build.sh
```

The script builds into `build/`, installs the plugins to `~/.vst3` and `~/.clap`, and adds OKTAKT to the app menu. Add `--no-install` to only build.

### Windows

You need Visual Studio 2022 or newer (or its Build Tools) with *Desktop development with C++*, and CMake 3.24 or newer.

- `scripts\build-windows.bat` builds the app and the plugins into `dist-windows\`.
- `scripts\build-windows-installer.bat` also packs them into a `Setup.exe`. It needs [Inno Setup](https://jrsoftware.org/isdl.php) 6.5.2 or newer.

### macOS

You need the Xcode Command Line Tools (`xcode-select --install`) and CMake 3.24 or newer (`brew install cmake`).

```bash
bash scripts/build-macos.sh
```

This builds a disk image with an installer in `dist-macos/`. The build is universal (Apple Silicon and Intel). For Apple Silicon only, which is faster to build, run `OKTAKT_ARCHS=arm64 bash scripts/build-macos.sh`. To sign and notarize, see the comments at the top of the script.

### Installers from GitHub Actions

You can also have GitHub build the installers: go to **Actions**, pick *Windows installer* or *macOS installer*, and click **Run workflow**. Pushing a tag like `v0.1.0` starts both. The files appear under the run's *Artifacts*.

## Self-check

`./scripts/build.sh --check` also builds and runs an offline test. It needs no sound card. It renders the engine, checks timing and the sequencer features, and saves screenshots of the UI to `build/check-shots/`.

## Project layout

```
src/Model.*            pattern data, saved as JSON
src/SampleStore.*      sample loading, waveform, hit detection, time-stretch
src/Engine.*           audio engine: sequencer, voices, filters
src/Session.*          editing, undo/redo, saving
src/PluginProcessor.*  plugin: 16 stereo outputs, DAW sync
src/ui/                user interface
tests/Check.cpp        offline self-check
```

The fonts are IBM Plex, under the SIL Open Font License (`resources/fonts/OFL.txt`).
