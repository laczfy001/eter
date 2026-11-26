#!/bin/sh
# Minimális, csak hangot dekódoló, statikus FFmpeg (LGPL) fordítása a third_party/ffmpeg alá.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${FFMPEG_SRC:-$ROOT/build/ffmpeg-src}"
[ -d "$SRC" ] || git clone --depth 1 --branch n7.1.1 https://github.com/FFmpeg/FFmpeg.git "$SRC"
cd "$SRC"
# Egy korábbi fordítás maradékai (más fordítóval készült objektumfájlok) ne kerüljenek az új könyvtárakba.
[ -f config.h ] && make distclean >/dev/null 2>&1 || true
rm -rf "$ROOT/third_party/ffmpeg"
./configure --prefix="$ROOT/third_party/ffmpeg" \
  --disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
  --enable-static --disable-shared --enable-pic \
  --enable-protocol=file \
  --enable-demuxer=mp3,flac,ogg,wav,aac,mov,matroska,aiff,asf,w64 \
  --enable-decoder=mp3,mp3float,mp2,flac,vorbis,opus,aac,aac_latm,alac,wmav2,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8,pcm_s16be,pcm_s24be \
  --enable-parser=mpegaudio,flac,vorbis,opus,aac,aac_latm
make -j"$(nproc)"
make install
# Melyik fordítóval készült: a build_windows.sh ebből látja, ha az MSYS2 frissítése óta újra kell fordítani.
sh "$ROOT/scripts/toolchain_id.sh" > "$ROOT/third_party/ffmpeg/toolchain.txt"
