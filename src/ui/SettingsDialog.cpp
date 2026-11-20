#include "i18n.h"
#include "SettingsDialog.h"
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include "net/Remote.h"
#include "MainWindow.h"
#include "audio/ProgramLogger.h"
#include "core/NowPlaying.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QTabWidget>
#include <QMessageBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QUrl>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(MainWindow *main) : QDialog(main), main_(main)
{
    setWindowTitle(TR("Beállítások"));
    setMinimumWidth(720);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 14);
    outer->setSpacing(10);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("settingsTabs");
    outer->addWidget(tabs, 1);
    auto newPage = [tabs](const QString &title) {
        auto *page = new QWidget;
        auto *l = new QVBoxLayout(page);
        l->setContentsMargins(14, 14, 14, 10);
        l->setSpacing(10);
        tabs->addTab(page, title);
        return l;
    };
    QVBoxLayout *root = newPage(TR("Általános és hang"));

    // Nyelv: a program indulásakor dől el, ezért a váltás az újraindítás után érvényes.
    auto *langCaption = new QLabel(TR("NYELV"));
    langCaption->setObjectName("caption");
    root->addWidget(langCaption);
    auto *langRow = new QHBoxLayout;
    auto *language = new QComboBox;
    language->setObjectName("languageCombo");
    language->setAccessibleName(TR("A felület nyelve"));
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Magyar"), QStringLiteral("hu"));
    language->setMinimumWidth(220);
    const QString saved = QSettings().value("ui/language", QStringLiteral("en")).toString();
    language->setCurrentIndex(std::max(0, language->findData(saved)));
    auto *langNote = new QLabel;
    langNote->setObjectName("small");
    auto showNote = [langNote, language] {
        // A megjegyzés mindkét nyelven látszik, hogy annak is érthető legyen, aki épp átváltott.
        const bool pending = language->currentData().toString() != i18n::language();
        langNote->setText(pending ? QStringLiteral("Restart the program to apply · A váltáshoz indítsd újra a programot")
                                  : QString());
    };
    langRow->addWidget(new QLabel(QStringLiteral("Language · Nyelv")));
    langRow->addWidget(language);
    langRow->addSpacing(16);
    langRow->addWidget(langNote);
    langRow->addStretch(1);
    root->addLayout(langRow);
    root->addSpacing(8);
    connect(language, &QComboBox::activated, this, [language, showNote] {
        QSettings().setValue("ui/language", language->currentData().toString());
        showNote();
    });
    showNote();

    auto *sysCaption = new QLabel(TR("HANGRENDSZER"));
    sysCaption->setObjectName("caption");
    root->addWidget(sysCaption);

    auto *sysRow = new QHBoxLayout;
    auto *backendLabel = new QLabel(TR("Háttérrendszer"));
    backend_ = new QComboBox;
    backend_->addItem(TR("Automatikus"), QString());
#ifdef Q_OS_WIN
    backend_->addItem(QStringLiteral("WASAPI"), QStringLiteral("wasapi"));
    backend_->addItem(QStringLiteral("DirectSound"), QStringLiteral("dsound"));
#else
    backend_->addItem(TR("PulseAudio / PipeWire"), QStringLiteral("pulseaudio"));
    backend_->addItem(TR("ALSA (közvetlen)"), QStringLiteral("alsa"));
    backend_->addItem(TR("JACK"), QStringLiteral("jack"));
#endif
    backend_->setMinimumWidth(220);
    backendLabel->setBuddy(backend_);
    auto *rate = new QLabel(TR("Belső mintavétel: 48 000 Hz, sztereó"));
    rate->setObjectName("small");
    sysRow->addWidget(backendLabel);
    sysRow->addWidget(backend_);
    sysRow->addSpacing(16);
    sysRow->addWidget(rate);
    sysRow->addStretch(1);
    root->addLayout(sysRow);
    root->addSpacing(8);

    auto *outRow = new QHBoxLayout;
    auto *outCaption = new QLabel(TR("KIMENETEK"));
    outCaption->setObjectName("caption");
    auto *refresh = new QPushButton(TR("Eszközlista frissítése"));
    outRow->addWidget(outCaption);
    outRow->addStretch(1);
    outRow->addWidget(refresh);
    root->addLayout(outRow);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(2, 1);
    const QString names[kRows] = {TR("A lejátszó"), TR("B lejátszó"), TR("C lejátszó"),
                                  TR("Előhallgatás (PFL)"), TR("Cartwall")};
    const char *badges[kRows] = {"badge1", "badge2", "badge3", "badgeP", "badgeP"};
    const QString letters[kRows] = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("P"), QStringLiteral("W")};
    for (int ch = 0; ch < kRows; ++ch) {
        auto *badge = new QLabel(letters[ch]);
        badge->setObjectName(badges[ch]);
        badge->setAlignment(Qt::AlignCenter);
        auto *name = new QLabel(names[ch]);
        device_[ch] = new QComboBox;
        device_[ch]->setAccessibleName(names[ch] + TR(" kimenete"));
        name->setBuddy(device_[ch]);
        test_[ch] = new QPushButton(TR("Teszthang"));
        test_[ch]->setToolTip(TR("Rövid sípoló hang ezen a kimeneten"));
        state_[ch] = new QLabel;
        state_[ch]->setObjectName("small");
        state_[ch]->setMinimumWidth(120);
        grid->addWidget(badge, ch, 0);
        grid->addWidget(name, ch, 1);
        grid->addWidget(device_[ch], ch, 2);
        grid->addWidget(test_[ch], ch, 3);
        grid->addWidget(state_[ch], ch, 4);

        connect(device_[ch], &QComboBox::activated, this, [this, ch](int index) {
            main_->changeRoute(channelOf(ch), device_[ch]->itemData(index).toString());
        });
        connect(test_[ch], &QPushButton::clicked, this, [this, ch] {
            const int c = channelOf(ch);
            main_->engine()->testTone(c == MainWindow::kCartRoute ? eter::Engine::Cart1 : static_cast<eter::Engine::Channel>(c));
        });
    }
    root->addLayout(grid);

    auto *note = new QLabel(TR("Mindegyik lejátszó külön hangkártyára tehető, például ha a keverőpulton saját fadert kapnak. "
        "Ha több csatorna ugyanoda szól, a program belül keveri össze őket. A voice track és a ráfektetett elemek az A "
        "lejátszó kimenetén szólnak; a cartwall alapból szintén azt követi."));
    note->setObjectName("small");
    note->setWordWrap(true);
    root->addWidget(note);
    root->addSpacing(8);

    root->addStretch(1);

    // Adásrögzítés (logger)
    root = newPage(TR("Adásrögzítés"));
    {
        auto *cap = new QLabel(TR("ADÁSRÖGZÍTÉS (LOGGER)"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *about = new QLabel(TR("Az adásba szóló jelet (lejátszók, voice track, ráfektetett elemek, cartwall) rögzíti "
                                    "óránként új MP3 fájlba, az előhallgatás nélkül. A megőrzési időnél régebbi fájlokat "
                                    "magától törli (csak a saját, „Eter-…” nevű fájljait)."));
        about->setObjectName("small");
        about->setWordWrap(true);
        root->addWidget(about);
        QSettings ls;
        auto *on = new QCheckBox(TR("Rögzítés bekapcsolva"));
        on->setChecked(ls.value("logger/enabled", false).toBool());
        root->addWidget(on);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnStretch(1, 1);
        auto *folder = new QLineEdit(ls.value("logger/folder").toString());
        folder->setPlaceholderText(ProgramLogger::defaultFolder());
        auto *browse = new QPushButton(TR("Tallózás…"));
        auto *openDir = new QPushButton(TR("Megnyitás"));
        g->addWidget(new QLabel(TR("Mappa")), 0, 0);
        g->addWidget(folder, 0, 1);
        g->addWidget(browse, 0, 2);
        g->addWidget(openDir, 0, 3);
        auto *kbps = new QComboBox;
        for (int k : {64, 96, 128, 160, 192, 256, 320}) kbps->addItem(TR("%1 kbit/s").arg(k), k);
        kbps->setCurrentIndex(std::max(0, kbps->findData(ls.value("logger/kbps", 128).toInt())));
        g->addWidget(new QLabel(TR("Minőség")), 1, 0);
        g->addWidget(kbps, 1, 1, Qt::AlignLeft);
        auto *days = new QSpinBox;
        days->setRange(1, 365);
        days->setSuffix(TR(" nap"));
        days->setValue(ls.value("logger/days", 30).toInt());
        g->addWidget(new QLabel(TR("Megőrzés")), 2, 0);
        g->addWidget(days, 2, 1, Qt::AlignLeft);
        root->addLayout(g);
        auto *size = new QLabel;
        size->setObjectName("small");
        root->addWidget(size);
        auto *applyRow = new QHBoxLayout;
        auto *apply = new QPushButton(TR("Alkalmazás"));
        applyRow->addWidget(apply);
        applyRow->addStretch(1);
        root->addLayout(applyRow);
        auto *status = new QLabel;
        status->setObjectName("small");
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(status);
        root->addStretch(1);
        auto estimate = [size, kbps, days] {
            const double gb = kbps->currentData().toInt() * 1000.0 / 8 * 3600 * 24 * days->value() / 1e9;
            size->setText(TR("Helyigény: kb. %1 MB óránként, %2 GB a teljes megőrzési időre.")
                              .arg(kbps->currentData().toInt() * 1000 / 8 * 3600 / 1000000)
                              .arg(i18n::locale().toString(gb, 'f', 1)));
        };
        estimate();
        connect(kbps, &QComboBox::activated, this, estimate);
        connect(days, &QSpinBox::valueChanged, this, estimate);
        connect(browse, &QPushButton::clicked, this, [this, folder] {
            const QString d = QFileDialog::getExistingDirectory(this, TR("Az adásrögzítés mappája"),
                                                                folder->text().isEmpty() ? ProgramLogger::defaultFolder() : folder->text());
            if (!d.isEmpty()) folder->setText(QDir::toNativeSeparators(d));
        });
        connect(openDir, &QPushButton::clicked, this, [folder] {
            const QString d = folder->text().isEmpty() ? ProgramLogger::defaultFolder() : folder->text();
            QDir().mkpath(d);
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        connect(apply, &QPushButton::clicked, this, [this, on, folder, kbps, days, status] {
            QSettings s;
            s.setValue("logger/enabled", on->isChecked());
            s.setValue("logger/folder", QDir::fromNativeSeparators(folder->text().trimmed()));
            s.setValue("logger/kbps", kbps->currentData().toInt());
            s.setValue("logger/days", days->value());
            main_->configureLogger();
            status->setText(TR("Állapot: %1").arg(main_->loggerStatus()));
        });
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this, status] { status->setText(TR("Állapot: %1").arg(main_->loggerStatus())); });
        t->start(1000);
        status->setText(TR("Állapot: %1").arg(main_->loggerStatus()));
    }

    root = newPage(TR("Most szól"));
    {
        // „Most szól”: a szóló elem címe fájlba és/vagy közvetlenül az Icecast szerverre.
        const NowPlaying::Config cur = NowPlaying::loadConfig();
        auto *cap = new QLabel(TR("MOST SZÓL – FÁJLBA ÍRÁS"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *about = new QLabel(TR("Az adásban lévő elem címét egy szövegfájlba írja minden váltáskor. A stream kódoló "
                                    "(például butt, Liquidsoap, Mixxx, OBS) ebből a fájlból tudja az Icecast vagy Shoutcast "
                                    "szerverre küldeni a címet, és weboldal vagy képernyőfelirat is olvashatja."));
        about->setObjectName("small");
        about->setWordWrap(true);
        root->addWidget(about);
        auto *fileOn = new QCheckBox(TR("Kiírás fájlba"));
        fileOn->setObjectName("nowFileOn");
        fileOn->setChecked(cur.fileEnabled);
        root->addWidget(fileOn);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnStretch(1, 1);
        auto *path = new QLineEdit(QDir::toNativeSeparators(cur.path));
        path->setObjectName("nowPath");
        path->setPlaceholderText(QDir::toNativeSeparators(NowPlaying::defaultPath()));
        auto *browse = new QPushButton(TR("Tallózás…"));
        g->addWidget(new QLabel(TR("Fájl")), 0, 0);
        g->addWidget(path, 0, 1);
        g->addWidget(browse, 0, 2);
        auto *format = new QLineEdit(cur.format);
        format->setObjectName("nowFormat");
        g->addWidget(new QLabel(TR("Formátum")), 1, 0);
        g->addWidget(format, 1, 1, 1, 2);
        auto *fields = new QLabel(TR("Mezők: %artist% előadó · %title% cím · %category% kategória · %duration% hossz · "
                                     "%next_artist%, %next_title% a következő elem · %time% idő · \\n új sor. "
                                     "Ha egy mező üres, a mellette álló elválasztó (például „ - ”) elmarad."));
        fields->setObjectName("small");
        fields->setWordWrap(true);
        fields->setTextFormat(Qt::PlainText);
        g->addWidget(fields, 2, 1, 1, 2);
        auto *preview = new QLabel;
        preview->setObjectName("small");
        preview->setTextFormat(Qt::PlainText);
        g->addWidget(new QLabel(TR("Előnézet")), 3, 0);
        g->addWidget(preview, 3, 1, 1, 2);
        auto *nonMusic = new QComboBox;
        nonMusic->setObjectName("nowNonMusic");
        nonMusic->addItem(TR("az előző zene címe marad"), int(NowPlaying::KeepLast));
        nonMusic->addItem(TR("a saját címe (például a jingle neve)"), int(NowPlaying::OwnTitle));
        nonMusic->addItem(TR("ez a szöveg:"), int(NowPlaying::FixedText));
        nonMusic->setCurrentIndex(std::max(0, nonMusic->findData(int(cur.nonMusic))));
        auto *nonMusicText = new QLineEdit(cur.nonMusicText);
        nonMusicText->setPlaceholderText(TR("például a rádió neve"));
        auto *nmRow = new QHBoxLayout;
        nmRow->addWidget(nonMusic);
        nmRow->addWidget(nonMusicText, 1);
        g->addWidget(new QLabel(TR("Nem zenei elemnél")), 4, 0);
        g->addLayout(nmRow, 4, 1, 1, 2);
        auto *idle = new QLineEdit(cur.idleText);
        idle->setPlaceholderText(TR("üres fájl"));
        g->addWidget(new QLabel(TR("Ha semmi sem szól")), 5, 0);
        g->addWidget(idle, 5, 1, 1, 2);
        auto *bom = new QCheckBox(TR("UTF-8 BOM a fájl elején (csak ha az olvasó program ékezethibásan mutatja a címet)"));
        bom->setChecked(cur.bom);
        g->addWidget(bom, 6, 1, 1, 2);
        root->addLayout(g);

        auto *iceCap = new QLabel(TR("ICECAST – KÖZVETLEN FRISSÍTÉS"));
        iceCap->setObjectName("caption");
        root->addWidget(iceCap);
        auto *iceAbout = new QLabel(TR("Ha a kódoló nem tud fájlból címet olvasni, a program maga is elküldheti a címet az "
                                       "Icecast szervernek (admin/metadata). A felhasználó az Icecast admin felhasználója, vagy "
                                       "„source” a mount saját jelszavával. A jelszó ezen a gépen, a program beállításai között "
                                       "tárolódik."));
        iceAbout->setObjectName("small");
        iceAbout->setWordWrap(true);
        root->addWidget(iceAbout);
        auto *iceOn = new QCheckBox(TR("Cím küldése az Icecast szervernek"));
        iceOn->setObjectName("nowIceOn");
        iceOn->setChecked(cur.icecastEnabled);
        root->addWidget(iceOn);
        auto *ig = new QGridLayout;
        ig->setHorizontalSpacing(10);
        ig->setVerticalSpacing(8);
        ig->setColumnStretch(1, 1);
        ig->setColumnStretch(3, 1);
        auto *iceUrl = new QLineEdit(cur.icecastUrl);
        iceUrl->setPlaceholderText(QStringLiteral("http://localhost:8000"));
        auto *iceMount = new QLineEdit(cur.icecastMount);
        iceMount->setPlaceholderText(QStringLiteral("/stream"));
        auto *iceUser = new QLineEdit(cur.icecastUser);
        auto *icePass = new QLineEdit(cur.icecastPassword);
        icePass->setEchoMode(QLineEdit::Password);
        ig->addWidget(new QLabel(TR("Szerver")), 0, 0);
        ig->addWidget(iceUrl, 0, 1);
        ig->addWidget(new QLabel(TR("Mount")), 0, 2);
        ig->addWidget(iceMount, 0, 3);
        ig->addWidget(new QLabel(TR("Felhasználó")), 1, 0);
        ig->addWidget(iceUser, 1, 1);
        ig->addWidget(new QLabel(TR("Jelszó")), 1, 2);
        ig->addWidget(icePass, 1, 3);
        root->addLayout(ig);

        auto *applyRow = new QHBoxLayout;
        auto *apply = new QPushButton(TR("Alkalmazás"));
        apply->setObjectName("nowApply");
        applyRow->addWidget(apply);
        applyRow->addStretch(1);
        root->addLayout(applyRow);
        auto *status = new QLabel;
        status->setObjectName("small");
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(status);
        root->addStretch(1);

        auto refresh = [=] {
            nonMusicText->setEnabled(nonMusic->currentData().toInt() == NowPlaying::FixedText);
            for (QWidget *w : std::initializer_list<QWidget *>{path, browse, format, nonMusic, idle, bom})
                w->setEnabled(fileOn->isChecked() || iceOn->isChecked());
            for (QWidget *w : std::initializer_list<QWidget *>{iceUrl, iceMount, iceUser, icePass}) w->setEnabled(iceOn->isChecked());
            NowPlaying::Track t, n;
            t.itemId = 1;
            t.title = TR("Hajnali vonat");
            t.artist = TR("Kormorán Híd");
            t.category = TR("Zene");
            t.durationMs = 241000;
            n.title = TR("Lassú folyó");
            n.artist = TR("Holdkomp");
            preview->setText(NowPlaying::render(format->text(), t, n).replace(QLatin1Char('\n'), QStringLiteral(" ⏎ ")));
        };
        refresh();
        connect(format, &QLineEdit::textChanged, this, refresh);
        connect(nonMusic, &QComboBox::currentIndexChanged, this, refresh);
        connect(fileOn, &QCheckBox::toggled, this, refresh);
        connect(iceOn, &QCheckBox::toggled, this, refresh);
        connect(browse, &QPushButton::clicked, this, [this, path] {
            const QString start = path->text().isEmpty() ? NowPlaying::defaultPath() : path->text();
            const QString f = QFileDialog::getSaveFileName(this, TR("A „Most szól” fájl"), start, TR("Szövegfájl (*.txt);;Minden fájl (*)"),
                                                           nullptr, QFileDialog::DontConfirmOverwrite);
            if (!f.isEmpty()) path->setText(QDir::toNativeSeparators(f));
        });
        connect(apply, &QPushButton::clicked, this, [=, this] {
            NowPlaying::Config c;
            c.fileEnabled = fileOn->isChecked();
            c.path = QDir::fromNativeSeparators(path->text().trimmed());
            c.format = format->text().isEmpty() ? NowPlaying::Config().format : format->text();
            c.bom = bom->isChecked();
            c.nonMusic = static_cast<NowPlaying::NonMusic>(nonMusic->currentData().toInt());
            c.nonMusicText = nonMusicText->text();
            c.idleText = idle->text();
            c.icecastEnabled = iceOn->isChecked();
            c.icecastUrl = iceUrl->text().trimmed();
            c.icecastMount = iceMount->text().trimmed();
            c.icecastUser = iceUser->text().trimmed();
            c.icecastPassword = icePass->text();
            NowPlaying::saveConfig(c);
            main_->configureNowPlaying();
            status->setText(TR("Állapot: %1").arg(main_->nowPlayingStatus()));
        });
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this, status] { status->setText(TR("Állapot: %1").arg(main_->nowPlayingStatus())); });
        t->start(1000);
        status->setText(TR("Állapot: %1").arg(main_->nowPlayingStatus()));
    }

    root = newPage(TR("Adatbázis"));
    {
        // Az adatbázis (hangtár, playlistek, ütemező, mixek, beállítások) exportálása és importálása.
        auto *cap = new QLabel(TR("ADATBÁZIS"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *about = new QLabel(TR("Az Éter mindent egy SQLite adatbázisban tárol: a hangtárat (a fájlok hivatkozásait és "
                                    "adatait), a kategóriákat, a playlisteket, az ütemezőt, a mentett mixeket, a cartwallt és a "
                                    "beállításokat. A hangfájlok nincsenek benne. Az exporttal ebből készíthetsz mentést, vagy "
                                    "átviheted egy másik gépre; az importtal visszatöltheted."));
        about->setObjectName("small");
        about->setWordWrap(true);
        root->addWidget(about);
        auto *summary = new QLabel(main_->databaseSummary());
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        summary->setWordWrap(true);
        root->addWidget(summary);
        auto *row = new QHBoxLayout;
        auto *exportBtn = new QPushButton(TR("Exportálás…"));
        exportBtn->setObjectName("dbExport");
        exportBtn->setToolTip(TR("Az adatbázis másolatát menti egy fájlba. Adás közben is használható."));
        auto *importBtn = new QPushButton(TR("Importálás…"));
        importBtn->setObjectName("dbImport");
        importBtn->setToolTip(TR("Egy exportált adatbázisra cseréli a mostanit (előtte biztonsági mentés készül), majd újraindul."));
        auto *openBtn = new QPushButton(TR("Mappa megnyitása"));
        row->addWidget(exportBtn);
        row->addWidget(importBtn);
        row->addWidget(openBtn);
        row->addStretch(1);
        root->addLayout(row);
        auto *notes = new QLabel(TR("Importálás: a program ellenőrzi, hogy a fájl az Éter ép adatbázisa-e, a mostaniról biztonsági "
                                    "mentést készít a „biztonsagi-mentesek” mappába, majd újraindul az importált adatbázissal. "
                                    "Régebbi verzióból származó adatbázist magától frissít; újabb verzióét nem fogadja el. "
                                    "Importálni csak akkor lehet, ha semmi sem szól.\n"
                                    "Ha a hangfájlok a másik gépen máshol vannak, a Hiányzó fájlok ellenőrzésével köthetők újra össze."));
        notes->setObjectName("small");
        notes->setWordWrap(true);
        root->addWidget(notes);
        root->addStretch(1);
        connect(exportBtn, &QPushButton::clicked, this, [this, summary] {
            main_->exportDatabase(this);
            summary->setText(main_->databaseSummary());
        });
        connect(importBtn, &QPushButton::clicked, this, [this] { main_->importDatabase(this); });
        connect(openBtn, &QPushButton::clicked, this, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(main_->database()->path()).absolutePath()));
        });
    }

    root = newPage(TR("Hálózat"));
    // Távoli szerkesztés: egy másik gépen futó Éter (szerkesztő módban) szerkesztheti ennek a playlistjét.
    auto *remCaption = new QLabel(TR("TÁVOLI SZERKESZTÉS"));
    remCaption->setObjectName("caption");
    root->addWidget(remCaption);
    auto *remRow = new QHBoxLayout;
    remRow->setSpacing(8);
    QSettings st;
    auto *remOn = new QCheckBox(TR("Engedélyezve"));
    remOn->setChecked(st.value("remote/enabled", false).toBool());
    auto *remPort = new QSpinBox;
    remPort->setRange(1024, 65535);
    remPort->setValue(st.value("remote/port", remote::kDefaultPort).toInt());
    auto *remPass = new QLineEdit;
    remPass->setEchoMode(QLineEdit::Password);
    remPass->setPlaceholderText(st.value("remote/key").toByteArray().isEmpty() ? TR("új jelszó (legalább 6 karakter)")
                                                                               : TR("új jelszó (üresen: marad a régi)"));
    auto *remApply = new QPushButton(TR("Alkalmazás"));
    remRow->addWidget(remOn);
    remRow->addWidget(new QLabel(TR("Port")));
    remRow->addWidget(remPort);
    remRow->addWidget(new QLabel(TR("Jelszó")));
    remRow->addWidget(remPass, 1);
    remRow->addWidget(remApply);
    root->addLayout(remRow);
    remoteState_ = new QLabel;
    remoteState_->setObjectName("small");
    remoteState_->setWordWrap(true);
    remoteState_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(remoteState_);
    connect(remApply, &QPushButton::clicked, this, [this, remOn, remPort, remPass] {
        QSettings s;
        const QString pass = remPass->text();
        if (remOn->isChecked() && pass.isEmpty() && remote::loadUsers().isEmpty()) {
            remoteState_->setText(TR("Adj meg jelszót: a szerkesztő ezzel csatlakozhat."));
            remPass->setFocus();
            return;
        }
        if (!pass.isEmpty() && pass.size() < 6) {
            remoteState_->setText(TR("A jelszó legalább 6 karakter legyen."));
            remPass->setFocus();
            return;
        }
        if (!pass.isEmpty()) {
            const QByteArray salt = remote::newSalt();
            s.setValue("remote/salt", salt.toBase64());
            s.setValue("remote/key", remote::deriveKey(salt, pass).toBase64());
            remPass->clear();
            remPass->setPlaceholderText(TR("új jelszó (üresen: marad a régi)"));
        }
        s.setValue("remote/enabled", remOn->isChecked());
        s.setValue("remote/port", remPort->value());
        main_->configureRemote();
        remoteState_->setText(TR("Állapot: %1").arg(main_->remoteStatus()));
    });
    remoteState_->setText(TR("Állapot: %1").arg(main_->remoteStatus())
                          + TR("\nA szerkesztő gépen: Fájl → Távoli szerkesztő, vagy az Éter indítása --editor kapcsolóval."));

    // Felhasználók: saját jelszó és jogok. Az alap jelszóval (felhasználónév nélkül) mindenhez van jog.
    root->addSpacing(6);
    auto *usersCap = new QLabel(TR("FELHASZNÁLÓK"));
    usersCap->setObjectName("caption");
    root->addWidget(usersCap);
    auto *usersNote = new QLabel(TR("A fenti alap jelszóval (felhasználónév nélkül) mindenhez van jog. Itt további "
                                    "felhasználók vehetők fel saját jelszóval és korlátozott jogokkal; a szerkesztőben a nevükkel lépnek be."));
    usersNote->setObjectName("small");
    usersNote->setWordWrap(true);
    root->addWidget(usersNote);
    auto *users = new QTableWidget(0, 2);
    users->setObjectName("remoteUsers");
    users->setHorizontalHeaderLabels({TR("Név"), TR("Jogok")});
    users->verticalHeader()->hide();
    users->setSelectionBehavior(QAbstractItemView::SelectRows);
    users->setSelectionMode(QAbstractItemView::SingleSelection);
    users->setEditTriggers(QAbstractItemView::NoEditTriggers);
    users->horizontalHeader()->setStretchLastSection(true);
    users->setColumnWidth(0, 160);
    users->setMinimumHeight(120);
    root->addWidget(users, 1);
    auto fillUsers = [users] {
        users->setRowCount(0);
        for (const remote::User &u : remote::loadUsers()) {
            if (u.name.isEmpty()) continue;
            QStringList names;
            for (const QString &p : u.perms) names << remote::permName(p);
            const int r = users->rowCount();
            users->insertRow(r);
            users->setItem(r, 0, new QTableWidgetItem(u.name));
            users->setItem(r, 1, new QTableWidgetItem(names.isEmpty() ? TR("csak megtekintés") : names.join(QStringLiteral(", "))));
        }
    };
    fillUsers();
    auto *ub = new QHBoxLayout;
    auto *addUser = new QPushButton(TR("Új felhasználó…"));
    auto *editUser = new QPushButton(TR("Szerkesztés…"));
    auto *delUser = new QPushButton(TR("Törlés"));
    ub->addWidget(addUser);
    ub->addWidget(editUser);
    ub->addWidget(delUser);
    ub->addStretch(1);
    root->addLayout(ub);
    // Egy felhasználó szerkesztése (name üres: új).
    auto editOne = [this, fillUsers](const QString &name) {
        QVector<remote::User> all = remote::loadUsers();
        int index = -1;
        for (int i = 0; i < all.size(); ++i)
            if (!name.isEmpty() && all[i].name == name) index = i;
        QDialog dlg(this);
        dlg.setWindowTitle(name.isEmpty() ? TR("Új felhasználó") : TR("Felhasználó: %1").arg(name));
        auto *v = new QVBoxLayout(&dlg);
        auto *f = new QFormLayout;
        auto *nameEdit = new QLineEdit(name);
        nameEdit->setMaxLength(40);
        auto *passEdit = new QLineEdit;
        passEdit->setEchoMode(QLineEdit::Password);
        passEdit->setPlaceholderText(name.isEmpty() ? TR("legalább 6 karakter") : TR("üresen: marad a régi"));
        f->addRow(TR("Név"), nameEdit);
        f->addRow(TR("Jelszó"), passEdit);
        v->addLayout(f);
        v->addWidget(new QLabel(TR("Jogok:")));
        QVector<QCheckBox *> boxes;
        for (const QString &p : remote::allPerms()) {
            auto *b = new QCheckBox(remote::permName(p));
            b->setChecked(index >= 0 ? all[index].perms.contains(p)
                                     : (p != QLatin1String("delete") && p != QLatin1String("upload") && p != QLatin1String("backup")));
            b->setProperty("perm", p);
            v->addWidget(b);
            boxes << b;
        }
        auto *err = new QLabel;
        err->setObjectName("small");
        v->addWidget(err);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(TR("Mentés"));
        bb->button(QDialogButtonBox::Cancel)->setText(TR("Mégse"));
        v->addWidget(bb);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        connect(bb, &QDialogButtonBox::accepted, &dlg, [&] {
            const QString n = nameEdit->text().trimmed();
            if (n.isEmpty()) { err->setText(TR("Adj meg nevet.")); return; }
            for (int i = 0; i < all.size(); ++i)
                if (i != index && all[i].name.compare(n, Qt::CaseInsensitive) == 0) { err->setText(TR("Ilyen nevű felhasználó már van.")); return; }
            if ((index < 0 || !passEdit->text().isEmpty()) && passEdit->text().size() < 6) { err->setText(TR("A jelszó legalább 6 karakter legyen.")); return; }
            dlg.accept();
        });
        if (dlg.exec() != QDialog::Accepted) return;
        remote::User u = index >= 0 ? all[index] : remote::User();
        u.name = nameEdit->text().trimmed();
        if (!passEdit->text().isEmpty()) {
            u.salt = remote::newSalt();
            u.key = remote::deriveKey(u.salt, passEdit->text());
        }
        u.perms.clear();
        for (QCheckBox *b : boxes)
            if (b->isChecked()) u.perms << b->property("perm").toString();
        if (index >= 0) all[index] = u;
        else all.push_back(u);
        remote::saveUsers(all);
        main_->configureRemote();
        fillUsers();
    };
    connect(addUser, &QPushButton::clicked, this, [editOne] { editOne(QString()); });
    connect(editUser, &QPushButton::clicked, this, [users, editOne] {
        if (users->currentRow() >= 0) editOne(users->item(users->currentRow(), 0)->text());
    });
    connect(users, &QTableWidget::cellDoubleClicked, this, [users, editOne](int r) { editOne(users->item(r, 0)->text()); });
    connect(delUser, &QPushButton::clicked, this, [this, users, fillUsers] {
        if (users->currentRow() < 0) return;
        const QString name = users->item(users->currentRow(), 0)->text();
        if (QMessageBox::question(this, TR("Felhasználó törlése"), TR("Törlöd: %1?").arg(name), QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        QVector<remote::User> all = remote::loadUsers();
        all.erase(std::remove_if(all.begin(), all.end(), [&](const remote::User &u) { return u.name == name; }), all.end());
        remote::saveUsers(all);
        main_->configureRemote();
        fillUsers();
    });

    auto *backupNote = new QLabel(TR("Tartalék playout: a tartalék gépen Fájl → Újraindítás tartalék playoutként (vagy az Éter "
                                     "indítása --backup kapcsolóval), és add meg ennek a gépnek a címét. A belépéshez „tartalék playout” "
                                     "jog kell (az alap jelszóval megvan)."));
    backupNote->setObjectName("small");
    backupNote->setWordWrap(true);
    root->addWidget(backupNote);

    // A szerkesztőkből feltöltött fájlok helye a playout gépen.
    auto *upRow = new QHBoxLayout;
    auto *upDir = new QLineEdit(QSettings().value("remote/uploadDir").toString());
    upDir->setPlaceholderText(MainWindow::uploadFolder());
    auto *upBrowse = new QPushButton(TR("Tallózás…"));
    upRow->addWidget(new QLabel(TR("Feltöltések mappája")));
    upRow->addWidget(upDir, 1);
    upRow->addWidget(upBrowse);
    root->addLayout(upRow);
    connect(upDir, &QLineEdit::editingFinished, this, [upDir] { QSettings().setValue("remote/uploadDir", QDir::fromNativeSeparators(upDir->text().trimmed())); });
    connect(upBrowse, &QPushButton::clicked, this, [this, upDir] {
        const QString d = QFileDialog::getExistingDirectory(this, TR("A feltöltések mappája"), MainWindow::uploadFolder());
        if (d.isEmpty()) return;
        upDir->setText(QDir::toNativeSeparators(d));
        QSettings().setValue("remote/uploadDir", QDir::fromNativeSeparators(d));
    });

    auto *bottom = new QHBoxLayout;
    count_ = new QLabel;
    count_->setObjectName("small");
    auto *close = new QPushButton(TR("Bezárás"));
    close->setProperty("accent", true);
    close->setMinimumWidth(96);
    close->setDefault(true);
    bottom->addWidget(count_);
    bottom->addStretch(1);
    bottom->addWidget(close);
    outer->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(refresh, &QPushButton::clicked, main_, &MainWindow::refreshDeviceLists);
    connect(main_, &MainWindow::devicesChanged, this, &SettingsDialog::reload);
    connect(backend_, &QComboBox::activated, this, [this](int index) {
        main_->changeBackend(backend_->itemData(index).toString());
        reload();
    });

    // A „szól – zárolva" jelzés frissítése
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        const bool any = main_->anythingPlaying();
        backend_->setEnabled(!any);
        for (int ch = 0; ch < kRows; ++ch) {
            const bool busy = main_->channelBusy(channelOf(ch));
            device_[ch]->setEnabled(!busy);
            state_[ch]->setText(busy ? TR("szól – zárolva") : QString());
        }
    });
    timer->start(250);

    reload();
}

void SettingsDialog::reload()
{
    backend_->setCurrentIndex(std::max(0, backend_->findData(main_->backendSetting())));
    const QStringList names = main_->deviceNames();
    for (int ch = 0; ch < kRows; ++ch) {
        device_[ch]->blockSignals(true);
        device_[ch]->clear();
        if (channelOf(ch) == MainWindow::kCartRoute)
            device_[ch]->addItem(TR("Az A lejátszó kimenetén (adás)"), QStringLiteral("@player1"));
        device_[ch]->addItem(TR("Alapértelmezett kimenet"), QString());
        for (const QString &n : names) device_[ch]->addItem(n, n);
        device_[ch]->setCurrentIndex(std::max(0, device_[ch]->findData(main_->routeOf(channelOf(ch)))));
        device_[ch]->blockSignals(false);
    }
    count_->setText(TR("%1 kimenet észlelve · %2")
                        .arg(names.size())
                        .arg(QString::fromStdString(main_->engine()->backendName())));
}

int SettingsDialog::channelOf(int row)
{
    static const int ch[kRows] = {eter::Engine::Player1, eter::Engine::Player2, eter::Engine::Player3, eter::Engine::Pfl,
                                  MainWindow::kCartRoute};
    return ch[row];
}
