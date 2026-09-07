#!/bin/bash
# 打出可双击的 TunnelApp.app（产物在 TunnelMac/TunnelApp.app，不进 git）
set -euo pipefail
cd "$(dirname "$0")"

MODE="release"
if [[ "${1:-}" == "--debug" ]]; then MODE="debug"; fi
FLAG="--configuration $MODE"

# shellcheck disable=SC2086
swift build $FLAG --target TunnelApp

rm -rf TunnelApp.app
mkdir -p TunnelApp.app/Contents/MacOS
cp ".build/$MODE/TunnelApp" TunnelApp.app/Contents/MacOS/TunnelApp
cp TunnelApp-Info.plist TunnelApp.app/Contents/Info.plist
codesign --sign - --force --deep TunnelApp.app >/dev/null 2>&1 || true

echo "ok: $(pwd)/TunnelApp.app"
