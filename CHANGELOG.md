# Changelog

## 0.37.0
- **Stream encoders** (new *Stream* tab next to Mixdown; *Tools → Stream encoder…*, Ctrl+Shift+S): Éter sends the
  broadcast to Icecast 2 and Shoutcast v1 / v2 servers by itself, no separate encoder program needed.
  - Formats: MP3 (LAME, 32–320 kbit/s, constant or average bitrate) and Ogg Vorbis (48–320 kbit/s nominal, Icecast
    only); sample rate 22.05 / 32 / 44.1 / 48 kHz, stereo or mono, per encoder.
  - Several encoders at once, each started and stopped on its own (also *Start all* / *Stop all*, and *Start when Éter
    starts*). Each card shows the state, the listeners, the time on air and the data sent.
  - Source: the main output (what the listeners hear) or any sound card input – for example the output of a mixing
    desk with the microphones on it. Level to the stream, level meter and a peak limiter before encoding.
  - The title on air goes to the server (MP3: through the server's admin interface; Ogg: as a new header in the
    stream), from the Now playing template and rules or from the encoder's own template. Station name, description,
    genre, website and the public listing flag are sent to the server.
  - Dropped connections are retried after a set time; a wrong password (401) or a mountpoint in use (403) stops with a
    clear message instead of retrying. *Test connection* checks the login without sending audio. Old Icecast servers
    are reached in SOURCE mode automatically. Every event is in the encoder's log, errors also in the error log.
  - Outgoing bandwidth: the bitrates of the running encoders add up, with the data per hour; given an upload limit,
    a bar shows how much of it is used and warns when the encoders together would exceed it.
  - A STREAM badge in the tab row (green: all on air, orange: reconnecting, red: error) is visible on every tab and
    leads to the Stream tab.
  - Builds now need libvorbis (Ubuntu: `libvorbis-dev`; MSYS2: `mingw-w64-ucrt-x86_64-libvorbis`, installed by
    `build_windows.sh` when missing). Licence: BSD-3-Clause, see THIRD-PARTY-NOTICES.md.

## 0.36.1
- **Project website** on GitHub Pages ([laczfy001.github.io/eter](https://laczfy001.github.io/eter/)): an English
  summary of the features with screenshots, the changes of the latest version, and download buttons that always
  point at the files of the latest release. Built from `site/` by a GitHub workflow after every push.
- **Windows build after an MSYS2 update**: the static FFmpeg built with an older GCC / binutils no longer linked
  ("access beyond end of merged section"). `build_windows.sh` now records which compiler built FFmpeg and rebuilds it
  automatically when the compiler changes (also on the first build).
- **Several Éter installations on one website** (WordPress plugin 1.2.3): if two computers sync with the same site
  (for example a test machine left connected), the song requests were shared out between them at random and the
  "Now playing" of the idle one overwrote the one on air. Now every Éter sends its computer name and a fixed id, and
  only one of them – the one on air – updates the site and receives the requests. The other one shows
  "connected, but another Éter (name) updates the website" and logs a warning; the one on air shows that another
  Éter is also connected, and the plugin's settings page names both. If the on-air computer stops syncing, the next
  one takes over within the offline time. Older Éter versions count as one unnamed computer.
- **Error log in the program folder:** every error and warning goes into a daily text file, `logs/eter-YYYY-MM-DD.txt`
  next to `eter.exe` (Linux AppImage: `eter-logs/` next to the AppImage; if the folder is not writable, the user's
  data folder). *Help → Error log folder…* opens it. Logs older than 30 days are deleted.
  - *Unexpected exit:* the running program leaves a sign of life every 30 seconds; if the next start finds one from a
    process that no longer runs (crash, killed, power cut), it logs when that run started and when it was last alive.
  - *Crash:* the call stack goes into the same daily log (it used to go to `crash.log` in the data folder).
  - *Interface:* a C++ exception in event handling is logged and no longer closes the program; every warning and error
    dialog is logged with its text; Qt warnings (drawing, layouts, network, SSL, SQL driver) are logged too.
  - *Database* (SQL errors with the query), *audio* (output cannot be opened, missing output, audio system), *playback*
    (file cannot be loaded, dead air, overlay, voice track, timed events, exact time), *stream* (dropped connection,
    reconnect, given up, unsupported stream), *website* (errors and recovery), *SQL server* (connection lost,
    writer lock lost, recovered), *online data* (MusicBrainz, Wikipedia, TheAudioDB), *MIDI*, *remote connection*
    (editor, backup playout; takeover and handback), *logger recording*.
  - Each line has the time to the millisecond, the level (INFO / WARNING / ERROR) and the area; the same message is
    written at most once a minute, with the number of repeats. The log follows the interface language.
- **Website connection no longer "drops" every minute:** when the web host closes a kept-alive connection just as Éter
  sends the next sync, Éter now repeats the request at once on a new connection instead of reporting an error
  ("Connection closed") and waiting. The harmless Qt message "QIODevice::read (QSslSocket): device not open" is
  no longer logged.
- **Error log:** the "Exact time … no better solution" warning, whose figure changes every minute, is logged at most
  once every 15 minutes per fixed point.
- **Crash report: what the thread was doing.** When a decoding thread crashes, the report names the audio file it was
  working on ("ez a szál éppen: hangfájl dekódolása: …"), so a damaged file can be found and removed.
- **Crash report on Windows:** `crash.log` now also records the exception code, the module and offset where it
  happened (and for an access violation the address read or written), the thread, and the text of an uncaught C++
  exception.

## 0.36.0
- **Fixed-time items: the playlist bar says whether Éter adjusts by itself.** Automatic adjustment (filling a gap,
  shortening, swapping) is off unless switched on in *Scheduler → Adjust fixed points*, and it works only in Auto mode,
  from 30 minutes before the fixed point. The bar now shows "not adjusted automatically", "adjusts automatically from
  18:30", or that an overrun is not fixed by the current setting (with a tooltip on where to change it). The *Adjust…*
  window has a tick box to switch automatic adjustment on in one step.
- **Windows build script:** when pacman cannot install the MariaDB client (for example without an internet
  connection), the script no longer stops; Éter is built without the external SQL server and a warning says how to
  add it later.
- **MIDI control** (*Settings → MIDI*): the buttons and faders of a MIDI-capable mixer (such as the RØDECaster Pro) or
  controller can run Éter.
  - *Device:* every device the system knows as a MIDI input (Windows: through the system's MIDI service; Linux: the
    ALSA MIDI devices). If the device is switched off or unplugged, Éter reconnects by itself within a few seconds.
    A monitor line shows the last message received.
  - *Learn mode:* click *Learn* next to an action, then press the button or move the fader on the device. A control
    that already belonged to another action of the same kind moves over (and the page says from where).
  - *Buttons* (note, program change, or a CC that jumps above 64): *Next*, *Stop* (all players), start/stop and fade
    out for each player, cartwall buttons 1–16 and *stop all carts*.
  - *Faders* (CC or pitch bend): volume of players A, B, C, the voice track, the cartwall and the output on the
    on-screen mixer (the top of the fader is 0 dB); *fader start* for each player: pushed up above the threshold
    (5 % by default) the player starts, pulled back down it stops. The first message after start-up never starts
    anything (the fader may already have been up). One fader can be both volume and fader start.
  - Device, assignments and threshold are stored per computer.
- **Preview of the automatic mix** (*Settings → Automatic mix → Preview*): a diagram of how two items are mixed
  with the values currently on the page, updated with every change, even before *Apply* (and for the template being
  edited). It uses the same planner as the broadcast. Example transitions: fade-out or sudden ending, soft intro or
  strong start, different tempo, music → jingle, jingle → music, hidden track. It shows both items' waveforms, the
  outgoing volume curve, the mix point, where the outgoing item ends, the overlap and the skipped silence at the start,
  with a one-line summary (e.g. "Smart mix: fade-out ending (5.4 s), soft intro (5.0 s). Overlap: 5.0 s · fade-out:
  1.0 s"). With smart mix switched off it shows the simple mode.
- **Fix: with the interface size set to 80 % or 90 %, the main window was drawn broken** (buttons and labels shifted
  and cut off). Qt does not draw correctly with a scale factor below 1, so the smallest size is now 100 %; an earlier
  80 % or 90 % setting counts as 100 %.
- **Info window** (library header *Info* button, *Tools → Info window*, Ctrl+I): a separate, non-modal window with
  everything Éter knows about an item, so the main window keeps its layout.
  - *Track:* title, artist, album and year (marked *MUSICBRAINZ* or *THEAUDIODB* when filled in from there), length,
    category, vocal.
  - *Artist:* photo, name, type and genres, origin, formed or born, disbanded or died, members, label, mood, website.
  - *Biography:* the Wikipedia summary with a *Read more on Wikipedia* link; the footer names the sources (Wikipedia
    under CC BY-SA) and when the data was fetched.
  - *Selected* shows the item selected in the library; *On air* follows the item playing (or the one on standby).
  - Missing data for the shown item is fetched straight away, in every fetch mode (opening the window is the
    request); *Refresh data* fetches it again. While fetching, when nothing is found, and when the sources are switched
    off, the window says so (with a button to *Settings → Artist info*); data already stored is still shown.
  - The window can be moved to another screen and resized; its position, size, *Always on top* and the
    selected/on-air choice are remembered, and it reopens at start-up if it was open.
- **Colour schemes and a theme editor** (*Settings → Appearance → Colour scheme*):
  - Built-in schemes: *Éter (dark)* (the previous look), *Graphite*, *Midnight blue*, *High contrast* and *Light*.
  - The base colours can be edited one by one: background, panels, buttons, borders, text, secondary text, accent,
    information (standby, focus, links), OK, error, and players A, B and C. Changes show across the whole program at
    once, including open windows; no restart is needed.
  - The in-between shades (selection, row backgrounds, fainter borders, status bars) are worked out from the base
    colours, so a light scheme stays readable without a separate set of colours. With *Éter (dark)* nothing changes
    from 0.35.
  - Own schemes: *Save as…* stores the current colours under a name; *Save*, *Rename…* and *Delete* manage them, and
    *Revert* returns to the saved colours of the selected scheme. A note shows when the colours differ from the saved
    scheme, and switching to another scheme asks before discarding unsaved colours.
  - *Export…* writes the scheme to a JSON file (`"format": "eter-color-scheme"`); *Import…* reads one or more such
    files and adds them under their own names (an identical scheme is not added twice).
  - A warning appears when the text is hard to read on the chosen background (contrast below 4.5:1, or below 3:1 for
    secondary text).
  - Schemes are stored per computer, like the interface size. The accent and player colours set in earlier versions
    are kept.

## 0.35.0
- **WordPress plugin 1.2.2:** works on sites where a security plugin or setting allows the REST API only to logged-in
  users ("Sorry, you must be logged in to make a request", HTTP 401). The plugin makes an exception only for its own
  routes (`eter/v1/…`), both for blocks in `rest_authentication_errors` and in `rest_pre_dispatch`, with
  `?rest_route=` and `/wp-json/` addresses alike. Every other route stays blocked. The sync and catalogue calls
  remain signed with the shared key; the other routes were meant to be public (the blocks read them). Éter's status
  line now says when a refusal comes from such a security setting rather than from the plugin.
- **Mix templates** (*Settings → Automatic mix → Template*): the automatic mix settings can be saved as named
  templates (e.g. Dance, Ballad), and each library category can be given one (*Library → Categories… → Mix
  template*). The transition after an item of that category follows its template; the cue-in threshold of the
  incoming item follows the incoming item's template. Categories without a template use the default settings.
  - The template selector chooses what the page edits; *Apply* saves into it. *Save as new template…*, *Rename…* and
    *Delete* manage them, and the page lists which categories use the selected template.
  - The dead-air guard stays common to all items.
  - The Mix editor shows the template used for the selected transition; the remote editor gets the templates too.
- **MusicBrainz as a source for artist info** (free, no key; *Settings → Artist info*, can be switched on and off on
  its own):
  - Artist: country and city, year formed or born, type (group or person), genres, official website.
  - Tracks: the album and year are filled in where they are empty. The year is the first release; the album is the
    earliest official studio album the track is on (else the single or EP), matched by title, artist and length.
  - *Biography and photo from Wikipedia* (optional): through the artist's MusicBrainz link, the Wikipedia summary in
    the program's language (or English) and its picture.
  - At most one request a second, as MusicBrainz asks. Nothing is overwritten, and one source never replaces what
    another has already filled in. The *Biography* window names the sources.
- **Fix: artist info did not appear in the player** when it arrived after the item was loaded (for example with
  *When an item is loaded into a player*). The players were created before the info service and never heard about
  the update.
- **TheAudioDB:** the free key (123) currently returns the same sample artist for any search. Éter now discards these
  answers in every lookup (also the lookup by MusicBrainz ID, which is now used only with your own key) and says so in
  the settings. TheAudioDB is now off by default; earlier "not found" entries are cleared so the new sources can try
  again.

## 0.34.0
- **Artist info from TheAudioDB** (new *Settings → Artist info* page; off by default, and when off Éter sends no
  requests):
  - The player shows the artist's photo, country, year formed or born, genre, style and mood next to the title.
  - The *Biography* button opens the biography (in the program's language when TheAudioDB has it, otherwise in
    English) with the artist's details.
  - API key: the free key (123) is the default. It gives artist info, at most 30 requests a minute; Éter keeps to
    this and pauses for a minute if the service says it is too busy. With your own (Premium) key, the album and year
    of tracks are filled in too, where they are empty.
  - Lookup mode: *automatically in the background* (the whole library, new artists as they arrive), *when an item
    is loaded into a player*, or *only by hand* (library: right-click → *Fetch artist info*).
  - Nothing is overwritten. The vocal (male, female, group) is filled in only where it is still unknown, and album
    and year only where they are empty. With several artists ("A feat. B") the first one is looked up.
  - The data is kept in the database (synced to the SQL server too); photos are cached on the computer. *Delete
    downloaded info* clears both.
- Database schema 17.

## 0.33.0
- **External SQL server (MySQL / MariaDB)**: new *Settings → SQL server* page with server address, port, user,
  password, database name and *Test connection*.
  - The server is the main store, with a local copy kept as a reserve. Every change to the library, scheduler,
    playlists, mixes, cartwalls, timed events, settings and play log is uploaded within a few seconds.
  - If the server or the network goes down, the broadcast is not affected. Changes wait locally and are uploaded when
    the connection returns.
  - *Upload to the server (migrate)* copies the existing SQLite database to the server. *Download from the server*
    replaces the local database with the server's copy (backup first, then restart). *Disconnect* returns to
    local-only use.
  - One computer writes to the server at a time. On start-up Éter downloads newer data left by another computer, and
    asks before taking over from a computer that is still running (that one then stops writing and says so). If both
    sides have changes, it asks which to keep.
  - The status bar shows the connection state. Tables on the server get the `eter_` prefix.
  - Audio files are not uploaded (the library keeps their paths: use the same paths, e.g. a network share, on every
    computer).
  - Needs Qt's MySQL driver: bundled in the AppImage (with the MySQL client library) and the Windows zip (with
    MariaDB Connector/C).
- **Appearance** (new *Settings → Appearance* page, stored per computer):
  - **Accent colour**: the selected tab, main buttons, on-air marks and check boxes. Pick one of nine presets or any
    colour. Text on it switches between dark and white to stay readable.
  - **Player colours**: players A, B and C each get their own colour (letter badges, player tiles, mini mixer,
    waveform, playlist marks). *Default colours* restores the originals.
  - **Interface size** from 80 % to 200 %. Takes effect the next time Éter starts.
- **Week grid presets** (Scheduler): any number of named week grids (*New*, *Copy*, *Rename*, *Delete*).
  - Manual switch: *Make this the active one*.
  - Automatic switch: *Periods…* sets date ranges during which another grid applies, optionally *every year* (also
    across the year end, e.g. Dec 20 – Jan 6). When periods overlap, the lower one in the list wins.
  - Generation uses the grid valid on each day, so a period that starts tomorrow is already used when tomorrow's hours
    are generated. The grid selector shows which grid is active and which applies today.
- **Generate dialog** (suggestion from a user): *Generate playlists* now asks which days (date range, weekdays) and
  which hours (from–to, also across midnight such as 22–02) to generate, with *Today*, *Tomorrow*, *Rest of this
  week* and *Next week* shortcuts. It shows how many hours that is, how many are already generated and how many have
  no clock in the week grid. *Regenerate hours already generated* can be switched off to fill only empty hours. Past
  hours are skipped; items added by hand and the item playing stay.
- **Note rows in clocks**: new row type *Note (label only)*. Its text appears in the playlist above the next item, as
  a label: it does not play and takes no time.
- **Sweeper per clock row**: every clock row can have *Sweeper on the transition*. An item from the chosen category is
  laid over the transition into that row: it starts at the mix point, takes no time, and lowers the music under it by
  the chosen amount (default −6 dB).
- **Artist separation with several artists**: “A and B”, “A feat. B”, “A ft. B”, “A vs. B”, “A / B” and “A; B” count
  as separate artists for *Artist repeat*. So “Alpha and Beta” is kept apart from both Alpha and Beta. “&” keeps a
  name together (Simon & Garfunkel, Earth, Wind & Fire), and so does a slash without spaces (AC/DC). The separators can
  be edited under *Scheduler → Rules → Artist separators*. The *On time* suggestions follow the same rule.
- Database schema 16 (updated automatically on first start; older versions of Éter cannot open it afterwards).

## 0.32.0
- **Hide tabs.** New *Settings → Tabs* page: untick a tab (Live, Scheduler, Mix editor, Playlist editor, Mixdown,
  Timed events, Settings) and it disappears from the main window. Hidden tabs keep working in the background (timed
  events still run); with the Settings tab hidden, settings open from *Tools → Settings* (Ctrl+,). Stored per computer.
- **Cartwall in place of the library.** The view selector at the top of the library now has three choices: *Library*,
  *Play log* and *Cartwall*. The cartwall takes the library's place on the right, with the same selector to switch
  back. The cartwall button was removed from the playlist panel, so the playlist has its full width again. The last
  view is remembered.
- **Resizable columns everywhere.** Every column of the library, the play log, the scheduler clocks and items, the Mix
  editor, the Playlist editor, the timed events, song requests and the other lists can be dragged to a new width.
  Widths are remembered per list (per computer). Title and artist columns still fill the free space, and never get
  narrower than about 110 px: the list scrolls sideways instead.
- **Cue-in threshold in simple mix mode** (*Settings → Automatic mix → Simple mode*): the incoming track starts where
  its signal first rises above this level (default −45 dB, as before; −80 … −10 dB). A cue-in point saved for the item
  overrides it.
- **Scheduler:** the rows of a clock can be reordered by dragging (the ↑ / ↓ buttons still work).

## 0.31.1
- Website connection (bug report from Windows: “the key does not match … or this computer’s clock is wrong”, even
  with the right key):
  - Éter now shows the website's own reason for a refusal. The site states whether the key is wrong, the clocks
    differ (with both times) or the signature did not arrive. Before, every refusal showed the same general message.
  - The signature time follows the website's clock (from the Date header of its replies). A computer whose clock or
    time zone is set wrongly, even by hours, no longer causes a refusal. If the difference is a minute or more, the
    status line says so.
  - The signature is sent in the address as well as in the headers, for hosts, proxies and firewalls that remove
    custom headers.
  - Redirects (http → https, without www → with www) are followed with the request intact; the status line shows
    the new address to enter.
- WordPress plugin 1.2.1 and Éter Web 1.0.1 accept the signature from the address too, and give an exact reason
  when they refuse. Both need Éter 0.31.1 for the clock correction; Éter 0.31.1 also works with the older versions.

## 0.31.0
- **Live streams on air.** An external audio stream can be a playlist item. Supported: Icecast and Shoutcast
  streams (MP3, AAC or Ogg) over http or https; `.pls` and `.m3u` playlist files are resolved. Add one with
  *Library → Import → Live stream…*: title, address, category and planned length, with a *Test the connection*
  button that reports the format, the station name and the current title.
  - In the playlist the stream plays for its planned length. It then fades out over 2 seconds while the next item
    starts.
  - It connects when it is cued. When it goes on air it starts close to live: the audio collected while it waited
    is dropped.
  - The player shows *● LIVE STREAM* with the stream's current title (ICY metadata) and the connection state, and
    the playlist row is marked LIVE STREAM.
  - If the stream drops, Éter reconnects. If no data arrives for 15 seconds (*Settings → General and audio*), the
    next item starts. The dead-air guard leaves streams to this setting.
  - A stream cannot go into a mixdown; the Mix editor has no waveform for it.
- **Automatic mix for every item.** In automatic mix mode the program now also handles the transitions of jingles,
  ads, news and voice tracks. Before, these used the settings saved in the Mix editor, and if there were none, the
  silence at the start of the next item stayed in. Now:
  - the silence is cut at the end of the outgoing item and at the start of the next one;
  - the items follow each other back to back, with no crossfade (an optional gap can be set);
  - from a song into a jingle, news or ad, the song's end follows the smart mix (a fading ending lets the next item
    start during the fade).
  Transitions saved in the Mix editor and an item's own mix point still apply by default; this can be turned off, so
  that the program mixes every transition (a saved voice track is always kept).
- **Hidden tracks.** When at least 4 seconds of silence in the second half of a track is followed by more sound, the
  track ends at the silence.
- **Dead-air guard.** In automatic mode, if the item on air stays silent (below −50 dBFS) for 3 seconds in its second
  half, the next item starts. The level is checked before the mini mixer's faders, so a lowered or muted fader does
  not trigger it.
- All of these can be turned on or off, and their values set, in *Settings → Automatic mix*. The Mix editor shows the
  plan for these transitions too.
- The Settings pages scroll when they do not fit on the screen.
- Mini mixer: a channel's label lights up in green while there is signal on it (above −50 dB).
- **Most played.** Éter sends the website the most played songs of the last 24 hours, 7 days and 30 days (music
  only, refreshed when a track changes). WordPress plugin 1.2.0 has a new *Most played* block (period, number of
  songs, number of plays) and the shortcode `[eter_most_played period="week" count="10"]`.
- **Éter Web**, a universal module (`eter-web.zip`) that does what the WordPress plugin does, on any website. It is
  one folder for any PHP 7.4+ web host, with no database. The widgets go into any page with one script line and a
  `<div data-eter="now|next|top|request">` for each, also on another address or on a site without PHP; for site
  builders that only allow an iframe there are ready iframe pages. Its admin page (admin.php, password in
  config.php) shows the address and the key for Éter, the embed codes ready to copy, the language (English,
  Nederlands, Magyar) and the recent requests. Éter talks to it exactly as to the plugin, so in Éter only the
  address and the key are entered.
- Library panel: the title is now a drop-down that switches between the **Library** and the **Play log** (the items
  that went on air, the most recent at the top, with start time and length; searchable; double-click adds the item to
  the playlist).
- Library: songs that went on air today have a red background (the tooltip shows when); it clears at midnight.
- Cartwall: while a button plays, a hand sweeps round it clockwise from 12 o'clock, showing how far it has got; the
  swept part turns from a dark to a light shade of the button's colour (it replaces the progress bar at the bottom).

## 0.30.1
- WordPress plugin *Éter Radio* 1.1.0:
  - the language can be chosen in WordPress under Settings → Éter Radio → Language: English (the default), Dutch,
    Hungarian, or the site's language;
  - new Dutch translation.
- Before, the plugin always followed the site's language; now it is English unless another language is chosen.

## 0.30.0
- **Website (WordPress).** A new plugin, *Éter Radio* (`eter-radio.zip`), adds three blocks to a WordPress site:
  *Now playing* (with a progress bar), *Up next* (with start times) and *Song request*. Shortcodes for classic themes
  and page builders: `[eter_now_playing]`, `[eter_up_next count="5"]`, `[eter_request]`. The blocks follow the
  theme's fonts and colours and refresh themselves; the plugin is in English and Hungarian.
- Éter sends the site what is on air and what comes next every 5 seconds (and at once when a track changes), and
  collects the requests on the same call. Éter always opens the connection, signed with the plugin's key
  (HMAC-SHA256), so the studio computer does not have to be reachable from the internet. If Éter stops updating, the
  site shows the station as offline and closes requests.
- **Song requests.** Visitors search the requestable tracks by artist or title and request one. The track goes into
  the playlist right after the item on air; further requests go below the last request. Without requests the
  playlist runs as scheduled. Request rows are marked REQUEST in the playlist and on the site, and the visitor sees
  what happens to the request (in the playlist, played, rejected…).
- New settings tab, **Settings → Website**:
  - the site address, the key, how many upcoming tracks to show, music only or all items;
  - requests on or off; automatically into the playlist or after approval in the studio (the new *Requests* button
    in the top bar lists the requests, with Approve / Reject);
  - requestable: all music or selected categories;
  - when: always, or in the hours of selected clocks.
- A request older than 30 minutes when Éter collects it (for example because Éter was not running) is not played.
- The Linux AppImage now includes Qt's OpenSSL plugin (it uses the system's OpenSSL), for HTTPS to the website.
- Database schema 15 (song requests), upgraded automatically.

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
