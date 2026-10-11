# Third-party software

Éter itself is licensed under the GNU General Public License v3.0 or later (see `LICENSE`).
It uses the following third-party components.

| Component | Used for | Licence | How it is included |
|---|---|---|---|
| [Qt 6](https://www.qt.io/) (Widgets, Sql) | user interface, SQLite access | LGPL-3.0 | linked dynamically; bundled in the AppImage |
| [FFmpeg](https://ffmpeg.org/) 7.1 (libavformat, libavcodec, libswresample, libavutil) | audio decoding | LGPL-2.1-or-later | built from unmodified source by `scripts/build_ffmpeg.sh` with audio decoders only, linked statically |
| [miniaudio](https://miniaud.io/) | audio output and recording | MIT-0 or public domain | single header in `third_party/miniaudio.h` (licence text at the end of the file) |
| [SQLite](https://sqlite.org/) | database | public domain | through the Qt SQLite driver |
| [IBM Plex](https://github.com/IBM/plex) Sans and Mono | interface typeface | SIL Open Font License 1.1 | `resources/fonts/` (licence in `resources/fonts/OFL.txt`) |
| MinGW-w64 runtime and the libraries Qt depends on (ICU, zlib, zstd, PCRE2, HarfBuzz, FreeType, libpng and others), from [MSYS2](https://www.msys2.org/) | Windows package only | various free licences (GPL with runtime exception, Unicode, zlib, BSD, MIT, FTL …) | DLLs next to `eter.exe`; their licence texts are in the `licenses` folder of the zip |
| [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | AppImage packaging (build time only) | MIT | downloaded by `scripts/build_appimage.sh` |

## FFmpeg and the LGPL

The AppImage and the Windows `eter.exe` contain a statically linked FFmpeg. The exact source is the upstream tag named in
`scripts/build_ffmpeg.sh`, built with the configure options listed there and without the GPL or
non-free parts. Because the complete source of Éter and its build scripts are public, anyone can
rebuild the program against a modified FFmpeg by following the build instructions in `README.md`.
