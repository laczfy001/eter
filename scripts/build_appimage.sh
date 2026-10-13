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
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --plugin qt

# Not used by Éter: TLS and network-information plugins (the remote editing protocol is plain TCP with its own
# challenge-response login), and Qt's own translations (Éter carries its own). Without them the libraries they
# pulled in (OpenSSL, GnuTLS, …) are dropped as well.
rm -rf "$APPDIR/usr/plugins/tls" "$APPDIR/usr/plugins/networkinformation" "$APPDIR/usr/translations"
# libproxy (and libpxbackend, which Qt also lists) would bring curl and its TLS stack; see packaging/libproxy-stub.c.
if [ -f "$APPDIR/usr/lib/libproxy.so.1" ]; then
    printf 'LIBPROXY_0.4.16 { global: px_proxy_factory_new; px_proxy_factory_get_proxies; px_proxy_factory_free_proxies; px_proxy_factory_free; local: *; };\n' > "$ROOT/build/libproxy.map"
    cc -shared -fPIC -O2 -Wl,-soname,libproxy.so.1 -Wl,--version-script="$ROOT/build/libproxy.map" \
        -o "$APPDIR/usr/lib/libproxy.so.1" "$ROOT/packaging/libproxy-stub.c"
    printf '' | cc -shared -fPIC -x c -o "$APPDIR/usr/lib/libpxbackend-1.0.so" -Wl,-soname,libpxbackend-1.0.so -
fi
needed="$(mktemp)"
find "$APPDIR/usr/bin" "$APPDIR/usr/plugins" -type f | while read -r f; do
    LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$f" 2>/dev/null | awk '{print $3}' | grep "^$APPDIR/usr/lib/" || true
done | sort -u > "$needed"
for lib in "$APPDIR"/usr/lib/*.so*; do
    grep -qxF "$lib" "$needed" || rm -f "$lib"
done
rm -f "$needed"

"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --output appimage
# linuxdeploy names the file after the desktop entry; give it a fixed ASCII name.
for f in ./*.AppImage; do
    [ "$f" = "./$OUT" ] || mv "$f" "./$OUT"
done
ls -la "$ROOT/build/$OUT"
