#!/bin/bash
# Builds "Filter Lab.app" in this folder. Needs only the Xcode Command Line Tools.
set -euo pipefail
cd "$(dirname "$0")"

APP="Filter Lab.app"

echo "Compiling…"
swift build -c release 2>&1 | grep -v "ld: warning: search path"
BIN_DIR="$(swift build -c release --show-bin-path)"

if [ ! -f Resources/AppIcon.icns ]; then
  echo "Drawing icon…"
  swift scripts/make_icon.swift
fi

echo "Packaging…"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/FilterLab" "$APP/Contents/MacOS/FilterLab"
cp Resources/Info.plist "$APP/Contents/Info.plist"
cp Resources/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
codesign --force --sign - "$APP" >/dev/null 2>&1

echo "Done: $(pwd)/$APP"
