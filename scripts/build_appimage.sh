#!/bin/sh
# Builds the AppImage. / AppImage előállítása.
# Needs: Qt 6 development packages, cmake, ninja, and a built FFmpeg (scripts/build_ffmpeg.sh).
# linuxdeploy and its Qt plugin are downloaded into build/tools if they are not there yet.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="${TOOLS:-$ROOT/build/tools}"
BUILD="$ROOT/build/release"
APPDIR="$ROOT/build/AppDir"
VERSION="$(sed -n 's/^project(eter VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
OUT="Eter-$VERSION-x86_64.AppImage"

mkdir -p "$TOOLS"
fetch() {
    [ -f "$TOOLS/$1" ] || curl -fsSL -o "$TOOLS/$1" "$2"
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$TOOLS"/linuxdeploy*.AppImage

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --strip
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
QT_QPA_PLATFORM=offscreen "$BUILD/eter" --write-icon "$APPDIR/usr/share/icons/hicolor/256x256/apps/eter.png"

export PATH="$TOOLS:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1          # run the packager without FUSE
export QMAKE="${QMAKE:-$(command -v qmake6)}"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
export EXTRA_QT_PLUGINS="waylandcompositor"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export NO_STRIP=1                          # the bundled strip is too old for current ELF sections
cd "$ROOT/build"
rm -f ./*.AppImage
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --plugin qt --output appimage
# linuxdeploy names the file after the desktop entry; give it a fixed ASCII name.
for f in ./*.AppImage; do
    [ "$f" = "./$OUT" ] || mv "$f" "./$OUT"
done
ls -la "$ROOT/build/$OUT"
