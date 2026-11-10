#!/bin/bash
# Builds OKTAKT on a Mac and packs it into a disk image (.dmg) in dist-macos/ with two installers:
#   Install OKTAKT.pkg    tick what to install: the app (Standalone), VST3, AU, CLAP and the demo kit
#   Uninstall OKTAKT.pkg  tick what to remove; lists only the parts installed on this Mac
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
<li>To remove OKTAKT later, run <b>Uninstall OKTAKT</b> from the same disk image.</li>
</ul>
</body></html>
EOF

# ---- the uninstaller -----------------------------------------------------------
# Payload-free packages: each one only runs a script that removes one component.
# Its checkboxes show only what is installed on this Mac; settings are off by default,
# saved patterns in ~/Documents/OKTAKT are never touched.
UN="$STAGE/uninstall"
mkdir -p "$UN"/{pkgs,res}

remover_pkg() {   # <name> <script body>
    local name=$1 body=$2 dir="$UN/scripts-$1"
    mkdir -p "$dir"
    printf '#!/bin/sh\n%s\nexit 0\n' "$body" > "$dir/postinstall"
    chmod +x "$dir/postinstall"
    pkgbuild --nopayload --scripts "$dir" --identifier "com.oktakt.uninstall.$name" --version "$VERSION" \
             "$UN/pkgs/remove-$name.pkg" >/dev/null
}
remover_pkg app  'rm -rf "/Applications/OKTAKT.app"; pkgutil --forget com.oktakt.pkg.app >/dev/null 2>&1 || true'
remover_pkg vst3 'rm -rf "/Library/Audio/Plug-Ins/VST3/OKTAKT.vst3"; pkgutil --forget com.oktakt.pkg.vst3 >/dev/null 2>&1 || true'
remover_pkg au   'rm -rf "/Library/Audio/Plug-Ins/Components/OKTAKT.component"; pkgutil --forget com.oktakt.pkg.au >/dev/null 2>&1 || true
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true'
remover_pkg clap 'rm -rf "/Library/Audio/Plug-Ins/CLAP/OKTAKT.clap"; pkgutil --forget com.oktakt.pkg.clap >/dev/null 2>&1 || true'
remover_pkg demo 'rm -rf "/Library/Application Support/OKTAKT"; pkgutil --forget com.oktakt.pkg.demo >/dev/null 2>&1 || true'
remover_pkg settings 'U=$(stat -f %Su /dev/console)
H=$(dscl . -read "/Users/$U" NFSHomeDirectory 2>/dev/null | sed "s/^NFSHomeDirectory: //")
[ -n "$H" ] && [ -d "$H/Library/Application Support/OKTAKT" ] && rm -rf "$H/Library/Application Support/OKTAKT"'

cat > "$UN/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Uninstall OKTAKT</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="always" require-scripts="true" hostArchitectures="$HOST_ARCHS"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <installation-check script="canUninstall()"/>
    <script><![CDATA[
var P = {
    app:  '/Applications/OKTAKT.app',
    vst3: '/Library/Audio/Plug-Ins/VST3/OKTAKT.vst3',
    au:   '/Library/Audio/Plug-Ins/Components/OKTAKT.component',
    clap: '/Library/Audio/Plug-Ins/CLAP/OKTAKT.clap',
    demo: '/Library/Application Support/OKTAKT'
};
function has(k) { return system.files.fileExistsAtPath(P[k]); }
function canUninstall() {
    if (!(has('app') || has('vst3') || has('au') || has('clap') || has('demo'))) {
        my.result.type = 'Fatal';
        my.result.title = 'OKTAKT is not installed';
        my.result.message = 'There is nothing to remove on this Mac.';
        return false;
    }
    var running = system.applications.fromIdentifier('com.oktakt.sampler');
    if (running && (running.length === undefined || running.length > 0)) {
        my.result.type = 'Fatal';
        my.result.title = 'Quit OKTAKT first';
        my.result.message = 'The OKTAKT app is open. Quit it and run the uninstaller again.';
        return false;
    }
    return true;
}
    ]]></script>
    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="clap"/>
        <line choice="demo"/>
        <line choice="settings"/>
    </choices-outline>
    <choice id="app" title="OKTAKT app (Standalone)" description="Removes /Applications/OKTAKT.app." start_visible="has('app')" start_selected="has('app')"><pkg-ref id="com.oktakt.uninstall.app"/></choice>
    <choice id="vst3" title="VST3 plug-in" description="Removes OKTAKT.vst3 from /Library/Audio/Plug-Ins/VST3. Projects that use it will open without it." start_visible="has('vst3')" start_selected="has('vst3')"><pkg-ref id="com.oktakt.uninstall.vst3"/></choice>
    <choice id="au" title="Audio Unit plug-in" description="Removes OKTAKT.component from /Library/Audio/Plug-Ins/Components." start_visible="has('au')" start_selected="has('au')"><pkg-ref id="com.oktakt.uninstall.au"/></choice>
    <choice id="clap" title="CLAP plug-in" description="Removes OKTAKT.clap from /Library/Audio/Plug-Ins/CLAP." start_visible="has('clap')" start_selected="has('clap')"><pkg-ref id="com.oktakt.uninstall.clap"/></choice>
    <choice id="demo" title="Demo kit" description="Removes /Library/Application Support/OKTAKT with the demo samples." start_visible="has('demo')" start_selected="has('demo')"><pkg-ref id="com.oktakt.uninstall.demo"/></choice>
    <choice id="settings" title="App settings" description="Removes the audio device and other app settings from your user Library. Saved patterns in Documents/OKTAKT stay." start_selected="false"><pkg-ref id="com.oktakt.uninstall.settings"/></choice>
    <pkg-ref id="com.oktakt.uninstall.app" version="$VERSION" onConclusion="none">remove-app.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.uninstall.vst3" version="$VERSION" onConclusion="none">remove-vst3.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.uninstall.au" version="$VERSION" onConclusion="none">remove-au.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.uninstall.clap" version="$VERSION" onConclusion="none">remove-clap.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.uninstall.demo" version="$VERSION" onConclusion="none">remove-demo.pkg</pkg-ref>
    <pkg-ref id="com.oktakt.uninstall.settings" version="$VERSION" onConclusion="none">remove-settings.pkg</pkg-ref>
</installer-gui-script>
EOF

cat > "$UN/res/welcome.html" <<EOF
<html><head><meta charset="utf-8">$STYLE</head><body>
<h2>Uninstall OKTAKT</h2>
<p>On the next screens tick what to remove. Only the parts installed on this Mac are listed.</p>
<p>Your saved patterns in <b>Documents/OKTAKT</b> and your own samples are not touched.</p>
</body></html>
EOF
cat > "$UN/res/conclusion.html" <<EOF
<html><head><meta charset="utf-8">$STYLE</head><body>
<h2>OKTAKT is removed</h2>
<p>The parts you ticked are gone. In your DAW, rescan plug-ins so it forgets OKTAKT.</p>
</body></html>
EOF

# ---- the disk image ------------------------------------------------------------
DMGSRC="$STAGE/dmg"
mkdir -p "$DMGSRC"
SIGN_ARGS=()
[ -n "$SIGN_PKG" ] && SIGN_ARGS=(--sign "$SIGN_PKG" --timestamp)
productbuild --distribution "$STAGE/distribution.xml" --resources "$STAGE/res" \
             --package-path "$STAGE/pkgs" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$DMGSRC/Install OKTAKT.pkg"
productbuild --distribution "$UN/distribution.xml" --resources "$UN/res" \
             --package-path "$UN/pkgs" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$DMGSRC/Uninstall OKTAKT.pkg"

mkdir -p dist-macos
DMG="dist-macos/OKTAKT-$VERSION-macOS.dmg"
rm -f "$DMG"
echo "== Making $DMG"
n=0
until hdiutil create -volname "OKTAKT $VERSION" -srcfolder "$DMGSRC" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null; do
    n=$((n + 1)); [ $n -ge 3 ] && fail "hdiutil could not create the disk image"
    sleep 3   # "Resource busy" happens now and then; a retry clears it
done
[ -n "$SIGN_APP" ] && codesign --force --timestamp --sign "$SIGN_APP" "$DMG"

if [ -n "$NOTARY" ]; then
    [ -n "$SIGN_PKG" ] && [ -n "$SIGN_APP" ] || fail "Notarizing needs OKTAKT_SIGN_PKG and OKTAKT_SIGN_APP"
    echo "== Notarizing (a few minutes)"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY" --wait
    xcrun stapler staple "$DMG"
fi

echo
echo "Done: $PWD/$DMG"
echo "Open it: 'Install OKTAKT' has Customize on the 'Installation Type' step, where you tick what to install;"
echo "'Uninstall OKTAKT' removes what you tick."
if [ -z "$SIGN_PKG" ]; then
    echo "The installers are not signed with a Developer ID: they open on this Mac. On another Mac,"
    echo "macOS blocks them at first: System Settings > Privacy & Security > Open Anyway."
fi
