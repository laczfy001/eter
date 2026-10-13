#!/bin/sh
# Builds the Windows package (a zip with eter.exe and every DLL it needs).
# Run it in an MSYS2 "UCRT64" shell. Packages needed:
#   pacman -S --needed git make diffutils zip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
#       mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-qt6-base
# and a built FFmpeg (scripts/build_ffmpeg.sh, in the same shell).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/win"
VERSION="$(sed -n 's/^project(eter VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="Eter-$VERSION-win64"
DIST="$ROOT/build/$NAME"
PREFIX="${MINGW_PREFIX:-/ucrt64}"

# Files dated in the future (unpacked from an archive made in another time zone, or a wrong system
# clock) make ninja re-run CMake forever: "manifest 'build.ninja' still dirty after 100 tries".
find "$ROOT" -path "$ROOT/build" -prune -o -type f -newermt "$(date '+%Y-%m-%d %H:%M:%S')" -exec touch {} +

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

rm -rf "$DIST" "$ROOT/build/$NAME.zip"
mkdir -p "$DIST"
cp "$BUILD/eter.exe" "$DIST/"
# Szerkesztő mód indítója: egy másik gépen futó playout playlistjének szerkesztése.
printf '@echo off\r\nstart "" "%%~dp0eter.exe" --editor\r\n' > "$DIST/Eter-szerkeszto.cmd"

# Qt libraries and plugins
DEPLOY=""
for tool in windeployqt6 windeployqt-qt6 windeployqt; do
    if command -v "$tool" >/dev/null 2>&1; then DEPLOY="$tool"; break; fi
done
[ -n "$DEPLOY" ] || { echo "windeployqt not found (install mingw-w64-ucrt-x86_64-qt6-base)"; exit 1; }
"$DEPLOY" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "$DIST/eter.exe"
# Only the SQLite database driver is used.
if [ -d "$DIST/sqldrivers" ]; then
    find "$DIST/sqldrivers" -type f ! -name 'qsqlite*' -delete
fi

# Everything else the program and the plugins link to from MSYS2 (ICU, zlib, the compiler runtime, …).
# Repeated until no new DLL turns up, because the copied DLLs have dependencies of their own.
while :; do
    added=0
    for f in $(find "$DIST" -name '*.exe' -o -name '*.dll'); do
        for dep in $(ldd "$f" 2>/dev/null | awk '{print $3}' | grep -i "^$PREFIX/" || true); do
            if [ ! -f "$DIST/$(basename "$dep")" ]; then
                cp "$dep" "$DIST/"
                added=1
            fi
        done
    done
    [ "$added" = 1 ] || break
done

# Licences: Éter's own, the notices, and those of the bundled MSYS2 packages.
cp "$ROOT/LICENSE" "$DIST/LICENSE.txt"
cp "$ROOT/THIRD-PARTY-NOTICES.md" "$ROOT/README.md" "$ROOT/README.hu.md" "$DIST/"
mkdir -p "$DIST/licenses"
cp "$ROOT/resources/fonts/OFL.txt" "$DIST/licenses/IBM-Plex-OFL.txt"
for dll in "$DIST"/*.dll; do
    pkg="$(pacman -Qqo "$PREFIX/bin/$(basename "$dll")" 2>/dev/null || true)"
    [ -n "$pkg" ] || continue
    short="${pkg#mingw-w64-ucrt-x86_64-}"
    if [ -d "$PREFIX/share/licenses/$short" ] && [ ! -d "$DIST/licenses/$short" ]; then
        cp -r "$PREFIX/share/licenses/$short" "$DIST/licenses/"
    fi
done

cd "$ROOT/build"
zip -qr "$NAME.zip" "$NAME"
ls -la "$ROOT/build/$NAME.zip"
