# Contributing

Bug reports and pull requests are welcome.

## Reporting a problem

Please include the Éter version (Help → About), your distribution and desktop (X11 or Wayland),
the audio system (PipeWire, PulseAudio, ALSA or JACK), and the steps that lead to the problem.
If it is about audio, the output of `./Eter-…AppImage --selftest some-file.mp3` helps.

## Working on the code

- Build instructions are in `README.md`.
- Source comments and the original interface strings are in Hungarian. Interface text goes
  through `TR("magyar szöveg")`; the English text for it goes into `i18n/en.json`, then
  `python3 scripts/gen_i18n.py` regenerates `src/i18n_en.cpp`. A string without an entry in
  `en.json` simply shows in Hungarian.
- The database schema is in `schema.sql`. Changing it needs a migration step in
  `Database::open()` (`src/db/Database.cpp`) and a higher `PRAGMA user_version`, so existing
  databases upgrade on the next start.
- The `--…test` command-line options run scripted scenarios without a sound card
  (`--backend null`); they are what the features were checked with. Run the ones related to
  your change before opening a pull request.
