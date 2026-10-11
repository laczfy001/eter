# Éter

Rádiós adáslebonyolító Linuxra és Windowsra: két lejátszó, cartwall, hangtár, ütemező és mix editor
egyetlen asztali alkalmazásban.

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

### Linux

Töltsd le a legfrissebb `Eter-…-x86_64.AppImage` fájlt a Releases oldalról, majd:

    chmod +x Eter-*-x86_64.AppImage
    ./Eter-*-x86_64.AppImage

64 bites Linux kell hozzá glibc 2.39-cel vagy újabbal (Ubuntu 24.04 és későbbi, illetve hasonló
disztribúciók), a hanghoz PipeWire, PulseAudio, ALSA vagy JACK.

A hangtár helye `~/.local/share/eter/library.sqlite`, a hangkártya-beállításoké
`~/.config/Eter/Eter.conf`.

### Windows

Töltsd le az `Eter-…-win64.zip` fájlt a Releases oldalról, csomagold ki bárhová, és indítsd el az
`eter.exe`-t (64 bites Windows 10 vagy 11). A program nincs digitálisan aláírva, ezért az első
indításkor a Windows „ismeretlen kiadó” figyelmeztetést adhat. A hang WASAPI-n megy. A hangtár helye
`%LOCALAPPDATA%\eter\library.sqlite`, a hangkártya-beállítások a rendszerleíró adatbázisban vannak
(`HKEY_CURRENT_USER\Software\Eter`).

A windowsos változat új, és jóval kevesebb tesztelést kapott, mint a linuxos.

### Mindkettő

A hangfájlokat a program nem másolja és nem módosítja, a hangtár csak hivatkozik rájuk.

Hasznos kapcsolók: `--backend NÉV` (Linuxon `pulseaudio`, `alsa`, `jack`; Windowson `wasapi`,
`dsound`), `--db FÁJL` (másik adatbázis),
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

Windowson, [MSYS2](https://www.msys2.org/) **UCRT64** parancssorban:

    pacman -S --needed git make diffutils zip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
        mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-qt6-base
    scripts/build_ffmpeg.sh
    scripts/build_windows.sh     # eredmény: build/Eter-<verzió>-win64.zip

VS Code-ból: telepítsd az MSYS2-t az alapértelmezett helyére (`C:\msys64`), nyisd meg a projekt
mappáját, majd Terminal → Run Task…, és futtasd sorban az „Éter: 1 … 4” nevű feladatokat (az első
kettőt csak egyszer kell). Utána a Ctrl+Shift+B újraépíti a csomagot. A projekt olyan útvonalon
legyen, amelyben nincs szóköz és ékezetes betű, például `C:\eter`.

Ha egy `v0.11.0` formájú címkét (tag) töltesz fel, a `.github/workflows/build.yml` munkafolyamat
megépíti az AppImage-et és a windowsos zipet, és egy kiadáshoz (release) csatolja őket.

## Fordítások

A felület szövegei a forrásban magyarul vannak, `TR("magyar szöveg")` alakban; az angol
megfelelőjük az `i18n/en.json` fájlban van. Módosítás után `python3 scripts/gen_i18n.py`
generálja újra a táblát.

## Készítők

Tervezte és irányította: laczfy001. A kódot Claude, az Anthropic MI-asszisztense írta.

## Licenc

Az Éter szabad szoftver, a [GNU General Public License 3.0 vagy újabb](LICENSE) feltételei
szerint. A felhasznált könyvtárak és licenceik: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Készítők

Tervezte és irányította: laczfy001. A kódot Claude, az Anthropic MI-asszisztense írta.
