# Third-party software

Éter itself is licensed under the GNU General Public License v3.0 or later (see `LICENSE`).
It uses the following third-party components.

| Component | Used for | Licence | How it is included |
|---|---|---|---|
| [Qt 6](https://www.qt.io/) (Widgets, Sql, Network) | user interface, SQLite and MySQL access, remote editing, website sync, live streams | LGPL-3.0 | linked dynamically; bundled in the AppImage (including Qt's OpenSSL TLS plugin) |
| [OpenSSL](https://www.openssl.org/) 3 | HTTPS for the website sync and live streams | Apache-2.0 | Linux: not bundled, the system's library is loaded at run time; Windows: Qt uses the system's Schannel, and the OpenSSL DLLs from MSYS2 may be next to `eter.exe` |
| [FFmpeg](https://ffmpeg.org/) 7.1 (libavformat, libavcodec, libswresample, libavutil) | audio decoding | LGPL-2.1-or-later | built from unmodified source by `scripts/build_ffmpeg.sh` with audio decoders only, linked statically |
| [LAME](https://lame.sourceforge.io/) 3.100 (libmp3lame) | MP3 encoding (program logger, Mixdown export) | LGPL-2.0-or-later | the unmodified static library from Ubuntu (`libmp3lame-dev`) or MSYS2 (`mingw-w64-ucrt-x86_64-lame`), linked statically |
| [miniaudio](https://miniaud.io/) | audio output and recording | MIT-0 or public domain | single header in `third_party/miniaudio.h` (licence text at the end of the file) |
| [SQLite](https://sqlite.org/) | database | public domain | through the Qt SQLite driver |
| MySQL / MariaDB client library | connection to an external MySQL / MariaDB server (Settings → SQL server) | AppImage: [MySQL client library](https://dev.mysql.com/) (`libmysqlclient`), GPL-2.0 with the Universal FOSS Exception; Windows: [MariaDB Connector/C](https://mariadb.com/kb/en/mariadb-connector-c/) (`libmariadb`), LGPL-2.1-or-later | through Qt's MySQL driver (`qsqlmysql`), both loaded dynamically, unmodified: in the AppImage from Ubuntu (`libqt6sql6-mysql`, `libmysqlclient21`), in the Windows zip from MSYS2 (`mingw-w64-ucrt-x86_64-libmariadbclient`) |
| [IBM Plex](https://github.com/IBM/plex) Sans and Mono | interface typeface | SIL Open Font License 1.1 | `resources/fonts/` (licence in `resources/fonts/OFL.txt`) |
| MinGW-w64 runtime and the libraries Qt depends on (ICU, zlib, zstd, PCRE2, HarfBuzz, FreeType, libpng and others), from [MSYS2](https://www.msys2.org/) | Windows package only | various free licences (GPL with runtime exception, Unicode, zlib, BSD, MIT, FTL …) | DLLs next to `eter.exe`; their licence texts are in the `licenses` folder of the zip |
| The libraries Qt depends on, from Ubuntu (ICU, GLib, zlib, zstd, Brotli, MIT Kerberos/GSSAPI, xcb, Wayland, FreeType, HarfBuzz and others) | AppImage only | various free licences (Unicode, LGPL, zlib, BSD, MIT …) | shared libraries inside the AppImage, unmodified |
| libproxy stand-in (`packaging/libproxy-stub.c`, part of Éter) | AppImage only: replaces libproxy, which Qt Network links only for system proxy lookup; Éter never uses a proxy | GPL-3.0-or-later (Éter) | built by `scripts/build_appimage.sh` |
| [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | AppImage packaging (build time only) | MIT | downloaded by `scripts/build_appimage.sh` |

## Online services (optional, off by default)

- [MusicBrainz](https://musicbrainz.org/) (*Settings → Artist info*): artist details and track album/year. Core data is
  CC0; the web service is free for non-commercial use. Éter keeps to one request a second.
- [Wikidata](https://www.wikidata.org/) and [Wikipedia](https://www.wikipedia.org/) (optional, through MusicBrainz):
  the article summary and its picture as the artist's biography and photo. Wikipedia text is CC BY-SA; the pictures
  carry their own licences on Wikimedia Commons. The *Biography* window names the source.
- [TheAudioDB](https://www.theaudiodb.com/) (*Settings → Artist info*): artist photos, biographies and details,
  fetched only when the feature is switched on. No code of theirs is included. The data and images belong to
  TheAudioDB and its contributors; use them under TheAudioDB's terms of use (the free API key is limited, and a
  commercial station should check the terms). Éter identifies itself to the service only by its name, version and
  project address.

## Parts of Éter distributed separately

- `wordpress/eter-radio` (the *Éter Radio* WordPress plugin, `eter-radio.zip`) and `web/eter-web` (the *Éter Web*
  module, `eter-web.zip`) are part of Éter, GPL-3.0-or-later, with the licence text in each package. They contain no
  third-party code; the plugin uses only the WordPress APIs (WordPress is GPL-2.0-or-later, compatible).
- Neither stores personal data of website visitors: a song request keeps only the track and the time, no IP address,
  name or cookie. (The web server's own access log is outside Éter.)

## FFmpeg, LAME and the LGPL

The AppImage and the Windows `eter.exe` contain a statically linked FFmpeg. The exact source is the upstream tag named in
`scripts/build_ffmpeg.sh`, built with the configure options listed there and without the GPL or
non-free parts. Because the complete source of Éter and its build scripts are public, anyone can
rebuild the program against a modified FFmpeg by following the build instructions in `README.md`.
The same holds for LAME (libmp3lame 3.100), which is linked statically for the program logger: its source is the
upstream LAME 3.100 release as packaged by Ubuntu and MSYS2, and the program can be relinked against a modified copy
by rebuilding it from source.
