# Changelog

## 0.11.2
- Windows: fixed a crash at start when opening the WASAPI output.
- The Windows build script copes with source files dated in the future.

## 0.11.1
- VS Code build tasks for Windows (`.vscode/tasks.json`).

## 0.11.0
- Windows build (64-bit, WASAPI or DirectSound audio), packaged as a zip by `scripts/build_windows.sh` and the GitHub workflow.

## 0.10.1
- Repository prepared for publication: licence, English README, build workflow, third-party notices.
- The AppImage build script downloads its packaging tools and writes `Eter-<version>-x86_64.AppImage`.

## 0.10.0
- English interface (default) next to Hungarian; language selector in Settings.

## 0.9.0
- Item editor: title, artist, album, year, category, and cue-in / intro / outro markers on a waveform with preview.

## 0.8
- Cartwall in place of the playlist: coloured walls of instant-start buttons, filled from the library or from files.
- The cartwall output can be chosen in Settings.

## 0.7.0
- Playlist editor: hand-built playlists saved into playlist categories and inserted into the main playlist as collapsible blocks.

## 0.6
- Mix editor: side-by-side layout, three-item view, voice tracks with ducking, intro marker and countdown, loop preview.
- The end of the outgoing item is set independently of the mix point; zoom follows the mouse cursor.
- Rows of non-music items are filled with their category colour in the playlist.

## 0.5 and earlier
- Mix editor with per-transition and per-track settings.
- Scheduler: clocks, week grid, repeat rules, automatic generation.
- Library with categories, import, playlist, auto and assist modes, play log.
- Two players and a preview channel with freely selectable outputs.
