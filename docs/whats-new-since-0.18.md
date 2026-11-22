# What's new in Éter since 0.18

This is a summary of the changes from 0.19.0 to 0.30.1, grouped by topic. The full list, version by version, is in
[CHANGELOG.md](../CHANGELOG.md).

Every database upgrade (schema 10 → 15) runs automatically at the first start. Update Éter on every computer that
uses remote editing or the backup playout, so they all run the same version.

## Playout

- **Three players (A, B and C).** A song, a jingle and the next song can sound at the same time, each on its own
  player.
  - The main window shows the player on air, with a strip of all three players under it.
  - Player C can have its own output.
- **Automatic mix / Mix editor switch** in the top bar.
  - *Automatic mix* works out song-to-song transitions from the audio.
  - *Mix editor* uses the transitions saved there, as before.
- **Smart automatic mix** (0.26). Instead of one level threshold, Éter now looks at how the outgoing track ends and how
  the next one starts:
  - it recognises fade-out endings and sudden endings;
  - it lets soft, rising intros start early, under the outgoing track;
  - it uses a shorter overlap when the tempos or keys clash.

  Every value is adjustable in *Settings → Automatic mix*. The Mix editor shows the plan and explains it.
- **Volume curve** for every item (0.22). Draw points on the waveform in the Mix editor or the item editor; the curve
  applies wherever the item plays. For example, a news jingle at full level that drops 12 dB under the newsreader.
- **Mini mixer** in the clock panel (0.28) with channels for the three players, the voice track, the cartwall and the
  main output. Each channel has:
  - a level meter with peak hold and an overload light;
  - a fader from −∞ to +6 dB;
  - a mute button.

  Fader positions are kept after a restart. Mutes are not, so the station never starts silent.

## Scheduler

- **Smarter fill to a fixed point** (0.23). The last 1–3 tracks before a fixed point are chosen together, so they end
  within ±15 s of it (the accuracy is adjustable). This works even with 6–8 minute tracks.
  - Clock rows can be set to "at most as many as fit".
  - The clock editor warns when rows cannot fit, and offers a one-click fix.
- **On-time adjustment on air** (0.24.1–0.26.2). In Auto mode, up to 30 minutes before a fixed-time item, Éter can do
  two things:
  - **Add, trim and swap tracks:** it removes up to two scheduled tracks that have not started and adds up to three
    others, so the playlist reaches the fixed point accurately.
  - **Fill a gap with one song:** it adds one song to fill the silence before the fixed point. The song may run over by
    a set limit (20 s by default) and is faded out at the fixed time.

  A track deleted by hand is not put back for two hours. The category to fill from can be set for each clock
  (0.29.4), so a 12 Inch hour fills with 12 Inch tracks and a Pop hour with Pop.
- **Musical key** (0.24).
  - Keys are read from the file tags, or estimated from the audio.
  - A background analysis fills in missing keys and tempos at the lowest priority, either continuously or in a daily
    time window.
  - Keys are shown in Camelot (8A, 5B …) or traditional notation.
  - New scheduler rule: *prefer matching* or *matching only* keys on the Camelot wheel.
- **Vocal attribute** (0.27): male, female, group, mixed or instrumental, with the rule *same vocal in a row: at most N*.
- **Rule compliance is visible.**
  - The playlist shows tempo, key and vocal on every music row, and marks a row with ⚠ when its transition breaks a
    rule.
  - After generating, the scheduler reports how well each rule was kept and where a rule had to be relaxed.
- The **Rules** section can be collapsed, and is collapsed by default (0.29.3).

## Mixdown (new in 0.29)

A separate editor that turns a sequence of items, with voice tracks, into one MP3 or WAV file, sample-accurately. The
result is added to the library's "Mixdown" category.

- The transitions are set by the smart mix when items are added, then can be changed freely on the timeline. They are
  independent of the Mix editor and the playlist.
- There are two music lanes and a voice-track lane. Items can be dragged straight onto the lane and the time where they
  should play, for example a jingle layered over the music.
- Voice tracks:
  - can be recorded, taken from a file or taken from the library, and go where the selection is on the timeline;
  - can be selected and deleted on their own;
  - duck the music under them.
- Each clip has its own volume curve, drawn on the timeline.
- Rendering runs faster than real time and does not affect the broadcast.
  - Options: MP3 128–320 kbps or WAV 16/24 bit, 48 or 44.1 kHz, with an optional peak limit at −1 dBFS.
  - Mixdown projects can be saved and reopened.

## Website and song requests (new in 0.30)

A WordPress plugin, *Éter Radio* (`eter-radio.zip`, attached to each release), connects the station's website to
Éter.
The plugin is in English by default; Dutch and Hungarian are included, and the language is chosen in WordPress under
Settings → Éter Radio (0.30.1).

- Three blocks: *Now playing* (with a progress bar), *Up next* (with start times) and *Song request*. They are also
  available as shortcodes for classic themes and page builders.
- Éter sends what is on air every 5 seconds and collects the requests on the same call. Éter always opens the
  connection, signed with the plugin's key, so the studio computer does not have to be reachable from the internet.
- Visitors search for an artist or title and request a track. The track goes into the playlist right after the item
  on air, below any earlier requests. Without requests the playlist runs as scheduled.
- In *Settings → Website*, requests can:
  - go into the playlist automatically, or wait for approval in the studio (the *Requests* button in the top bar);
  - be limited to selected categories;
  - be limited to the hours of selected clocks.

## Other

- **Now playing output** (0.20). The title on air is written to a text file from a template, for stream encoders,
  websites or captions. Éter can also update an Icecast server directly.
- **Database export and import** (0.21) in *Settings → Database*. Export works while on air. Import checks the file and
  backs up the current database first.
- **Crash report** (0.22.1). If Éter crashes, the call stack goes to `crash.log` in the data folder.
- **Window layout.** On screens below 1920×1080, the Scheduler and the Mix editor open in their own windows. On larger
  screens they stay pages of the main window.
- **Fixes:**
  - a random crash while reading audio files, present since the first versions (0.22.2);
  - the fill before a fixed point found no tracks with a small library generated far ahead (0.24.3);
  - the automatic fill stopped refilling after a few manual deletions (0.26.1, 0.26.2).
