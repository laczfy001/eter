# Éter

Rádiós adáslebonyolító Linuxra: két lejátszó, cartwall, hangtár, ütemező és mix editor egyetlen
asztali alkalmazásban. Egyetlen AppImage fájlként fut.

*English: [README.md](README.md)*

![Az Adás oldal: két lejátszó, a playlist és a hangtár](docs/screenshots/on-air.png)

## Funkciók

- **Két lejátszó** hullámformával, intro-visszaszámlálóval, automata vagy asszisztált átúsztatással;
  minden csatorna (1-es és 2-es lejátszó, előhallgatás, cartwall) külön hangkártyára tehető.
- **Hangtár** SQLite adatbázisban, saját kategóriákkal, fájlok és egész mappák importálásával,
  elemszerkesztővel (cím, előadó, kezdőpont, intro és outro jelölő).
- **Playlist** tervezett adásidővel, húzással rendezhető sorokkal, adásnaplóval (CSV-export).
- **Ütemező**: órasémák kategóriákból, fix idejű elemekkel és kitöltéssel, heti rács,
  előadó- és számismétlési szabályok, automatikus előre generálás.
- **Mix editor**: két vagy három egymást követő elem átmenete idővonalon állítható,
  úsztatási görbékkel, voice trackkel (felvétel vagy fájl) és zenehalkítással.
- **Playlist szerkesztő**: kézzel összeállított playlistek mentése kategóriába, és beillesztésük
  a fő playlistbe kinyitható blokként.
- **Cartwall**: színes gombfalak azonnal induló hangokkal, a hangtárból vagy fájlból feltöltve.
- Angol és magyar felület (Beállítások → Nyelv).

## Telepítés

Töltsd le a legfrissebb `Eter-…-x86_64.AppImage` fájlt a Releases oldalról, majd:

    chmod +x Eter-*-x86_64.AppImage
    ./Eter-*-x86_64.AppImage

64 bites Linux kell hozzá glibc 2.39-cel vagy újabbal (Ubuntu 24.04 és későbbi, illetve hasonló
disztribúciók), a hanghoz PipeWire, PulseAudio, ALSA vagy JACK.

A hangtár helye `~/.local/share/eter/library.sqlite`, a hangkártya-beállításoké
`~/.config/Eter/Eter.conf`. A hangfájlokat a program nem másolja és nem módosítja, a hangtár
csak hivatkozik rájuk.

Hasznos kapcsolók: `--backend pulseaudio|alsa|jack`, `--db FÁJL` (másik adatbázis),
`--selftest FÁJL` (a hangmotor ellenőrzése egy hangfájllal), `--help`.

## Fordítás forrásból

Ubuntu 24.04-en vagy újabbon:

    sudo apt install build-essential cmake ninja-build nasm git curl \
        qt6-base-dev libqt6sql6-sqlite qt6-wayland libgl1-mesa-dev
    scripts/build_ffmpeg.sh      # kis, statikus FFmpeg csak hangdekóderekkel, a third_party/ffmpeg alá
    cmake -S . -B build/release -G Ninja
    cmake --build build/release
    build/release/eter

AppImage készítése (első alkalommal letölti a linuxdeploy eszközt):

    scripts/build_appimage.sh    # eredmény: build/Eter-<verzió>-x86_64.AppImage

Ha egy `v0.10.1` formájú címkét (tag) töltesz fel, a `.github/workflows/build.yml` munkafolyamat
megépíti az AppImage-et, és egy kiadáshoz (release) csatolja.

## Fordítások

A felület szövegei a forrásban magyarul vannak, `TR("magyar szöveg")` alakban; az angol
megfelelőjük az `i18n/en.json` fájlban van. Módosítás után `python3 scripts/gen_i18n.py`
generálja újra a táblát.

## Licenc

Az Éter szabad szoftver, a [GNU General Public License 3.0 vagy újabb](LICENSE) feltételei
szerint. A felhasznált könyvtárak és licenceik: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
