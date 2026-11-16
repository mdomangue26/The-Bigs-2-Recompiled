#!/usr/bin/env bash
# Build the Apple Silicon Mac app and zip it. Run on the Mac.
#
#   packaging/build-macos.sh                   # -> out/macos/TheBigs2-macos-arm64.zip
#
# Needs (docs/BUILDING.md, macOS):
#   - macOS 13.3 or newer on Apple Silicon, Xcode 26 or newer (AppleClang 17)
#   - CMake and Ninja (brew install cmake ninja)
#   - the LunarG Vulkan SDK, its setup-env.sh sourced in this shell (Vulkan
#     loader and MoltenVK)
#   - the ReXGlue SDK built and installed on this Mac, in REXSDK (default
#     ~/rexsdk-install)
#   - the game's default.xex in game/ (codegen reads it; it is not packaged)
#
# The app holds the executable, the ReXGlue runtime, SDL3, the Vulkan loader
# and MoltenVK, but no game data: on first launch it asks for your disc image
# (.iso) and extracts it to ~/Library/Application Support/thebigs2/game.
# It is signed ad hoc (no Apple Developer ID), so macOS asks before the
# first launch; see the README.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRESET="${PRESET:-mac-arm64-release}"
BUILD_DIR="$ROOT/out/build/$PRESET"
OUT_DIR="${OUT_DIR:-$ROOT/out/macos}"
REXSDK="${REXSDK:-$HOME/rexsdk-install}"
APP_NAME="The Bigs 2"
ZIP_NAME=TheBigs2-macos-arm64
APP="$OUT_DIR/$APP_NAME.app"

[ "$(uname -s)" = Darwin ] || { echo "Run this on a Mac." >&2; exit 1; }
[ "$(uname -m)" = arm64 ] || echo "Warning: not an Apple Silicon Mac; building $PRESET anyway." >&2
[ -f "$REXSDK/lib/cmake/rexglue/rexglueConfig.cmake" ] ||
  { echo "No ReXGlue SDK at $REXSDK (set REXSDK)." >&2; exit 1; }
[ -n "${VULKAN_SDK:-}" ] ||
  { echo "Source the Vulkan SDK's setup-env.sh first (VULKAN_SDK is not set)." >&2; exit 1; }
[ -f "$ROOT/game/default.xex" ] ||
  { echo "Put the game's files (default.xex) in $ROOT/game for codegen." >&2; exit 1; }

configure() {
  cmake --preset "$PRESET" -DCMAKE_PREFIX_PATH="$REXSDK" -Wno-dev >/dev/null
}
# A fresh checkout builds twice: the generated source list is read at configure time.
configure
if ! cmake --build --preset "$PRESET" || [ ! -f "$BUILD_DIR/thebigs2" ]; then
  configure
  cmake --build --preset "$PRESET"
fi

echo "Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# The executable with everything the SDK staged next to it: its libraries,
# SDL3, and vulkan/ (loader, MoltenVK and its ICD file), which the SDK finds
# next to the executable.
for item in "$BUILD_DIR"/thebigs2 "$BUILD_DIR"/*.dylib "$BUILD_DIR"/vulkan; do
  [ -e "$item" ] && cp -R "$item" "$APP/Contents/MacOS/"
done
[ -f "$APP/Contents/MacOS/vulkan/lib/libMoltenVK.dylib" ] ||
  { echo "The build did not stage MoltenVK (vulkan/lib/libMoltenVK.dylib)." >&2; exit 1; }

# Icon from the 1024 px PNG.
ICONSET="$OUT_DIR/thebigs2.iconset"
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
  sips -z $size $size "$ROOT/packaging/macos/thebigs2-1024.png" \
    --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips -z $double $double "$ROOT/packaging/macos/thebigs2-1024.png" \
    --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/thebigs2.icns"
rm -rf "$ICONSET"

VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo dev)"
sed -e "s/@VERSION@/${VERSION#v}/g" "$ROOT/packaging/macos/Info.plist" > "$APP/Contents/Info.plist"
cp "$ROOT/LICENSE" "$APP/Contents/Resources/"

# Ad hoc signature: Apple Silicon runs only signed code, and the bundle must be
# signed as a whole after every file is in place.
codesign --force --deep --sign - --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"

rm -f "$OUT_DIR/$ZIP_NAME.zip"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT_DIR/$ZIP_NAME.zip"
echo "Done: $OUT_DIR/$ZIP_NAME.zip"
