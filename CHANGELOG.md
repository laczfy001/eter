# Changelog

## 0.18.0
- Scheduler: every item can have an energy (1 very calm … 5 very upbeat), a tempo (BPM) and dayparts (night, morning,
  daytime, afternoon, evening; the start hours can be set). New rules: at most N slow songs in a row, at most this big
  an energy jump, at most this big a tempo change between two songs (half and double tempo count as the same). A clock
  slot can ask for an energy band (for example 4–5 after the news). Items are only scheduled in their dayparts. When no
  item fits, the rules are relaxed one by one (flow, energy band, separation, daypart).
- The tempo can be estimated from the audio (item editor → Estimate, or for many items at once from the library's
  right-click menu), or tapped in by hand. Energy and dayparts can be set for many items at once in the library; the
  library shows Energy and BPM columns.
- Remote editor: the playout's library is shown under the playlist; items can be dragged or double-clicked into the
  playlist. Audio files can be uploaded from the editing computer into the playout's library (button, or drop files
  onto the library or the playlist), and voice tracks can be recorded or loaded from file in the editor's Mix editor:
  they are uploaded to the playout and used there.
- Remote editing users: besides the main password, users with their own password and rights (order, delete, insert,
  mix and overlay, upload, backup playout) can be added in Settings → Network. The playout checks every request; the
  editor greys out what the user may not do. The protocol changed: update Éter on every computer.
- Backup playout: a second computer (File → Restart as backup playout, or `--backup`; on Windows
  `Eter-tartalek.cmd`) mirrors the main playout: it receives a copy of the whole database (every 10 minutes, and half a
  minute after changes) and the live state every few seconds. If the main computer disappears while something is on
  air, the backup takes over automatically after the set time (or only by button, as set), continuing the item that was
  playing from where it would be. When the main computer is back, *Hand back* returns the programme to it at exactly
  the same point, and the backup goes back to standby. A normal shutdown of the main computer does not trigger a
  takeover.

## 0.17.0
- Missing files: the files of the waiting playlist items (and their voice tracks) are checked in the background after
  every change and once a minute. An item whose file cannot be reached turns red ("MISSING FILE"), a button under the
  playlist counts them and opens a list (check again, remove selected or all), and a warning comes 30 minutes before
  such an item would go on air. Library → Find missing files in the library checks every item; those are left out by
  the scheduler until they turn up, and can be deleted from the library. Remote editors see the red rows too.
- On time: before every fixed-time item (such as the news at the top of the hour) the playlist shows how much is
  missing or over (a "−1:12" / "+0:42" mark on the row, and a bar above the playlist). *Adjust…* suggests items to fill
  the gap (from a chosen category, best fit first) or items to take out. In automatic mode it can also do this by
  itself 20 minutes before the fixed point (Scheduler → Fixed points: by hand / fill / fill and shorten).
- Program logger: what goes on air (players, voice tracks, overlaid items, cartwall, without the preview) is recorded
  into a new MP3 file every hour, split exactly on the hour; files older than the keep time (default 30 days) are
  deleted. Settings → Logger: folder, quality, keep time. A red "● Recording" mark shows it in the status bar.
- Settings are now on tabs: General and audio, Logger, Network.
- Building from source needs LAME (`libmp3lame-dev` on Ubuntu, `mingw-w64-ucrt-x86_64-lame` in MSYS2); the Windows build
  script installs it by itself if it is missing.

## 0.16.1
- Mix editor: clicking anywhere in a lane starts the preview from that point (if it is already playing, it jumps
  there). Whatever is audible at that moment starts from the right place, including voice tracks and overlaid items.
  The Preview button works as before. Dragging still edits; a click that moves only a few pixels counts as a click.
- Items that are not music (jingles, news, adverts…) are no longer faded out at the mix point by default: they play to
  their end while the next item starts under them. A fade set by hand in the Mix editor still applies, and pressing
  Next early still fades them out.
- Remote editor: connecting with an empty password field shows a warning and does not try to connect. A wrong password
  now shows "Wrong password" instead of a generic "connection closed" message. Closing the editor window always quits
  the program.

## 0.16.0
- On air: in the last 10 seconds of the item on air (player 1 or 2) a red countdown appears at the bottom of the
  window, counting down from 10. In automatic mode it counts to the mix point, so it reaches zero when the next item
  starts. The countdown cannot be clicked: it lets every click through to whatever is under it.

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
