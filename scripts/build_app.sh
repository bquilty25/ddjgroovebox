#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build DDJ Groovebox.app (release) into build/. Usage: scripts/build_app.sh [--open]
set -e
cd "$(dirname "$0")/.."
for h in felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h felucca_fm6.h sloop_logo.h; do
    [ -f "vendor/sloop/build/gen/$h" ] || { sh scripts/gen_headers.sh; break; }
done
swift build -c release --product DDJGroovebox
BIN="$(swift build -c release --show-bin-path)/DDJGroovebox"
APP="build/DDJ Groovebox.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/DDJGroovebox"
cp docs/FAQ.md Sources/DDJGroovebox/Resources/FAQ.md          # (the in-app help: always the current docs)
cp docs/TRIGDECK.md Sources/DDJGroovebox/Resources/TRIGDECK.md
cp Sources/DDJGroovebox/Resources/SLOOPTerminus.ttf Sources/DDJGroovebox/Resources/Terminus-LICENSE.txt \
   Sources/DDJGroovebox/Resources/FAQ.md Sources/DDJGroovebox/Resources/TRIGDECK.md "$APP/Contents/Resources/"
# the icon: drawn by scripts/make_icon.swift (redrawn when the script changes)
if [ ! -f build/AppIcon.icns ] || [ scripts/make_icon.swift -nt build/AppIcon.icns ]; then
    rm -rf build/AppIcon.iconset
    swift scripts/make_icon.swift build/AppIcon.iconset >/dev/null
    iconutil -c icns build/AppIcon.iconset -o build/AppIcon.icns
fi
cp build/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>DDJ Groovebox</string>
    <key>CFBundleDisplayName</key><string>DDJ Groovebox</string>
    <key>CFBundleIdentifier</key><string>org.ddjgroovebox.app</string>
    <key>CFBundleExecutable</key><string>DDJGroovebox</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>GPL-3.0. SLOOP by isod89, based on Felucca by Leo Kuroshita.</string>
</dict>
</plist>
PLIST
codesign --force --sign - "$APP" >/dev/null 2>&1 || true
echo "built: $APP"
[ "$1" = "--open" ] && open "$APP"
exit 0
