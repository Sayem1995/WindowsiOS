#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds a fully UNSIGNED WindowsiOS.ipa on an Apple-Silicon macOS machine.
# Used by the GitHub Actions workflow (ci/build.yml) and by local builds.
#
#   bash scripts/ci_build.sh [clean]
#
# Output: out/WindowsiOS-unsigned.ipa
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.cargo/bin:$PATH"

echo "=== host ==="
uname -m
sysctl -n hw.ncpu
xcodebuild -version || true
xcrun --sdk iphoneos --show-sdk-version || true

if [ "${1:-}" = "clean" ]; then
  rm -rf work/ios_deps_build ios/deps build/ios build/DerivedData out
fi

echo "=== tool deps ==="
command -v meson >/dev/null || { echo "meson missing: brew install meson ninja pkg-config python"; exit 1; }
command -v ninja >/dev/null || { echo "ninja missing"; exit 1; }
command -v pkg-config >/dev/null || { echo "pkg-config missing"; exit 1; }
command -v xcodegen >/dev/null || { echo "xcodegen missing: brew install xcodegen"; exit 1; }

echo "=== 1/5 cross-compile iOS deps (libffi, glib, libslirp) ==="
chmod +x scripts/*.sh
./scripts/build_ios_deps.sh

echo "=== 2/5 engine static library ==="
make -j"$(sysctl -n hw.ncpu)" ios-lib

echo "=== 3/5 xcode project ==="
( cd ios && xcodegen generate --spec project.yml )

echo "=== 4/5 app build (unsigned) ==="
( cd ios && xcodebuild \
    -project WindowsiOS.xcodeproj \
    -scheme WindowsiOS \
    -configuration Release \
    -sdk iphoneos \
    -derivedDataPath ../build/DerivedData \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGN_IDENTITY="" \
    PROVISIONING_PROFILE_SPECIFIER="" \
    DEVELOPMENT_TEAM="" \
    build )

echo "=== 5/5 package IPA ==="
APP_DIR="$ROOT/build/DerivedData/Build/Products/Release-iphoneos"
APP="$(find "$APP_DIR" -maxdepth 1 -name '*.app' | head -n1)"
[ -n "$APP" ] || { echo "no .app produced"; exit 1; }
echo "app: $APP"
rm -rf out && mkdir -p out/Payload
cp -R "$APP" out/Payload/
/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Info.plist" || true
/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP/Info.plist" || true
lipo -info "$APP/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Info.plist")" || true
du -sh "$APP"
( cd out && zip -qry WindowsiOS-unsigned.ipa Payload )
ls -lh out/WindowsiOS-unsigned.ipa
echo "OK -> out/WindowsiOS-unsigned.ipa"
