# Changelog

## 0.29.4
- The category used to fill before a fixed point can now be set **for each clock** (Scheduler → Clock → *Fill before
  fixed points from*). Before, *Fill from* in Rules was one setting for all clocks, so choosing "12 Inch" for a 12 Inch
  hour also filled the Pop hours with 12 Inch tracks. A clock can use the general setting (default), the category
  of the music before the fixed point, the clock's own *Fill to the next fixed point* category, or any category. The
  clock that counts is the one of the hour in which the music before the fixed point plays (for the 13:00 news, the
  12 o'clock clock).
- The general *Fill from* setting has a new choice too: *the fill category of the hour's clock*.

## 0.29.3
- Scheduler: the **Rules** section can be collapsed and is collapsed by default; click its header to open it. This
  leaves more room for the clock and the selected element.

## 0.29.2
- Mixdown: a voice track can be **selected and deleted on its own** (click it on the timeline, then *Delete* or the
  Delete key); before, deleting removed the item under it too. The selected voice track has an orange frame.
- Mixdown: a new voice track (recorded, from a file or from the library) goes **where the selection is**: to the point
  where you clicked a clip on the timeline (the playhead moves there), or to the start of the item selected in the
  list. With a voice track selected, the new one replaces it in the same place.
- Mixdown: clicking a clip on the timeline now selects that clip (before, the one before it), so *Delete* removes the
  clip you clicked.

## 0.29.1
- Mixdown: **volume curve** for every clip (*Volume curve* button): click a clip for a new point, drag points (up and
  down: level from 0 to −40 dB, sideways: time), right-click or double-click deletes. It starts from the item's own
  curve but belongs to the mixdown only (saved in the mixdown project, the item is not changed). In a test, a −20 dB
  section drawn on a clip measured 20.1 dB quieter in the rendered file.
- Mixdown: items can be **dragged straight onto the timeline**, to the lane and the moment where they are dropped
  (an orange marker shows where). The items after it keep their times when the dropped one fits before them, so a
  jingle can be layered over the music on the other lane. Clips can also be moved to the other lane by dragging them
  up or down. Items dropped on the voice-track lane become the voice track of the nearest transition.
- A transition's mix point may now be later than the end of the item (the next one starts after a pause, while
  something else can play on the other lane).
- The volume curve between two points now changes evenly in dB in the mixdown too, as in playback.

## 0.29.0
- New **Mixdown** page (tab after the Playlist editor): put a sequence of items together, add voice tracks over the
  transitions, and render it into one audio file, sample-accurately.
  - Drag items in from the page's own library list or from the main window's library; reorder with drag or ↑ ↓.
  - The transitions are the mixdown's own, completely independent of the Mix editor and the playlist: when items
    are added they are set by the smart mix (fade-out and sudden endings, soft and strong intros, tempo and key),
    then they can be changed freely – drag a clip on the timeline (mix point), its edges (start and end point), or
    use the fields under it (mix point, fade-out, end, next start, voice-track start, music under the voice track).
    *Back to automatic* restores the calculated values.
  - Voice tracks: record (the silence at both ends is trimmed), from a file, or from the library's voice-track
    category; the music is ducked under them with 0.3 s ramps.
  - Preview from the red line (90 s) or listen to the selected transition on the preview output.
  - Save and reopen mixdown projects.
  - *Create mixdown…*: MP3 (128–320 kbps) or WAV (16/24 bit), 48 or 44.1 kHz, optional peak limit at −1 dBFS
    (two passes: only turns the whole mix down if needed), and adding it to the library's "Mixdown" category
    (created if missing). Rendering runs much faster than real time, the broadcast is not affected. In a test, a
    2.5-minute mix with five items and a voice track took 0.3 s as WAV and 3.3 s as MP3 (192 kbps, with the peak limit, i.e. two passes).

## 0.28.0
- Mini mixer in the clock panel of the main window (it replaces the old "Players" level meter): channels for the three
  players (A, B, C), the voice track, the cartwall and the main output (OUT). Each has a stereo level meter after the
  fader with a 2-second peak hold and an overload indicator (click to clear), a fader from −∞ to +6 dB (catches at
  0 dB; wheel ±1 dB, Shift+wheel ±0.1 dB; double-click: 0 dB) and a mute button. Right-click: 0 dB, mute, reset all.
- The faders act above the mix and the volume curve: transitions sound the same, only the whole channel is louder or
  quieter. The main output fader acts on everything that goes on air (overlaid items included). Fader positions are
  kept after a restart; mutes are not, so the station never starts silent. Muting the player on air or the main
  output asks first.
- The time left until the end of the hour moved next to the date.

## 0.27.0
- Vocal attribute for every item: male, female, group, mixed (duet) or instrumental. Set it in the item editor (next
  to Dayparts) or for many tracks at once from the library's right-click menu (*Set vocal ▸*); the library has a
  sortable, coloured Vocal column.
- Scheduler rule *Same vocal in a row: at most N* (Scheduler → Rules), one number for every vocal type. Unknown and
  instrumental tracks do not count and do not break a run. It works like the other music-flow rules: relaxed only when
  no track fits, followed by the fill to a fixed point, and reported after generating ("vocal: 84 / 84 tracks within
  the rule"). In a 6-hour test with half the library female, the longest run of the same vocal went from 5 to 2.
- The playlist shows the vocal as a letter after tempo and key (Hungarian F N E V I, English M F G X I), and marks a
  track that makes the run too long with ⚠ ("3 female vocals in a row (at most 2)").
- The fill to a fixed point now also orders its last tracks by the slow-songs rule.
- The Scheduler and the Mix editor open in their own windows only when the screen resolution is below 1920×1080
  (physical pixels, regardless of scaling); on a larger screen they are pages in the main window again.
- Database schema 14 (upgraded automatically).

## 0.26.2
- Fixed: the automatic fill before a fixed point stopped after three changes per fixed point, so after deleting the
  filled track by hand a few times the gap stayed empty. The limit is now a safety cap of 20, and when the tracks
  before the fixed point change (deleted, added, moved, or the next one starts), it checks again 3 seconds later
  instead of waiting up to two minutes.
- A track removed from the playlist by hand is not put back by the automatic fill for two hours; it picks another one.

## 0.26.1
- New switch in Scheduler → Rules: *Fill a gap with one song*, with *may run over by at most* (default 20 s). In Auto
  mode, when there would be silence before a fixed-time item, one track from the fill category is added that covers the
  gap; it may run over by up to the set limit and is faded out at the fixed time. If no track is within the limit, the
  one with the smallest overrun is used (still better than silence); if every track is shorter than the gap, the longest
  one goes in and the rest is filled a minute later. It works on its own or together with the fixed-point adjustment.
- Fixed: the automatic fixed-point adjustment gave up after three unsuccessful attempts. When nothing could be done
  for a while (for example an overrun with *automatic fill*, which can only add tracks), it no longer adjusted later
  when the overrun had turned into a gap. Now only adjustments actually made count (at most three per fixed point); a
  check that finds nothing to do is simply repeated two minutes later.

## 0.26.0
- Smart automatic mix. Between two songs in automatic mix mode the program now looks at the end of the outgoing track
  and the start of the next one instead of using a single level threshold:
  - a **fade-out ending** is recognised, and the next track starts once the outgoing one has faded by a set amount
    (default 10 dB); a **sudden ending** gets a tight join (default 0.15 s overlap);
  - a **soft, rising intro** starts earlier, under the end of the outgoing track (by up to 6 s by default); a **strong
    start** comes in at the end;
  - with **tempo and key** taken into account, tracks whose tempos differ (more than 6 % by default; half and double
    tempo count as equal) or whose keys clash on the Camelot wheel get a shorter overlap (2 s by default), with the
    outgoing track fading throughout it;
  - the leading silence of the incoming track is skipped, and the longest overlap is capped (8 s by default).
- All of this is adjustable on the new *Settings → Automatic mix* tab, together with the simple-mode threshold and the
  default fade-out (which had no setting on the interface before).
- In automatic mix mode the Mix editor shows the smart plan on the timeline for music-to-music transitions, and the
  banner explains it (for example "fade-out ending (4.7 s) · soft intro (3.8 s) → the next starts 3.8 s before the
  audible end"). The remote editor receives the same settings.

## 0.25.0
- The Scheduler and the Mix editor open in their own windows instead of replacing the on-air view, so the playlist and
  players stay visible while you work on them (they can also go on a second screen). Clicking the tab again brings the
  window to the front. Each window remembers its position and size, and windows left open are reopened at the next
  start. Closing the Mix editor window stops its preview. Space (Next) only works in the main window, so it cannot
  start the next item by accident while you edit.

## 0.24.3
- Fixed: the on-time adjustment (and the *Adjust…* dialog) found no track to fill a gap when the playlist was generated
  far ahead, because it skipped every track that appeared anywhere in the playlist – with a small library (for example
  100 songs and 24 hours generated ahead) that is all of them. Now a track is only skipped if it plays within the
  track-repeat time of the fixed point (in the play log or elsewhere in the playlist); if that leaves too few
  candidates, the repeat time is shortened step by step (to half, then to 30 minutes). Tracks playing farthest from the
  fixed point are preferred.

## 0.24.2
- The playlist shows the tempo and key of every music row (for example "124 · 8A", at the end of the artist line), and
  marks a row with ⚠ in amber when its transition from the previous track breaks the tempo or key rule; the tooltip
  says why (for example "tempo jump 14 % (118 → 135 BPM, the rule allows 8 %)" or "key does not match: 8A → 3B").
- After *Generate playlists* the scheduler reports how well the rules were kept in the generated hours: how many
  transitions are within the tempo rule and how many keys match, and in how many places a rule had to be relaxed, and
  which one (music flow, energy band, repetition, daypart). Previously this was a single number labelled as the
  repetition rule.
- The fill to a fixed point now orders its last 1–3 tracks by every music-flow rule (tempo, energy jump, key), not only
  by key, and counts it as a relaxation when the exact fill needs to break one.

## 0.24.1
- Smarter on-time adjustment before fixed-time items (Scheduler → Rules → Fixed points: *automatic fill, trim and
  swap*). Previously it could only add items or drop one whole track, which with long tracks (6–8 minute 12-inch mixes)
  often left minutes of overrun or a gap of silence. Now it looks at every combination of dropping up to two
  not-yet-started tracks placed by the scheduler and adding up to three tracks from the library, and picks the one that
  reaches the fixed point most accurately – a few seconds over (faded out at the fixed time, within "Fill accuracy") is
  preferred to silence. A track already loaded in a player can be swapped too, if it starts at least 90 seconds later.
  Examples from the test: 2:47 over → 0:04 over (one track swapped for a shorter one); 8 s short (silence) → 0:05 over;
  4 minutes short → 0:05 over.
- It now starts up to 30 minutes before the fixed point (was 20), and checks again (at most three times, two minutes
  apart) if the playlist changes in the meantime. Items added by hand are never removed.
- The *Adjust…* dialog shows the same combined plan (− dropped / + added) with the result, and can offer another one.

## 0.24.0
- Musical key for every item, with a Key column in the library (sortable along the Camelot wheel, with the wheel's
  colour). Notation: Camelot (8A, 5B …) by default, or traditional (Am, C# …) – Settings → Analysis.
- Keys and tempos are read from the file's tags on import (ID3 TKEY/TBPM, Vorbis/FLAC INITIALKEY/KEY/BPM; Camelot,
  Open Key and traditional forms are all understood).
- Key estimation from the audio (chroma analysis against Krumhansl–Kessler key profiles): in the item editor
  (Estimate button next to the key) and for a selection in the library's context menu. On a synthetic test set of all
  24 keys every key was found; with real music the relative major/minor can sometimes be confused.
- Background analysis (Settings → Analysis): goes through the music in the library at the lowest priority (idle thread
  and disk priority, a pause between tracks) and fills in missing tempos and keys. It runs continuously or only in a
  daily time window, picks up new imports by itself, and never overwrites values entered by hand or taken from tags.
- Scheduler key rule (Scheduler → Rules → Key): *prefer matching* or *matching only*, following the Camelot wheel
  (same key, neighbouring number, or the relative major/minor). The fill to a fixed point also orders its last tracks
  for the best key flow. In a 6-hour test with random keys, non-matching transitions went from 73 of 85 (no rule) to
  5 of 79 (prefer) and 1–4 of 80 (matching only).
- Database schema 13 (upgraded automatically).

## 0.23.0
- Smarter fill to the next fixed point: the scheduler now chooses the last 1–3 items of a fill together, so that they
  end as close as possible to the fixed point (within the "Fill accuracy" setting, ±15 s by default). Previously it
  added items until it passed the fixed point, which with long tracks (for example 6–8 minute 12-inch mixes) could
  leave several minutes to be faded out. In a test with 400 tracks of 5–8.5 minutes, every one of the 48 fixed points
  in 24 hours was reached within ±15 s (average 7 s).
- Clock rows with a count can be set to "at most as many as fit": the count is then an upper limit, and the last items
  are chosen to fit the time left before the next fixed point (or the end of the hour).
- The clock editor warns when rows with a fixed count do not fit between two fixed points (the rows turn red, with the
  estimated and available time), and offers a one-click fix: music rows become "Fill to the next fixed point", other
  rows "at most as many as fit".
- Database schema 12 (upgraded automatically).

## 0.22.2
- Fixed a random crash ("free(): invalid pointer") while reading audio files, most often right after putting a newly
  imported item in the playlist (when its waveform was being read). The decoder started its channel layout from
  uninitialised memory; depending on what was left there, FFmpeg tried to free an invalid pointer. Present since the
  first versions, on Linux and Windows alike.

## 0.22.1
- Crash report: if Éter crashes, the call stack is written to the terminal and appended to `crash.log` in the
  program's data folder (Linux: `~/.local/share/Eter/Eter/crash.log`, Windows: `%APPDATA%\Eter\Eter\crash.log`),
  so the cause can be found. On Linux the function names are included.

## 0.22.0
- Volume curve for items: in the Mix editor (Volume curve button) and in the item editor, points can be put on the
  waveform and dragged – up and down for the level (0 … −40 dB), sideways for the time; right-click or double-click
  deletes a point. Between points the level changes evenly in dB. Example: a news jingle stays at full level for its
  first seconds, then its bed continues 12 dB lower under the newsreader.
  - The curve belongs to the item, so it applies wherever the item plays: on the players, as an overlay, as a voice
    track, in preview and in the Mix editor's preview (where changes are heard immediately). It is independent of the
    transition settings and of Automatic mix: saving only a curve does not turn an automatic transition into a custom one.
  - Remote editors can edit it too (with the mix permission).
  - Database schema 11 (upgraded automatically).

## 0.21.0
- Settings → Database: export and import the SQLite database (library, categories, playlists, scheduler, saved mixes,
  cartwall, settings; not the audio files).
  - Export makes a consistent copy with SQLite `VACUUM INTO`, so it also works while on air.
  - Import checks that the file is an intact Éter database (SQLite header, quick check, tables, schema version), shows
    what is in it, backs up the current database to the `biztonsagi-mentesek` folder next to it, then restarts Éter; the
    imported file takes the old one's place at startup. A database from an older version is upgraded as usual; one from
    a newer version is refused. Import is refused while something is playing.
  - The restarted instance waits for the previous one to exit (the single-instance lock).
- The players show the title and artist from the library (as the playlist does), not the file's own tags.
- The mix point label on the waveform moves to the right of the mark when it would not fit on the left.
- README: updated for three players, automatic mix, now playing and database export/import; new On air screenshot.

## 0.20.0
- Now playing (Settings → Now playing): the title of the item on air is written to a text file at every change, for
  stream encoders (butt, Liquidsoap, Mixxx, OBS…), websites or on-screen captions. The file is replaced in one step,
  so readers never see a half-written line; if a reader locks it (Windows), it is overwritten in place.
  - Template with `%artist%`, `%title%`, `%category%`, `%duration%`, `%next_artist%`, `%next_title%`, `%time%` and `\n`;
    a separator next to an empty field is left out (a jingle with no artist is written as just its title).
  - Non-music items (jingles, news, ads, voice tracks): keep the previous song's title (default), show their own
    title, or a fixed text such as the station name.
  - Text when nothing has played for 3 seconds (default: empty file); it is also written when Éter is closed. The file
    is not touched until the first item plays, so a backup machine does not overwrite it.
  - Optional UTF-8 BOM for programs that need it.
  - Optional direct Icecast update: Éter sends the title to the server (admin/metadata, mode=updinfo, UTF-8) with the
    admin user or "source" and the mount password. The status line shows the last update or the error.

## 0.19.0
- Three players: A, B and C. The main window shows one big player, always the one on air (otherwise the one on
  standby), with its letter and output. A strip under it shows all three players (ON AIR / FADING / PLAYING /
  STANDBY / LOADED, title, time, output) and a pink VT tile while a voice track plays; clicking a tile shows that
  player for a while. Next shows which player starts next. The playlist rows say "ON AIR · B", "FADING · A",
  "STANDBY · C"; the playlist buttons are →A →B →C. The players take turns A → B → C.
- With three players a song, a jingle and the next song can sound at the same time: when the next song starts under a
  jingle while the previous song is still fading, each plays on its own player (before, the third item could not
  start until one of the two players was free). A non-music item that plays on under the next one shows "PLAYING".
  This works in automatic and in assisted mode.
- Automatic mix / Mix editor switch in the top bar. *Automatic mix*: transitions between two songs are mixed from the
  waveform (mix point where the song falls below the threshold, silence at the start of the next song is skipped); the
  saved Mix editor timing of those transitions is not used, but their voice track is. Transitions where either side
  is not music keep their saved settings. In the playlist such saved MIX marks are grey and struck through, the
  footer counts them, the waveform shows "AUTO MIX → B" (or "SAVED MIX → B") at the mix point, and the Mix editor
  shows a notice. *Mix editor*: every transition follows its saved settings, as before. Remote editors see the mode.
- The automatic mix point of quietly mastered songs (peak below the threshold) is now measured relative to the song's
  own peak, so it no longer falls at the very start of the song.
- Settings → outputs: player C gets its own output (until set, it follows player B's). Voice tracks and overlaid items
  play on player A's output.
- The play log can now hold player 3 (database schema 10; upgraded automatically).

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
