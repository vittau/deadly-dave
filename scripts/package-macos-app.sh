#!/bin/sh
#
# Wraps the plain executable in a double-clickable macOS .app bundle, with the
# game resources inside it. The game then opens as a window on its own, without
# a terminal, and finds 'res' through the bundle instead of the directory it is
# started from.
#
# usage: package-macos-app.sh <binary> <app-bundle> [version]
set -eu

BIN="$1"
APP="$2"
VERSION="${3:-1.0}"

if [ ! -x "$BIN" ]; then
    echo "no executable at '$BIN'" >&2
    exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/deadly-dave"
cp -R res "$APP/Contents/Resources/res"
rm -rf "$APP/Contents/Resources/res/screenshots"

ICON_LINE=
if [ -f assets/icon.icns ]; then
    cp assets/icon.icns "$APP/Contents/Resources/deadly-dave.icns"
    ICON_LINE='    <key>CFBundleIconFile</key><string>deadly-dave.icns</string>'
fi

# SDL documents that a bundled app looks for its resources in this directory.
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>deadly-dave</string>
    <key>CFBundleIdentifier</key><string>io.github.vittau.deadly-dave</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>Deadly Dave</string>
    <key>CFBundleDisplayName</key><string>Deadly Dave</string>
    <key>CFBundlePackageType</key><string>APPL</string>
${ICON_LINE}
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Ad-hoc signature, which Apple Silicon requires even for local builds.
codesign --force --sign - "$APP"
