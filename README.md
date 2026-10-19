# Éter

Radio playout and automation for Linux and Windows: two players, a cartwall, a music library,
a scheduler, a mix editor, remote playlist editing, a program logger and a backup playout in one desktop application.

*Magyar leírás: [README.hu.md](README.hu.md)*

![The On air page: two players, the playlist with an expanded transition, and the library](docs/screenshots/on-air.png)

## Features

### On air

- **Two players** with waveform, intro countdown and automatic or assisted crossfading. In the last
  10 seconds of the item on air a red countdown appears at the bottom of the window (in automatic mode it counts
  to the mix point); it never takes clicks.
  Every channel (player 1, player 2, preview, cartwall) can go to its own sound card.
- **Playlist** with planned air times and hour separators. Each row shows the start time, a type icon,
  the length, title and artist, and marks for custom mixes (MIX), voice tracks (VT) and fixed times (FIX).
  Non-music items are filled with their category colour; the item on air and the one on standby stand out,
  with a progress bar on the item on air.
- **The playlist follows the item on air** unless you scroll away; scrolling back turns following on again.
- **Mini mix editor in the playlist**: the arrow at the end of a row opens the transition to the next item.
  Drag the next item onto the end of the outgoing one, listen to the transition, reset it, or open it in the
  full Mix editor.
- **Overlay**: an item can be laid over the transition between the items before and after it, so all three
  play at once (for example a jingle over a crossfade). It plays on its own channel; right-click a waiting
  item → "Overlay on the transition", or drag in the Mix editor (see below).
- **Cartwall** in place of the playlist: coloured walls of instant-start buttons, filled from the library or
  from files, with its own output (by default it follows player 1).
- **On time**: before every fixed-time item (the news at the top of the hour, say) the playlist shows how much is
  missing or over, suggests items to fill the gap or to take out, and in automatic mode can do it by itself.
- **Missing files** are found in the background: rows whose file cannot be reached turn red well before they would go on
  air, and the whole library can be checked as well.
- **Program logger**: what goes on air is recorded into hourly MP3 files, old ones are deleted after a set time.
- **Play log** with CSV export.

### Preparation

- **Library** in SQLite with user-defined categories, import of files and whole folders, and an item editor
  for title, artist, album, year, category, cue-in, intro and outro markers.
- **Scheduler**: clocks built from categories, fixed-time elements and fills, a week grid,
  artist and title separation, automatic generation ahead of time. Items can have an energy (1–5), a tempo (estimated
  from the audio or tapped in) and dayparts; rules keep slow songs apart, limit energy jumps and tempo changes, and a
  clock slot can ask for an energy band.
- **Mix editor**: set the transition between two or three consecutive items on a timeline: mix point,
  end of the outgoing item, fade curves, cue-in, fade-in, intro, voice tracks (recorded or from file) and
  music ducking, with preview. In the three-item view, drag the third item in front of the second one and
  release: the middle item is overlaid on the transition, with its own lane. Click anywhere in a lane to listen from
  that point. Items that are not music are not faded out by default: they play to their end under the next item.
- **Playlist editor**: build playlists by hand, save them into categories and insert them into
  the main playlist as collapsible blocks.
- **Timed events**: send a library item or a saved playlist into the playlist (as next or at the end),
  or start it on air, once on a date or weekly on chosen days.

### Remote editing

![The remote editor: the live playlist of the playout computer and the Mix editor](docs/screenshots/remote-editor.png)

The already generated playlist of a playout computer can be edited from another computer on the network:
order, delete, overlay, and the full Mix editor with preview. Every change is carried out by the playout at
once, in its own database and on air; every connected editor sees the live state.

1. **On the playout computer**: Tools → Settings → Remote editing. Tick *Enabled*, choose a port
   (default 7341) and a password (at least 6 characters), then *Apply*. The computer's IP addresses are shown
   below. Allow the port in the firewall.
2. **On the editing computer**: File → Remote editor (or start Éter with `--editor`; on Windows run
   `Eter-szerkeszto.cmd`). Enter the playout's IP address, the port and the password. The audio files are read
   from a shared network folder: give the folder as the playout sees it (for example `D:\Music`) and as this
   computer sees it (for example `\\radio\Music`, or on Linux `smb://radio/Music` once the share has been opened in the
   file manager, or a mounted folder such as `/mnt/music`), or leave both empty if the paths are the same.

The playout's library is shown in the editor too: drag or double-click items into the playlist. Audio files can be
uploaded from the editing computer (button, or drop them onto the library or the playlist), and voice tracks can be
recorded or loaded from file in the editor's Mix editor; they are uploaded to the playout.

Besides the main password, users with their own password and rights (order, delete, insert, mix and overlay, upload,
backup playout) can be added under Settings → Network; they log in with their name.

The editor does not play anything on air and does not touch its own database. The password never travels
over the network (challenge–response with HMAC-SHA256), and the playout stores only a key derived from it.
The editor reconnects by itself if the connection drops or the playout restarts.

### Backup playout

A second computer can stand by as a backup: File → Restart as backup playout (or start Éter with `--backup`; on Windows
`Eter-tartalek.cmd`), then enter the main playout's address, a user with the *backup playout* right (or the main
password) and, if needed, where the shared audio folder is on both computers. The backup keeps a copy of the main
computer's whole database and follows what is on air. If the main computer stops answering while something is playing,
the backup takes over after the set time (default 10 seconds), or only when *Take over now* is pressed, as set. It
continues the item that was playing. When the main computer is back, *Hand back to the main playout* returns the
programme there at the same point. A normal shutdown of the main computer never triggers a takeover.

Both computers need to reach the audio files (the same shared folder). If the network between them fails but both
keep running, both could be on air: the backup is meant for a main computer that crashes or loses power.

### Other

- English and Hungarian interface (Settings → Language).
- Only one copy can run with the same database: a second copy would play the same playlist at the same time.

| Mix editor with an overlaid item | Scheduler |
|---|---|
| ![Mix editor](docs/screenshots/mix-editor.png) | ![Scheduler](docs/screenshots/scheduler.png) |

![Item editor](docs/screenshots/item-editor.png)

## Install

### Linux

Download the latest `Eter-…-x86_64.AppImage` from the Releases page, then:

    chmod +x Eter-*-x86_64.AppImage
    ./Eter-*-x86_64.AppImage

It needs a 64-bit Linux with glibc 2.39 or newer (Ubuntu 24.04 and later, and comparable
distributions) and PipeWire, PulseAudio, ALSA or JACK for sound. The library is stored in
`~/.local/share/eter/library.sqlite`, the settings in `~/.config/Eter/Eter.conf`.

### Windows

Download `Eter-…-win64.zip` from the Releases page, unpack it anywhere and start `eter.exe`
(64-bit Windows 10 or 11); `Eter-szerkeszto.cmd` starts the remote editor, `Eter-tartalek.cmd` the backup playout. The program is not code-signed,
so Windows may show an “unknown publisher” warning on first start. Sound goes through WASAPI. The library is
stored in `%LOCALAPPDATA%\eter\library.sqlite`, the settings in the registry under
`HKEY_CURRENT_USER\Software\Eter`.

The Windows build has been tested far less than the Linux one.

### Both

Audio files are never copied or modified; the library only refers to them. Databases from older versions
are upgraded automatically on first start.

Useful options: `--editor` (remote editor), `--backup` (backup playout), `--backend NAME` (`pulseaudio`, `alsa`, `jack` on Linux;
`wasapi`, `dsound` on Windows), `--db FILE` (use another database), `--selftest FILE` (check the audio
engine with an audio file), `--help`.

## Build from source

### Linux

On Ubuntu 24.04 or newer:

    sudo apt install build-essential cmake ninja-build nasm git curl \
        qt6-base-dev libqt6sql6-sqlite qt6-wayland libgl1-mesa-dev libmp3lame-dev
    scripts/build_ffmpeg.sh      # small static FFmpeg with audio decoders only, into third_party/ffmpeg
    cmake -S . -B build/release -G Ninja
    cmake --build build/release
    build/release/eter

To package the AppImage (downloads linuxdeploy on first use):

    scripts/build_appimage.sh    # result: build/Eter-<version>-x86_64.AppImage

### Windows

In an [MSYS2](https://www.msys2.org/) **UCRT64** shell:

    pacman -S --needed git make diffutils zip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
        mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-qt6-base \
        mingw-w64-ucrt-x86_64-lame
    scripts/build_ffmpeg.sh
    scripts/build_windows.sh     # result: build/Eter-<version>-win64.zip

From VS Code: install MSYS2 to its default place (`C:\msys64`), open the project folder, then
Terminal → Run Task… and run the tasks named “Éter: 1 … 4” in order (the first two only once).
Afterwards Ctrl+Shift+B rebuilds the package. Keep the project in a path without spaces or
accented letters, for example `C:\eter`.

### Releases

Pushing a tag such as `v0.15.0` makes the GitHub workflow in `.github/workflows/build.yml`
build the AppImage and the Windows zip and attach them to a release.

## Source layout

- `src/audio` – decoder (FFmpeg → 48 kHz stereo), sources (decode thread and ring buffer),
  engine (miniaudio: outputs, routing, mixing, recording)
- `src/ui` – main window, players, library, scheduler, mix editor and mini mix editor, playlist editor,
  cartwall, timed events, remote editor
- `src/net` – remote editing protocol (JSON lines over TCP, HMAC login), server and client
- `src/db` – SQLite layer and migrations; `schema.sql` – the database schema
- `src/sched` – playlist generation
- `packaging` – desktop entry, Windows resources, the libproxy stand-in used in the AppImage
- `i18n/en.json` – English interface text (see [CONTRIBUTING.md](CONTRIBUTING.md))

Source comments are in Hungarian. The changes of each version are listed in [CHANGELOG.md](CHANGELOG.md).

## Credits

Designed and directed by laczfy001. The code was written with Claude, Anthropic's AI assistant.

## Licence

Éter is free software under the [GNU General Public License v3.0 or later](LICENSE).
Third-party components and their licences are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
