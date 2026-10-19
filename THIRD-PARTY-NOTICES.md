# Third-party software

Éter itself is licensed under the GNU General Public License v3.0 or later (see `LICENSE`).
It uses the following third-party components.

| Component | Used for | Licence | How it is included |
|---|---|---|---|
| [Qt 6](https://www.qt.io/) (Widgets, Sql, Network) | user interface, SQLite access, remote editing connection | LGPL-3.0 | linked dynamically; bundled in the AppImage |
| [FFmpeg](https://ffmpeg.org/) 7.1 (libavformat, libavcodec, libswresample, libavutil) | audio decoding | LGPL-2.1-or-later | built from unmodified source by `scripts/build_ffmpeg.sh` with audio decoders only, linked statically |
| [LAME](https://lame.sourceforge.io/) 3.100 (libmp3lame) | MP3 encoding for the program logger | LGPL-2.0-or-later | the unmodified static library from Ubuntu (`libmp3lame-dev`) or MSYS2 (`mingw-w64-ucrt-x86_64-lame`), linked statically |
| [miniaudio](https://miniaud.io/) | audio output and recording | MIT-0 or public domain | single header in `third_party/miniaudio.h` (licence text at the end of the file) |
| [SQLite](https://sqlite.org/) | database | public domain | through the Qt SQLite driver |
| [IBM Plex](https://github.com/IBM/plex) Sans and Mono | interface typeface | SIL Open Font License 1.1 | `resources/fonts/` (licence in `resources/fonts/OFL.txt`) |
| MinGW-w64 runtime and the libraries Qt depends on (ICU, zlib, zstd, PCRE2, HarfBuzz, FreeType, libpng and others), from [MSYS2](https://www.msys2.org/) | Windows package only | various free licences (GPL with runtime exception, Unicode, zlib, BSD, MIT, FTL …) | DLLs next to `eter.exe`; their licence texts are in the `licenses` folder of the zip |
| The libraries Qt depends on, from Ubuntu (ICU, GLib, zlib, zstd, Brotli, MIT Kerberos/GSSAPI, xcb, Wayland, FreeType, HarfBuzz and others) | AppImage only | various free licences (Unicode, LGPL, zlib, BSD, MIT …) | shared libraries inside the AppImage, unmodified |
| libproxy stand-in (`packaging/libproxy-stub.c`, part of Éter) | AppImage only: replaces libproxy, which Qt Network links only for system proxy lookup; Éter never uses a proxy | GPL-3.0-or-later (Éter) | built by `scripts/build_appimage.sh` |
| [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | AppImage packaging (build time only) | MIT | downloaded by `scripts/build_appimage.sh` |

## FFmpeg, LAME and the LGPL

The AppImage and the Windows `eter.exe` contain a statically linked FFmpeg. The exact source is the upstream tag named in
`scripts/build_ffmpeg.sh`, built with the configure options listed there and without the GPL or
non-free parts. Because the complete source of Éter and its build scripts are public, anyone can
rebuild the program against a modified FFmpeg by following the build instructions in `README.md`.
The same holds for LAME (libmp3lame 3.100), which is linked statically for the program logger: its source is the
upstream LAME 3.100 release as packaged by Ubuntu and MSYS2, and the program can be relinked against a modified copy
by rebuilding it from source.
