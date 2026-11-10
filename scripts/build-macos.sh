#!/bin/bash
# Builds OKTAKT on a Mac and packs it into one installer (.pkg) in dist-macos/.
# The installer lets you pick what to install: the app (Standalone), VST3, AU, CLAP and the demo kit.
#
# Needs: Xcode Command Line Tools (xcode-select --install) and CMake 3.24+ (brew install cmake).
#
# Optional, for sharing the installer with other Macs (needs an Apple Developer account):
#   OKTAKT_SIGN_APP="Developer ID Application: Your Name (TEAMID)"
#   OKTAKT_SIGN_PKG="Developer ID Installer: Your Name (TEAMID)"
#   OKTAKT_NOTARY_PROFILE=<profile saved with: xcrun notarytool store-credentials>
# Without them the bundles get an ad-hoc signature: fine on the Mac that built them.
#
#   OKTAKT_ARCHS="arm64"   build for Apple Silicon only (default: arm64;x86_64, runs on Intel Macs too)
set -euo pipefail   # (empty arrays below use ${a[@]+"${a[@]}"}: macOS ships bash 3.2)
cd "$(dirname "$0")/.."

fail() { echo; echo "Error: $*"; exit 1; }

xcode-select -p >/dev/null 2>&1 || fail "Xcode Command Line Tools are missing. Run: xcode-select --install"
command -v cmake >/dev/null 2>&1 || fail "CMake was not found. Install it with: brew install cmake (or from cmake.org)"

VERSION=$(sed -n 's/^project(OKTAKT VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ARCHS="${OKTAKT_ARCHS:-arm64;x86_64}"
SIGN_APP="${OKTAKT_SIGN_APP:-}"
SIGN_PKG="${OKTAKT_SIGN_PKG:-}"
NOTARY="${OKTAKT_NOTARY_PROFILE:-}"

# ---- build -------------------------------------------------------------------
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
echo "== Building OKTAKT $VERSION for $ARCHS"
cmake -S . -B build-mac ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DOKTAKT_INSTALL_PLUGINS=OFF
cmake --build build-mac --config Release --parallel "$(sysctl -n hw.ncpu)"

ART=build-mac/OKTAKT_artefacts/Release
for b in "Standalone/OKTAKT.app" "VST3/OKTAKT.vst3" "AU/OKTAKT.component"; do
    [ -d "$ART/$b" ] || fail "$ART/$b was not built"
done
HAS_CLAP=0
[ -d "$ART/CLAP/OKTAKT.clap" ] && HAS_CLAP=1

# ---- stage and sign ------------------------------------------------------------
STAGE=build-mac/installer
rm -rf "$STAGE"
mkdir -p "$STAGE"/{app,vst3,au,clap,demo,pkgs,res,au-scripts}
ditto "$ART/Standalone/OKTAKT.app"  "$STAGE/app/OKTAKT.app"
ditto "$ART/VST3/OKTAKT.vst3"       "$STAGE/vst3/OKTAKT.vst3"
ditto "$ART/AU/OKTAKT.component"    "$STAGE/au/OKTAKT.component"
[ $HAS_CLAP = 1 ] && ditto "$ART/CLAP/OKTAKT.clap" "$STAGE/clap/OKTAKT.clap"
ditto resources/demo-kit "$STAGE/demo/Demo Kit"
find "$STAGE/demo" -name ".DS_Store" -delete

sign() {
    xattr -cr "$1"
    if [ -n "$SIGN_APP" ]; then
        codesign --force --deep --timestamp --options runtime --sign "$SIGN_APP" "$1"
    else
        codesign --force --deep --sign - "$1"
    fi
}
echo "== Signing ${SIGN_APP:-ad-hoc}"
sign "$STAGE/app/OKTAKT.app"
sign "$STAGE/vst3/OKTAKT.vst3"
sign "$STAGE/au/OKTAKT.component"
[ $HAS_CLAP = 1 ] && sign "$STAGE/clap/OKTAKT.clap"

# ---- one package per component -----------------------------------------------
# Bundles are not relocatable: the installer always writes to the folder below,
# even if an older OKTAKT.app sits somewhere else.
component_pkg() {   # <name> <root> <install location> [scripts dir]
    local name=$1 root=$2 loc=$3 scripts=${4:-}
    local plist="$STAGE/$name.plist" args=()
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
        i=$((i + 1))
    done
    [ $i -gt 0 ] && args+=(--component-plist "$plist")
    [ -n "$scripts" ] && args+=(--scripts "$scripts")
    pkgbuild --root "$root" ${args[@]+"${args[@]}"} --identifier "com.oktakt.pkg.$name" --version "$VERSION" \
             --install-location "$loc" "$STAGE/pkgs/$name.pkg" >/dev/null
}

# After an AU is installed, make Logic / GarageBand see it without a restart
cat > "$STAGE/au-scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
chmod +x "$STAGE/au-scripts/postinstall"

echo "== Packing"
component_pkg app  "$STAGE/app"  "/Applications"
component_pkg vst3 "$STAGE/vst3" "/Library/Audio/Plug-Ins/VST3"
component_pkg au   "$STAGE/au"   "/Library/Audio/Plug-Ins/Components" "$STAGE/au-scripts"
[ $HAS_CLAP = 1 ] && component_pkg clap "$STAGE/clap" "/Library/Audio/Plug-Ins/CLAP"
component_pkg demo "$STAGE/demo" "/Library/Application Support/OKTAKT"

# ---- the installer with choices ------------------------------------------------
HOST_ARCHS=$(echo "$ARCHS" | tr ';' ',')
CLAP_LINE="" CLAP_CHOICE="" CLAP_REF=""
if [ $HAS_CLAP = 1 ]; then
    CLAP_LINE='<line choice="clap"/>'
    CLAP_CHOICE='<choice id="clap" title="CLAP plug-in" description="For DAWs with CLAP support (Bitwig, Reaper and others). Goes to /Library/Audio/Plug-Ins/CLAP." start_selected="true"><pkg-ref id="com.oktakt.pkg.clap"/></choice>'
    CLAP_REF="<pkg-ref id=\"com.oktakt.pkg.clap\" version=\"$VERSION\" onConclusion=\"none\">clap.pkg</pkg-ref>"
fi

cat > "$STAGE/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>OKTAKT $VERSION</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="always" require-scripts="false" hostArchitectures="$HOST_ARCHS"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <os-version min="11.0"/>
    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
        $CLAP_LINE
        <line choice="demo"/>
    </choices-outline>
    <choice id="app" title="OKTAKT app (Standalone)" description="The app on its own, without a DAW. Goes to /Applications." start_selected="true"><pkg-ref id="com.oktakt.pkg.app"/></choice>
    <choice id="vst3" title="VST3 plug-in" description="For Ableton Live, Cubase, Reaper, Bitwig, FL Studio and others. Goes to /Library/Audio/Plug-Ins/VST3." start_selected="true"><pkg-ref id="com.oktakt.pkg.vst3"/></choice>
    <choice id="au" title="Audio Unit plug-in" description="For Logic Pro, GarageBand and other AU hosts. Goes to /Library/Audio/Plug-Ins/Components." start_selected="true"><pkg-ref id="com.oktakt.pkg.au"/></choice>
    $CLAP_CHOICE
    <choice id="demo" title="Demo kit" description="Drums, breaks and loops to start with (3.5 MB). The app opens on it the first time. Goes to /Library/Application Support/OKTAKT." start_selected="true"><pkg-ref id="com.oktakt.pkg.demo"/></choice>
    <pkg-ref id="com.oktakt.pkg.app" version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.pkg.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.pkg.au" version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
    $CLAP_REF
    <pkg-ref id="com.oktakt.pkg.demo" version="$VERSION" onConclusion="none">demo.pkg</pkg-ref>
</installer-gui-script>
EOF

STYLE='<style>body{font-family:-apple-system,"Helvetica Neue",sans-serif;font-size:13px;line-height:1.45}h2{font-size:16px;margin:0 0 8px}li{margin:3px 0}</style>'
cat > "$STAGE/res/welcome.html" <<EOF
<html><head><meta charset="utf-8">$STYLE</head><body>
<h2>OKTAKT $VERSION</h2>
<p>A step sequencer and sampler, as an app and as a plug-in for your DAW.</p>
<p>On the next screens pick what to install: the app, the VST3, Audio Unit and CLAP plug-ins, and the demo kit. Untick anything you do not need.</p>
</body></html>
EOF
cat > "$STAGE/res/conclusion.html" <<EOF
<html><head><meta charset="utf-8">$STYLE</head><body>
<h2>OKTAKT is installed</h2>
<ul>
<li>The app is in <b>Applications</b>.</li>
<li>In your DAW, rescan plug-ins if OKTAKT does not show up: it is an instrument by OKTAKT.</li>
<li>Logic Pro: for separate track outputs, load it as <b>Multi Output (16xStereo)</b>.</li>
</ul>
</body></html>
EOF

mkdir -p dist-macos
PKG="dist-macos/OKTAKT-$VERSION-macOS.pkg"
rm -f "$PKG"
SIGN_ARGS=()
[ -n "$SIGN_PKG" ] && SIGN_ARGS=(--sign "$SIGN_PKG" --timestamp)
productbuild --distribution "$STAGE/distribution.xml" --resources "$STAGE/res" \
             --package-path "$STAGE/pkgs" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$PKG"

if [ -n "$NOTARY" ]; then
    [ -n "$SIGN_PKG" ] || fail "Notarizing needs OKTAKT_SIGN_PKG and OKTAKT_SIGN_APP"
    echo "== Notarizing (a few minutes)"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY" --wait
    xcrun stapler staple "$PKG"
fi

echo
echo "Done: $PWD/$PKG"
echo "Double-click it; the 'Installation Type' step has Customize, where you tick what to install."
if [ -z "$SIGN_PKG" ]; then
    echo "The installer is not signed with a Developer ID: it opens on this Mac. On another Mac,"
    echo "macOS blocks it at first: System Settings > Privacy & Security > Open Anyway."
fi
