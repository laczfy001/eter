#!/bin/sh
# Packs the Éter Radio WordPress plugin: build/eter-radio.zip (install it in WordPress: Plugins → Add New → Upload).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
rm -f "$ROOT/build/eter-radio.zip"
cd "$ROOT/wordpress"
zip -qr "$ROOT/build/eter-radio.zip" eter-radio -x '*/.*'
ls -la "$ROOT/build/eter-radio.zip"
