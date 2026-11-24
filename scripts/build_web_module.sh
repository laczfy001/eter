#!/bin/sh
# Packs the universal Éter Web module: build/eter-web.zip (upload the eter-web folder to any PHP web host).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
rm -f "$ROOT/build/eter-web.zip"
cd "$ROOT/web"
# The data folder ships empty (only its protection files); the settings and the key are created on the first visit.
zip -qr "$ROOT/build/eter-web.zip" eter-web -x 'eter-web/data/*.php' 'eter-web/data/.lock' 'eter-web/data/*.tmp'
ls -la "$ROOT/build/eter-web.zip"
