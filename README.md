Designed and directed by laczfy001. The code was written with Claude, Anthropic's AI assistant.

# Éter

Radio playout and automation for Linux: two players, a cartwall, a music library, a scheduler
and a mix editor in one desktop application. It ships as a single AppImage.

*Magyar leírás: [README.hu.md](README.hu.md)*

![The On air page: two players, the playlist and the library](docs/screenshots/on-air.png)

## Features

- **Two players** with waveform, intro countdown and automatic or assisted crossfading;
  every channel (player 1, player 2, preview, cartwall) can go to its own sound card.
- **Library** in SQLite with user-defined categories, import of files and whole folders,
  and an item editor for title, artist, cue-in, intro and outro markers.
- **Playlist** with planned air times, drag-and-drop ordering, and a play log (CSV export).
- **Scheduler**: clocks built from categories, fixed-time elements and fills, a week grid,
  artist and title separation, automatic generation ahead of time.
- **Mix editor**: set the transition between two or three consecutive items on a timeline,
  with fade curves, voice tracks (recorded or from file) and music ducking.
- **Playlist editor**: build playlists by hand, save them into categories and insert them into
  the main playlist as collapsible blocks.
- **Cartwall**: coloured walls of instant-start buttons, filled from the library or from files.
- English and Hungarian interface (Settings → Language).

| Scheduler | Item editor |
|---|---|
| ![Scheduler](docs/screenshots/scheduler.png) | ![Item editor](docs/screenshots/item-editor.png) |

## Install

Download the latest `Eter-…-x86_64.AppImage` from the Releases page, then:

    chmod +x Eter-*-x86_64.AppImage
    ./Eter-*-x86_64.AppImage

It needs a 64-bit Linux with glibc 2.39 or newer (Ubuntu 24.04 and later, and comparable
distributions) and PipeWire, PulseAudio, ALSA or JACK for sound.

The library is stored in `~/.local/share/eter/library.sqlite`, the sound card choices in
`~/.config/Eter/Eter.conf`. Audio files are never copied or modified; the library only refers
to them.

Useful options: `--backend pulseaudio|alsa|jack`, `--db FILE` (use another database),
`--selftest FILE` (check the audio engine with an audio file), `--help`.

## Build from source

On Ubuntu 24.04 or newer:

    sudo apt install build-essential cmake ninja-build nasm git curl \
        qt6-base-dev libqt6sql6-sqlite qt6-wayland libgl1-mesa-dev
    scripts/build_ffmpeg.sh      # small static FFmpeg with audio decoders only, into third_party/ffmpeg
    cmake -S . -B build/release -G Ninja
    cmake --build build/release
    build/release/eter

To package the AppImage (downloads linuxdeploy on first use):

    scripts/build_appimage.sh    # result: build/Eter-<version>-x86_64.AppImage

Pushing a tag such as `v0.10.1` makes the GitHub workflow in `.github/workflows/build.yml`
build the AppImage and attach it to a release.

## Source layout

- `src/audio` – decoder (FFmpeg → 48 kHz stereo), sources (decode thread and ring buffer),
  engine (miniaudio: outputs, routing, mixing, recording)
- `src/ui` – main window, players, library, scheduler, mix editor, playlist editor, cartwall
- `src/db` – SQLite layer and migrations; `schema.sql` – the database schema
- `src/sched` – playlist generation
- `i18n/en.json` – English interface text (see [CONTRIBUTING.md](CONTRIBUTING.md))

Source comments are in Hungarian.

## Licence

Éter is free software under the [GNU General Public License v3.0 or later](LICENSE).
Third-party components and their licences are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
