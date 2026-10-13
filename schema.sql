-- Éter – rádiós adáslebonyolító · SQLite séma, 7. verzió
--
-- Egyezmények:
--   * időtartam és pozíció: egész ezredmásodperc (_ms)
--   * időbélyeg: Unix idő másodpercben, UTC (_at)
--   * naptári nap: 'ÉÉÉÉ-HH-NN' szöveg, helyi idő szerint
--   * a hangkártya-beállítások NEM itt vannak, hanem a gép saját
--     konfigurációs fájljában (~/.config/eter/), hogy az adatbázis
--     másik gépre is átvihető legyen
--
-- Megnyitáskor minden kapcsolaton:
--   PRAGMA foreign_keys = ON;
--   PRAGMA journal_mode = WAL;

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

BEGIN;

-- ---------------------------------------------------------------------
-- Általános beállítások (ütemező szabályai, alapértelmezett mix stb.)
-- ---------------------------------------------------------------------
CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) STRICT, WITHOUT ROWID;

-- ---------------------------------------------------------------------
-- Hangtár
-- ---------------------------------------------------------------------
CREATE TABLE categories (
    id    INTEGER PRIMARY KEY,
    kind  TEXT NOT NULL CHECK (kind IN ('music','jingle','ad','news','voicetrack','playlist','other')),
    name  TEXT NOT NULL,
    color TEXT,                              -- '#rrggbb', a listák címkéihez
    UNIQUE (kind, name)
) STRICT;

CREATE TABLE items (
    id            INTEGER PRIMARY KEY,
    category_id   INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    title         TEXT NOT NULL,
    artist        TEXT NOT NULL DEFAULT '',
    album         TEXT NOT NULL DEFAULT '',
    year          INTEGER,

    -- fájl
    file_path     TEXT NOT NULL UNIQUE,
    file_size     INTEGER,
    file_mtime    INTEGER,                   -- változásfigyeléshez
    duration_ms   INTEGER NOT NULL CHECK (duration_ms >= 0),

    -- jelölőpontok: a szám saját alapértelmezései
    -- (Mix editor: „Alapértelmezés a számokhoz")
    cue_in_ms     INTEGER NOT NULL DEFAULT 0,   -- innen indul a lejátszás
    intro_ms      INTEGER,                      -- intro vége (ének kezdete)
    mix_out_ms    INTEGER,                      -- itt indul a következő elem; NULL = automatikus
    cue_out_ms    INTEGER,                      -- itt áll meg; NULL = fájl vége
    fade_out_ms   INTEGER,                      -- NULL = globális alapérték
    fade_curve    TEXT CHECK (fade_curve IN ('linear','log','scurve')),
    auto_mix_out_ms INTEGER,                    -- a program által megtalált mix pont (a hullámformából)
    gain_db       REAL NOT NULL DEFAULT 0,

    -- érvényesség (főleg reklámhoz)
    enabled       INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)),
    valid_from    TEXT,
    valid_until   TEXT,

    -- statisztika, az ismétlési szabályokhoz
    added_at       INTEGER NOT NULL DEFAULT (unixepoch()),
    last_played_at INTEGER,
    play_count     INTEGER NOT NULL DEFAULT 0
) STRICT;

CREATE INDEX idx_items_category    ON items(category_id, enabled);
CREATE INDEX idx_items_artist      ON items(artist COLLATE NOCASE);
CREATE INDEX idx_items_title       ON items(title  COLLATE NOCASE);
CREATE INDEX idx_items_last_played ON items(last_played_at);

-- Előre kiszámolt hullámforma, külön táblában, hogy az items könnyű maradjon
CREATE TABLE waveforms (
    item_id        INTEGER PRIMARY KEY REFERENCES items(id) ON DELETE CASCADE,
    samples_per_px INTEGER NOT NULL,         -- felbontás
    peaks          BLOB NOT NULL             -- min/max párok, int8
) STRICT;

-- ---------------------------------------------------------------------
-- Ütemező: órasémák és heti rács
-- ---------------------------------------------------------------------
CREATE TABLE clocks (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL UNIQUE,
    color TEXT
) STRICT;

CREATE TABLE clock_slots (
    id             INTEGER PRIMARY KEY,
    clock_id       INTEGER NOT NULL REFERENCES clocks(id) ON DELETE CASCADE,
    position       INTEGER NOT NULL,         -- sorrend a sémán belül
    slot_type      TEXT NOT NULL CHECK (slot_type IN ('category','item','fill')),
    category_id    INTEGER REFERENCES categories(id) ON DELETE RESTRICT,
    item_id        INTEGER REFERENCES items(id)      ON DELETE CASCADE,
    item_count     INTEGER NOT NULL DEFAULT 1 CHECK (item_count >= 1),  -- pl. reklámblokk: 3 szpot
    fixed_offset_s INTEGER CHECK (fixed_offset_s BETWEEN 0 AND 3599),   -- fix idő az órán belül; NULL = folytatólagos
    optional       INTEGER NOT NULL DEFAULT 0 CHECK (optional IN (0,1)), -- pl. „voice track, ha van"
    UNIQUE (clock_id, position),
    CHECK ( (slot_type = 'item' AND item_id IS NOT NULL)
         OR (slot_type IN ('category','fill') AND category_id IS NOT NULL) )
) STRICT;

-- Általános hét: nap (0 = hétfő … 6 = vasárnap) × óra → óraséma
CREATE TABLE week_grid (
    weekday  INTEGER NOT NULL CHECK (weekday BETWEEN 0 AND 6),
    hour     INTEGER NOT NULL CHECK (hour BETWEEN 0 AND 23),
    clock_id INTEGER NOT NULL REFERENCES clocks(id) ON DELETE RESTRICT,
    PRIMARY KEY (weekday, hour)
) STRICT, WITHOUT ROWID;

-- Eltérés egy konkrét napon (ünnepnap, különkiadás)
CREATE TABLE grid_overrides (
    air_date TEXT NOT NULL,
    hour     INTEGER NOT NULL CHECK (hour BETWEEN 0 AND 23),
    clock_id INTEGER NOT NULL REFERENCES clocks(id) ON DELETE RESTRICT,
    PRIMARY KEY (air_date, hour)
) STRICT, WITHOUT ROWID;

-- ---------------------------------------------------------------------
-- Playlist: óránként egy, benne a sorba rendezett elemek
-- ---------------------------------------------------------------------
CREATE TABLE playlists (
    id           INTEGER PRIMARY KEY,
    air_date     TEXT NOT NULL,
    hour         INTEGER NOT NULL CHECK (hour BETWEEN 0 AND 23),
    clock_id     INTEGER REFERENCES clocks(id) ON DELETE SET NULL,  -- miből generálódott
    generated_at INTEGER,                    -- NULL = kézzel készült
    UNIQUE (air_date, hour)
) STRICT;

CREATE TABLE playlist_entries (
    id            INTEGER PRIMARY KEY,
    playlist_id   INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
    position      INTEGER NOT NULL,
    item_id       INTEGER NOT NULL REFERENCES items(id) ON DELETE RESTRICT,
    fixed_start_s INTEGER CHECK (fixed_start_s BETWEEN 0 AND 3599),  -- fix idejű elem
    source        TEXT NOT NULL DEFAULT 'manual' CHECK (source IN ('scheduler','manual')),
    status        TEXT NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending','playing','played','skipped')),
    group_id      INTEGER,                   -- kézzel készített playlist blokkja a fő playlistben
    group_name    TEXT,
    overlay_offset_ms INTEGER,               -- ráfektetett elem: a kezdése a szomszédos átmenet mix pontjához képest
    overlay_duck_db   REAL NOT NULL DEFAULT 0, -- ennyivel halkul alatta a zene
    UNIQUE (playlist_id, position)
) STRICT;

CREATE INDEX idx_entries_item ON playlist_entries(item_id);

-- Kézzel készített, elmentett playlistek (Playlist szerkesztő); playlist típusú kategóriába kerülnek.
CREATE TABLE saved_playlists (
    id          INTEGER PRIMARY KEY,
    category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    name        TEXT NOT NULL,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    UNIQUE (category_id, name)
) STRICT;

CREATE TABLE saved_playlist_items (
    playlist_id INTEGER NOT NULL REFERENCES saved_playlists(id) ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    item_id     INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    PRIMARY KEY (playlist_id, position)
) STRICT, WITHOUT ROWID;

-- Egyedi mix két szomszédos playlist-elem között
-- (Mix editor: „Csak ez az átmenet"). Ha a sorrend megváltozik, és a
-- from_entry már nem a to_entry közvetlen előzője, a sor érvényét veszti.
CREATE TABLE transitions (
    to_entry_id    INTEGER PRIMARY KEY REFERENCES playlist_entries(id) ON DELETE CASCADE,
    from_entry_id  INTEGER NOT NULL    REFERENCES playlist_entries(id) ON DELETE CASCADE,

    -- kimenő elem
    mix_out_ms     INTEGER NOT NULL,         -- a kimenő elem ezen pontján indul a bejövő
    fade_out_ms    INTEGER NOT NULL DEFAULT 0,
    fade_out_curve TEXT NOT NULL DEFAULT 'linear' CHECK (fade_out_curve IN ('linear','log','scurve')),
    out_gain_db    REAL NOT NULL DEFAULT 0,

    -- bejövő elem
    cue_in_ms      INTEGER NOT NULL DEFAULT 0,
    fade_in_ms     INTEGER NOT NULL DEFAULT 0,
    start_level_db REAL NOT NULL DEFAULT 0,  -- a beúsztatás kezdő szintje
    in_gain_db     REAL NOT NULL DEFAULT 0,

    -- voice track az átmenet fölött
    vt_item_id     INTEGER REFERENCES items(id) ON DELETE SET NULL,
    vt_offset_ms   INTEGER NOT NULL DEFAULT 0,   -- kezdés a mix ponthoz képest (negatív: előtte)
    vt_duck_db     REAL NOT NULL DEFAULT -9,     -- ennyivel halkul alatta a zene
    out_end_ms     INTEGER,                      -- a kimenő elem itt hallgat el; NULL = mix pont + kiúsztatás

    updated_at     INTEGER NOT NULL DEFAULT (unixepoch())
) STRICT;

-- ---------------------------------------------------------------------
-- Cartwall: színes gombfalak azonnal indítható hangokkal. Egy gomb vagy
-- hangtárelemre mutat (item_id), vagy közvetlenül egy fájlra (file_path).
-- ---------------------------------------------------------------------
CREATE TABLE cartwalls (
    id       INTEGER PRIMARY KEY,
    name     TEXT NOT NULL,
    color    TEXT,
    position INTEGER NOT NULL DEFAULT 0
) STRICT;

CREATE TABLE carts (
    wall_id     INTEGER NOT NULL REFERENCES cartwalls(id) ON DELETE CASCADE,
    slot        INTEGER NOT NULL,
    item_id     INTEGER REFERENCES items(id) ON DELETE CASCADE,
    file_path   TEXT,
    label       TEXT NOT NULL DEFAULT '',
    duration_ms INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (wall_id, slot)
) STRICT, WITHOUT ROWID;

-- ---------------------------------------------------------------------
-- Időzített események: egy hangtárelem vagy egy mentett playlist adott
-- időpontban a playlistbe kerül (következőként vagy a végére), vagy elindul.
-- Egyszeri (on_date) vagy hetente ismétlődő (weekdays: 1. bit hétfő … 7. bit vasárnap).
-- ---------------------------------------------------------------------
CREATE TABLE timed_events (
    id                INTEGER PRIMARY KEY,
    enabled           INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)),
    item_id           INTEGER REFERENCES items(id) ON DELETE CASCADE,
    saved_playlist_id INTEGER REFERENCES saved_playlists(id) ON DELETE CASCADE,
    action            TEXT NOT NULL DEFAULT 'next' CHECK (action IN ('next','end','start')),
    time_s            INTEGER NOT NULL CHECK (time_s BETWEEN 0 AND 86399),
    on_date           TEXT,
    weekdays          INTEGER NOT NULL DEFAULT 0,
    last_fired_at     INTEGER,
    CHECK ((item_id IS NULL) <> (saved_playlist_id IS NULL))
) STRICT;

-- ---------------------------------------------------------------------
-- Adásnapló: mi szólt ténylegesen (jogdíjjelentéshez is)
-- A cím és az előadó másolatként is bekerül, hogy a napló akkor is
-- olvasható maradjon, ha az elemet később törlik a hangtárból.
-- ---------------------------------------------------------------------
CREATE TABLE play_log (
    id         INTEGER PRIMARY KEY,
    started_at INTEGER NOT NULL,
    ended_at   INTEGER,
    item_id    INTEGER REFERENCES items(id) ON DELETE SET NULL,
    entry_id   INTEGER REFERENCES playlist_entries(id) ON DELETE SET NULL,
    kind       TEXT NOT NULL,
    title      TEXT NOT NULL,
    artist     TEXT NOT NULL DEFAULT '',
    player     INTEGER NOT NULL CHECK (player IN (1,2)),
    mode       TEXT NOT NULL CHECK (mode IN ('auto','assist','manual')),
    end_reason TEXT CHECK (end_reason IN ('finished','mixed','faded','stopped','error'))
) STRICT;

CREATE INDEX idx_log_started ON play_log(started_at);
CREATE INDEX idx_log_item    ON play_log(item_id, started_at);

-- ---------------------------------------------------------------------
-- Kezdőadatok
-- ---------------------------------------------------------------------
INSERT INTO settings (key, value) VALUES
    ('artist_separation_min', '90'),
    ('title_separation_min',  '240'),
    ('generate_ahead_hours',  '24'),
    ('fixed_time_policy',     'fade'),       -- 'fade' = futó elem kiúsztatása | 'wait' = megvárja a végét
    ('default_fade_out_ms',   '2500'),
    ('default_fade_curve',    'linear'),
    ('auto_mix_threshold_db', '-24');        -- automatikus mix pont: a szám vége előtt eddig halkul

-- Kategóriákat a felhasználó hoz létre, ezért nincs előre feltöltött lista.

PRAGMA user_version = 8;

COMMIT;
