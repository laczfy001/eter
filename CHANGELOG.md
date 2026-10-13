# Changelog

## 0.15.1
- Remote editor: the folder on the editing computer can be given as a file-manager network address
  (`smb://computer/share/folder`) or as `\\computer\share\folder`. On Linux it is translated to the folder where
  the file manager has mounted the share (GNOME/gvfs); on Windows to a network path. Before, such an address gave
  "Protocol not found" and there was no waveform or preview.
- Remote editor: if the audio files cannot be reached on the editing computer, a warning says so and what to do.

## 0.15.0
- Remote editing: a second Éter in editor mode (`--editor`, or File → Remote editor) connects to a playout
  computer by IP address and edits its generated playlist: reorder, delete, overlay, and the full Mix editor
  with preview. Every change is carried out by the playout at once, in its own database and on air, and every
  connected editor sees the live state (what is on air, what is next).
  - Turned on in the playout's Settings: port (default 7341) and password. The password never travels over the
    network (challenge–response with HMAC-SHA256), and the playout stores only a key derived from it.
  - The editor reads the audio files from a shared network folder; the folder on the playout computer and the
    same folder on the editing computer can be given when connecting.
  - The editor reconnects by itself if the connection drops or the playout restarts.

## 0.14.1
- Only one copy of Éter can run with the same database. A second copy would play the same playlist at the
  same time, slightly shifted, which sounds like an echo; it now shows a message and does not start.

## 0.14.0
- Overlay: a playlist item can be laid over the transition between the item before and after it, so all three
  can play at once. It plays on its own channel (on player 1's output), not on the two players.
  - Mix editor, three-item view: drag the third item in front of the second one and release.
  - Or right-click a waiting item in the playlist: "Overlay on the transition".
  - The overlay has its own lane in the Mix editor: drag it to set when it starts relative to the mix point,
    set how much the music is lowered under it, listen to the whole transition, or make it standalone again.
  - Works in Auto and Assist mode; in the playlist the overlaid item is indented and marked, and it is logged.
- Database schema 8 (existing databases are upgraded automatically).
- Fixed: after a playlist refresh the Mix editor could keep showing the previous view.

## 0.13.2
- Fixed: clearing the playlist (and other changes that shrink it) could read past the end of the list and crash.
- Fixed: a crash on exit while an item editor window was still open.
- Fixed: the waveform reader threads of the Mix editor and the mini editor were only cleaned up at exit, so they piled up during long continuous operation.
- Deleting library items, a category or a clock, and clearing the playlist now ask for confirmation.
- Mix editor: the long hint line no longer squeezes the transition list; titles and the column header fit.
- Smaller fixes: no duplicated category name in the item editor ("Music · Music"), clearer output count text in Settings.

## 0.13.1
- Fixed: the selected playlist row was filled with a solid colour and its text was not visible.
- Fixed: a crash when expanding a row's mini mix editor. The mini editor is now a single widget placed over the expanded row instead of a table cell widget.
- The selected row has a clearer frame.

## 0.13.0
- New main playlist look: taller rows filled with the category colour, type icons, large lengths, title and artist in two lines, MIX/VT/FIX marks, live progress on the item on air, hour separators.
- Each row can be expanded into a small mix editor: drag the next item onto the end of the outgoing one to set the mix point; listen, reset, or open the full Mix editor.

## 0.12.0
- Timed events: send a library item or a saved playlist to the playlist (as next or at the end) or start it on air, once on a date or weekly on chosen days.
- The main playlist follows the item on air, unless you scroll away from it; scrolling back to it turns following on again.

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
