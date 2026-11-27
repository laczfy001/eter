#include "i18n.h"
#include "Widgets.h"
#include "SettingsDialog.h"
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QLocale>
#include <cstdio>
#include <QJsonObject>
#include <QInputDialog>
#include "db/ServerSync.h"
#include "net/ArtistInfo.h"
#include <functional>
#include <QFile>
#include <QApplication>
#include <QSqlDatabase>
#include <QProgressDialog>
#include "Theme.h"
#include "MixPreview.h"
#include "midi/MidiControl.h"
#include <QToolButton>
#include <QPainter>
#include <QColorDialog>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include "net/Remote.h"
#include "MainWindow.h"
#include "audio/ProgramLogger.h"
#include "core/NowPlaying.h"
#include "core/LibraryAnalyzer.h"
#include "core/SmartMixConfig.h"
#include "db/Database.h"

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
#include <QScrollArea>
#include <QListWidget>
#include "net/WebSync.h"
#include "net/StreamFetcher.h"

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
    // Minden lap görgethető (kisebb képernyőn vagy hosszabb lapnál ne nyomódjanak össze a sorok).
    auto newPage = [tabs](const QString &title) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *page = new QWidget;
        page->setObjectName("settingsPage");
        auto *l = new QVBoxLayout(page);
        l->setContentsMargins(14, 14, 14, 10);
        l->setSpacing(10);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
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

    {
        // Élő stream: meddig próbál újrakapcsolódni adásban, mielőtt továbblép a következő elemre.
        auto *cap = new QLabel(TR("ÉLŐ STREAM"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *row = new QHBoxLayout;
        auto *giveUp = new QSpinBox;
        giveUp->setObjectName("streamGiveUp");
        giveUp->setRange(3, 120);
        giveUp->setSuffix(TR(" mp"));
        giveUp->setValue(QtStreamInput::giveUpMs() / 1000);
        row->addWidget(new QLabel(TR("Ha a stream adásban megszakad, ennyi ideig próbál újrakapcsolódni:")));
        row->addWidget(giveUp);
        row->addStretch(1);
        root->addLayout(row);
        auto *sn = new QLabel(TR("Utána a playlist következő eleme indul. Az élő streamet a hangtárban az Importálás → Élő "
                                 "stream… menüponttal lehet felvenni."));
        sn->setObjectName("small");
        sn->setWordWrap(true);
        root->addWidget(sn);
        connect(giveUp, &QSpinBox::valueChanged, this, [](int s) {
            QtStreamInput::setGiveUpMs(s * 1000);
            QSettings().setValue("stream/giveUpS", s);
        });
    }

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

    root = newPage(TR("Megjelenés"));
    {
        // Saját színek és a felület mérete; gépenként tárolva.
        auto cap = [&](const QString &t) {
            auto *c = new QLabel(t);
            c->setObjectName("caption");
            root->addWidget(c);
        };
        auto small = [&](const QString &t) {
            auto *l = new QLabel(t);
            l->setObjectName("small");
            l->setWordWrap(true);
            root->addWidget(l);
        };
        // Színválasztó gomb: a minta a gomb ikonja, a név a felirata.
        auto swatch = [](const QColor &c) {
            QPixmap pm(28, 16);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(theme::c("#0c0d10"));
            p.setBrush(c);
            p.drawRoundedRect(QRectF(0.5, 0.5, 27, 15), 3, 3);
            return QIcon(pm);
        };
        auto colorButton = [swatch](const QString &key, const QColor &def, const QString &name) {
            auto *b = new QPushButton;
            b->setObjectName(QStringLiteral("color_") + key);
            b->setIconSize(QSize(28, 16));
            b->setIcon(swatch(def));
            b->setText(QStringLiteral("  %1").arg(def.name().toUpper()));
            b->setAccessibleName(name);
            b->setMinimumWidth(140);
            return b;
        };
        auto setButton = [swatch](QPushButton *b, const QColor &c) {
            b->setIcon(swatch(c));
            b->setText(QStringLiteral("  %1").arg(c.name().toUpper()));
        };

        // ---- Színséma: választás, mentés név szerint, import és export ----
        cap(TR("SZÍNSÉMA"));
        small(TR("Válassz egy sémát, vagy szabd testre az alapszíneket: a változás azonnal látszik az egész programban. "
                 "A saját sémát név szerint mentheted, és fájlba exportálva másik gépre is átviheted."));
        auto *schemeRow = new QHBoxLayout;
        schemeRow->setSpacing(6);
        auto *schemeCombo = new QComboBox;
        schemeCombo->setObjectName("colorScheme");
        schemeCombo->setMinimumWidth(240);
        schemeCombo->setAccessibleName(TR("Színséma"));
        auto *modified = new QLabel;
        modified->setObjectName("small");
        modified->setWordWrap(true);
        schemeRow->addWidget(schemeCombo);
        schemeRow->addWidget(modified, 1);
        root->addLayout(schemeRow);
        auto *schemeBtns = new QHBoxLayout;
        schemeBtns->setSpacing(6);
        auto mkBtn = [&](const QString &text, const char *name) {
            auto *b = new QPushButton(text);
            b->setObjectName(QLatin1String(name));
            schemeBtns->addWidget(b);
            return b;
        };
        auto *saveBtn = mkBtn(TR("Mentés"), "schemeSave");
        auto *saveAsBtn = mkBtn(TR("Mentés másként…"), "schemeSaveAs");
        auto *renameBtn = mkBtn(TR("Átnevezés…"), "schemeRename");
        auto *deleteBtn = mkBtn(TR("Törlés"), "schemeDelete");
        schemeBtns->addStretch(1);
        auto *importBtn = mkBtn(TR("Importálás…"), "schemeImport");
        auto *exportBtn = mkBtn(TR("Exportálás…"), "schemeExport");
        root->addLayout(schemeBtns);

        cap(TR("ALAPSZÍNEK"));
        small(TR("A köztes árnyalatokat (kijelölés, sávok, halványabb keretek) a program ezekből számolja ki."));
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(6);
        QHash<QString, QPushButton *> roleButtons;
        int n = 0;
        for (const theme::Role &r : theme::roles()) {
            if (r.key.startsWith(QLatin1String("player"))) continue;
            auto *l = new QLabel(i18n::text(r.label));
            l->setToolTip(i18n::text(r.hint));
            auto *b = colorButton(r.key, r.def, i18n::text(r.label));
            b->setToolTip(i18n::text(r.hint));
            const int row = n % 5, col = (n / 5) * 2;
            grid->addWidget(l, row, col);
            grid->addWidget(b, row, col + 1);
            roleButtons[r.key] = b;
            ++n;
        }
        grid->setColumnMinimumWidth(2, 140);
        grid->setColumnStretch(4, 1);
        root->addLayout(grid);
        auto *contrastWarn = new QLabel;
        contrastWarn->setObjectName("contrastWarn");
        contrastWarn->setWordWrap(true);
        root->addWidget(contrastWarn);

        auto *accRow = new QHBoxLayout;
        accRow->setSpacing(6);
        auto *accLabel = new QLabel(TR("Gyors kiemelő szín:"));
        accLabel->setObjectName("small");
        accRow->addWidget(accLabel);
        const std::pair<const char *, const char *> presets[] = {
            {QT_TR_NOOP("Borostyán"), "#f5a524"}, {QT_TR_NOOP("Korall"), "#ff7a59"}, {QT_TR_NOOP("Piros"), "#e5484d"},
            {QT_TR_NOOP("Rózsaszín"), "#f472b6"}, {QT_TR_NOOP("Lila"), "#a78bfa"},   {QT_TR_NOOP("Kék"), "#5cb0ff"},
            {QT_TR_NOOP("Türkiz"), "#3fd0b5"},    {QT_TR_NOOP("Zöld"), "#45d483"},   {QT_TR_NOOP("Citrom"), "#e2d34a"}};
        QVector<QPair<QToolButton *, QColor>> presetButtons;
        for (const auto &[label, hexv] : presets) {
            auto *t = new QToolButton;
            t->setIcon(swatch(QColor(hexv)));
            t->setIconSize(QSize(28, 16));
            t->setToolTip(i18n::text(label));
            t->setAccessibleName(i18n::text(label));
            t->setAutoRaise(true);
            accRow->addWidget(t);
            presetButtons << qMakePair(t, QColor(hexv));
        }
        accRow->addStretch(1);
        root->addLayout(accRow);

        cap(TR("A LEJÁTSZÓK SZÍNEI"));
        small(TR("Az A, B és C lejátszó betűjele, a mini mixer, a hullámforma és a playlist sorainak jelölése."));
        auto *pl = new QFormLayout;
        pl->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        for (int i = 0; i < 3; ++i) {
            const QString letter = QString(QChar('A' + i));
            const QString key = QStringLiteral("player%1").arg(i);
            auto *b = colorButton(key, theme::defaultPlayerColor(i), TR("%1 lejátszó színe").arg(letter));
            pl->addRow(TR("%1 lejátszó").arg(letter), b);
            roleButtons[key] = b;
        }
        root->addLayout(pl);
        auto *revertBtn = new QPushButton(TR("Visszaállítás"));
        revertBtn->setObjectName("schemeRevert");
        revertBtn->setToolTip(TR("A kiválasztott séma mentett színei."));
        auto *rr = new QHBoxLayout;
        rr->addWidget(revertBtn);
        rr->addStretch(1);
        root->addLayout(rr);

        // Az oldal állapota a mentett beállításokból.
        auto refresh = [=] {
            const theme::Colors cur = theme::currentColors();
            for (auto it = roleButtons.cbegin(); it != roleButtons.cend(); ++it) setButton(it.value(), cur.value(it.key()));
            const QString id = theme::currentSchemeId();
            const theme::Scheme sel = theme::scheme(id);
            {
                const QSignalBlocker block(schemeCombo);
                schemeCombo->clear();
                if (sel.id.isEmpty()) schemeCombo->addItem(TR("Mentetlen saját színek"), QString());
                bool userSep = false;
                for (const theme::Scheme &s : theme::schemes()) {
                    if (!s.builtIn && !userSep) {
                        schemeCombo->insertSeparator(schemeCombo->count());
                        userSep = true;
                    }
                    schemeCombo->addItem(swatch(s.colors.value(QStringLiteral("accent"))), s.name, s.id);
                }
                schemeCombo->setCurrentIndex(std::max(0, schemeCombo->findData(sel.id.isEmpty() ? QString() : id)));
            }
            const bool mod = theme::currentModified();
            const bool user = id.startsWith(QLatin1String("user:")) && !sel.id.isEmpty();
            modified->setText(sel.id.isEmpty() ? TR("A színek nincsenek sémaként mentve: a „Mentés másként” gombbal mentheted el őket.")
                              : mod        ? TR("Módosítva: a változtatások már látszanak, de a séma még a régi színeket tárolja.")
                                           : (sel.builtIn ? TR("Beépített séma: a változtatásait saját sémaként mentheted.") : QString()));
            saveBtn->setEnabled(user && mod);
            renameBtn->setEnabled(user);
            deleteBtn->setEnabled(user);
            revertBtn->setEnabled(mod && !sel.id.isEmpty());
            // Olvashatóság: a szöveg és a háttér, illetve a panel kontrasztja.
            const double c1 = theme::contrast(cur.value(QStringLiteral("text")), cur.value(QStringLiteral("bg")));
            const double c2 = theme::contrast(cur.value(QStringLiteral("text")), cur.value(QStringLiteral("panel")));
            const double c3 = theme::contrast(cur.value(QStringLiteral("muted")), cur.value(QStringLiteral("panel")));
            const double worst = std::min({c1, c2});
            if (worst < 4.5)
                contrastWarn->setText(TR("Figyelem: a szöveg nehezen olvasható ezen a háttéren (kontraszt %1:1, legalább 4,5:1 ajánlott).")
                                          .arg(QLocale().toString(worst, 'f', 1)));
            else if (c3 < 3.0)
                contrastWarn->setText(TR("Figyelem: a halvány szöveg nehezen olvasható a paneleken (kontraszt %1:1, legalább 3:1 ajánlott).")
                                          .arg(QLocale().toString(c3, 'f', 1)));
            else
                contrastWarn->clear();
            contrastWarn->setVisible(!contrastWarn->text().isEmpty());
            contrastWarn->setStyleSheet(QStringLiteral("color: #ffb4ae;"));
        };
        auto changeRole = [refresh](const QString &key, const QColor &c) {
            theme::Colors cur = theme::currentColors();
            cur[key] = c;
            theme::setCurrentColors(cur, QString());
            refresh();
        };
        for (auto it = roleButtons.cbegin(); it != roleButtons.cend(); ++it) {
            QPushButton *b = it.value();
            const QString key = it.key();
            connect(b, &QPushButton::clicked, this, [this, b, key, changeRole] {
                const QColor cur = theme::currentColors().value(key);
                QColor c;
                if (qEnvironmentVariableIsSet("ETER_PICK_COLOR")) c = QColor(qEnvironmentVariable("ETER_PICK_COLOR"));   // tesztekhez
                else c = QColorDialog::getColor(cur, this, b->accessibleName());
                if (c.isValid()) changeRole(key, c);
            });
        }
        for (const auto &[t, c] : presetButtons) {
            const QColor col = c;
            connect(t, &QToolButton::clicked, this, [changeRole, col] { changeRole(QStringLiteral("accent"), col); });
        }
        connect(schemeCombo, &QComboBox::activated, this, [this, schemeCombo, refresh] {
            const QString id = schemeCombo->currentData().toString();
            const theme::Scheme s = theme::scheme(id);
            if (s.id.isEmpty() || (s.id == theme::currentSchemeId() && !theme::currentModified())) return refresh();
            if (theme::currentModified() && !qEnvironmentVariableIsSet("ETER_AUTOCONFIRM")
                && QMessageBox::question(this, TR("Színséma"), TR("A jelenlegi színek nincsenek elmentve, és a váltással elvesznek. Folytatod?"))
                       != QMessageBox::Yes)
                return refresh();
            theme::setCurrentColors(s.colors, s.id);
            refresh();
        });
        // Név bekérése (tesztekhez: ETER_SCHEME_NAME).
        auto askName = [this](const QString &title, const QString &def) {
            if (qEnvironmentVariableIsSet("ETER_SCHEME_NAME")) return qEnvironmentVariable("ETER_SCHEME_NAME").trimmed();
            bool ok = false;
            const QString n = QInputDialog::getText(this, title, TR("A séma neve:"), QLineEdit::Normal, def, &ok).trimmed();
            return ok ? n : QString();
        };
        connect(saveBtn, &QPushButton::clicked, this, [refresh] {
            const QString id = theme::currentSchemeId();
            if (!id.startsWith(QLatin1String("user:"))) return;
            theme::setCurrentColors(theme::currentColors(), theme::saveScheme(id.mid(5), theme::currentColors()));
            refresh();
        });
        connect(saveAsBtn, &QPushButton::clicked, this, [this, askName, refresh] {
            const theme::Scheme sel = theme::scheme(theme::currentSchemeId());
            const QString def = sel.builtIn || sel.id.isEmpty() ? theme::uniqueSchemeName(TR("Saját séma")) : sel.name;
            const QString name = askName(TR("Színséma mentése"), def);
            if (name.isEmpty()) return;
            if (theme::isBuiltInName(name)) {
                QMessageBox::warning(this, TR("Színséma"), TR("Ez egy beépített séma neve; válassz másikat."));
                return;
            }
            const QString id = QStringLiteral("user:") + name;
            if (!theme::scheme(id).id.isEmpty() && id != theme::currentSchemeId() && !qEnvironmentVariableIsSet("ETER_AUTOCONFIRM")
                && QMessageBox::question(this, TR("Színséma"), TR("Már van „%1” nevű séma. Felülírod?").arg(name)) != QMessageBox::Yes)
                return;
            theme::setCurrentColors(theme::currentColors(), theme::saveScheme(name, theme::currentColors()));
            refresh();
        });
        connect(renameBtn, &QPushButton::clicked, this, [this, askName, refresh] {
            const theme::Scheme sel = theme::scheme(theme::currentSchemeId());
            if (sel.builtIn || sel.id.isEmpty()) return;
            const QString name = askName(TR("Színséma átnevezése"), sel.name);
            if (name.isEmpty() || name == sel.name) return;
            if (theme::renameScheme(sel.id, name).isEmpty())
                QMessageBox::warning(this, TR("Színséma"), TR("Ilyen nevű séma már van, vagy beépített séma neve."));
            refresh();
        });
        connect(deleteBtn, &QPushButton::clicked, this, [this, refresh] {
            const theme::Scheme sel = theme::scheme(theme::currentSchemeId());
            if (sel.builtIn || sel.id.isEmpty()) return;
            if (!qEnvironmentVariableIsSet("ETER_AUTOCONFIRM")
                && QMessageBox::question(this, TR("Színséma"), TR("Törlöd a(z) „%1” sémát? A jelenlegi színek megmaradnak.").arg(sel.name))
                       != QMessageBox::Yes)
                return;
            theme::removeScheme(sel.id);
            refresh();
        });
        connect(revertBtn, &QPushButton::clicked, this, [refresh] {
            const theme::Scheme sel = theme::scheme(theme::currentSchemeId());
            if (!sel.id.isEmpty()) theme::setCurrentColors(sel.colors, sel.id);
            refresh();
        });
        connect(exportBtn, &QPushButton::clicked, this, [this] {
            const theme::Scheme sel = theme::scheme(theme::currentSchemeId());
            const QString name = sel.id.isEmpty() ? TR("Saját séma") : sel.name;
            QString path = qEnvironmentVariable("ETER_SCHEME_FILE");   // tesztekhez
            if (path.isEmpty()) {
                QString base = name;
                base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
                path = QFileDialog::getSaveFileName(this, TR("Színséma exportálása"), QDir::home().filePath(base + QStringLiteral(".json")),
                                                    TR("Éter színséma (*.json)"));
            }
            if (path.isEmpty()) return;
            if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || f.write(QJsonDocument(theme::schemeToJson(name, theme::currentColors())).toJson(QJsonDocument::Indented)) < 0) {
                QMessageBox::warning(this, TR("Színséma"), TR("Nem sikerült menteni: %1").arg(f.errorString()));
                return;
            }
            std::printf("színséma exportálva: %s\n", qPrintable(path));
        });
        connect(importBtn, &QPushButton::clicked, this, [this, refresh] {
            QStringList paths;
            if (qEnvironmentVariableIsSet("ETER_SCHEME_FILE")) paths << qEnvironmentVariable("ETER_SCHEME_FILE");
            else paths = QFileDialog::getOpenFileNames(this, TR("Színséma importálása"), QDir::homePath(), TR("Éter színséma (*.json)"));
            QString lastId;
            QStringList errors;
            for (const QString &p : paths) {
                QFile f(p);
                QString name, err;
                theme::Colors cols;
                if (!f.open(QIODevice::ReadOnly) || f.size() > 256 * 1024) {
                    errors << QStringLiteral("%1: %2").arg(QFileInfo(p).fileName(), f.isOpen() ? TR("túl nagy fájl") : f.errorString());
                    continue;
                }
                if (!theme::schemeFromJson(f.readAll(), &name, &cols, &err)) {
                    errors << QStringLiteral("%1: %2").arg(QFileInfo(p).fileName(), err);
                    continue;
                }
                if (name.isEmpty()) name = QFileInfo(p).completeBaseName();
                // Ugyanilyen nevű és színű séma már van: nem kell kétszer.
                const theme::Scheme same = theme::scheme(QStringLiteral("user:") + name);
                if (!same.id.isEmpty() && same.colors == cols) lastId = same.id;
                else lastId = theme::saveScheme(theme::uniqueSchemeName(name), cols);
            }
            if (!lastId.isEmpty()) theme::setCurrentColors(theme::scheme(lastId).colors, lastId);
            refresh();
            if (!errors.isEmpty()) {
                if (qEnvironmentVariableIsSet("ETER_SCHEME_FILE")) std::printf("import hiba: %s\n", qPrintable(errors.join(" | ")));
                else QMessageBox::warning(this, TR("Színséma importálása"), errors.join(QLatin1Char('\n')));
            }
        });
        refresh();

        cap(TR("ELRENDEZÉS"));
        auto *strip = new QCheckBox(TR("A, B, C lejátszó sávja a fő lejátszó alatt"));
        strip->setObjectName("playerStrip");
        strip->setChecked(MainWindow::playerStripVisible());
        root->addWidget(strip);
        small(TR("A sáv a három lejátszó állapotát (adásban, készenlétben, betöltve), a címet, a hátralévő időt és a "
                 "szóló voice tracket mutatja; kattintásra az a lejátszó látszik fölötte. Kikapcsolva a fő lejátszó "
                 "magasabb lesz. Az Ablakok menüből is ki- és bekapcsolható."));
        connect(strip, &QCheckBox::toggled, this, [this](bool on) { main_->setPlayerStripVisible(on); });

        auto *docking = new QCheckBox(TR("Önálló panelek (áthelyezhető, kiemelhető ablakok)"));
        docking->setObjectName("dockingEnabled");
        docking->setChecked(MainWindow::dockingSetting());
        root->addWidget(docking);
        small(TR("Bekapcsolva a főablak részei (lejátszó, adásvezérlés, óra, keverő, playlist, hangtár, cartwall, "
                 "számkérések) a fejlécüknél fogva áthelyezhetők, kiemelhetők önálló ablakká, és az elrendezés "
                 "menthető. Kikapcsolva minden panel fixen az eredeti elrendezésben van; a saját elrendezés megmarad, "
                 "és visszakapcsoláskor visszajön. Az Ablakok menüből is ki- és bekapcsolható."));
        connect(docking, &QCheckBox::toggled, this, [this](bool on) { main_->setDockingEnabled(on); });

        cap(TR("A FELÜLET MÉRETE"));
        small(TR("Minden felirat, gomb és ablak ennyivel nagyobb. A program következő indításakor lép életbe."));
        auto *scale = new QComboBox;
        scale->setObjectName("uiScale");
        for (int pct : {100, 110, 125, 140, 150, 175, 200}) scale->addItem(QStringLiteral("%1 %").arg(pct), pct);
        scale->setCurrentIndex(std::max(0, scale->findData(theme::uiScalePercent())));
        auto *sf = new QFormLayout;
        sf->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        sf->addRow(TR("Méret"), scale);
        root->addLayout(sf);
        connect(scale, &QComboBox::activated, this, [scale] { QSettings().setValue(QStringLiteral("ui/scale"), scale->currentData().toInt()); });
        root->addStretch(1);
    }

    root = newPage(TR("MIDI"));
    if (MidiControl *mc = MidiControl::instance()) {
        auto cap = [&](const QString &t) {
            auto *c = new QLabel(t);
            c->setObjectName("caption");
            root->addWidget(c);
        };
        auto small = [&](const QString &t) {
            auto *l = new QLabel(t);
            l->setObjectName("small");
            l->setWordWrap(true);
            root->addWidget(l);
            return l;
        };
        cap(TR("MIDI-ESZKÖZ"));
        small(TR("Egy MIDI-képes keverőpult (például RØDECaster Pro) vagy kontroller gombjaival és fadereivel kezelheted a "
                 "programot. Itt azok az eszközök látszanak, amelyeket a rendszer MIDI-eszközként ismer (Windows alatt az "
                 "Eszközkezelőben is látszanak). Ha az eszköz nem küld üzenetet, nézd meg a saját beállításaiban, hogy a "
                 "MIDI-vezérlés be van-e kapcsolva."));
        auto *devRow = new QHBoxLayout;
        devRow->setSpacing(6);
        auto *dev = new QComboBox;
        dev->setObjectName("midiDevice");
        dev->setMinimumWidth(280);
        auto *refreshDev = new QPushButton(TR("Frissítés"));
        refreshDev->setObjectName("midiRefresh");
        devRow->addWidget(dev, 1);
        devRow->addWidget(refreshDev);
        root->addLayout(devRow);
        auto *status = small(QString());
        status->setObjectName("midiStatus");
        auto *monitor = new QLabel(TR("Utolsó üzenet: —"));
        monitor->setObjectName("mono");
        monitor->setToolTip(TR("Az eszközről érkező utolsó üzenet: így látszik, hogy a gombok és faderek mit küldenek"));
        root->addWidget(monitor);

        cap(TR("VEZÉRLÉS"));
        small(TR("Tanuló mód: kattints a művelet „Tanítás” gombjára, majd nyomd meg a gombot vagy mozdítsd meg a fadert az "
                 "eszközön. Gombhoz: indítás és leállítás, következő, kiúsztatás, cartwall. Faderhez: hangerő (a fader teteje "
                 "0 dB) vagy fader-indítás (a fader felhúzására indul, lehúzására megáll a lejátszó). Egy fader lehet egyszerre "
                 "hangerő és fader-indítás is."));
        auto *table = new QTableWidget(0, 4);
        table->setObjectName("midiBindings");
        table->setHorizontalHeaderLabels({TR("Művelet"), TR("Vezérlő"), QString(), QString()});
        table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(34);
        table->setShowGrid(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setFocusPolicy(Qt::NoFocus);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table->setMinimumHeight(320);
        root->addWidget(table, 1);
        auto *learnNote = small(QString());
        learnNote->setObjectName("midiLearnNote");

        auto *opts = new QHBoxLayout;
        opts->setSpacing(8);
        auto *thrLabel = new QLabel(TR("Fader-indítás: nyitottnak számít efölött"));
        auto *thr = new QSpinBox;
        thr->setObjectName("midiFaderThreshold");
        thr->setRange(1, 50);
        thr->setSuffix(QStringLiteral(" %"));
        thr->setValue(mc->faderStartThreshold());
        auto *clearAll = new QPushButton(TR("Összes hozzárendelés törlése"));
        clearAll->setObjectName("midiClearAll");
        opts->addWidget(thrLabel);
        opts->addWidget(thr);
        opts->addStretch(1);
        opts->addWidget(clearAll);
        root->addLayout(opts);

        auto fillDevices = [=] {
            const QSignalBlocker b(dev);
            dev->clear();
            dev->addItem(TR("Nincs (kikapcsolva)"), QString());
            bool found = mc->deviceId().isEmpty();
            for (const MidiControl::PortInfo &p : MidiControl::ports()) {
                dev->addItem(p.name, p.id);
                dev->setItemData(dev->count() - 1, p.name, Qt::UserRole + 1);
                if (p.id == mc->deviceId() || (!mc->deviceName().isEmpty() && p.name == mc->deviceName())) found = true;
            }
            if (!found) {
                dev->addItem(TR("%1 (nincs csatlakoztatva)").arg(mc->deviceName().isEmpty() ? mc->deviceId() : mc->deviceName()), mc->deviceId());
                dev->setItemData(dev->count() - 1, mc->deviceName(), Qt::UserRole + 1);
            }
            int idx = dev->findData(mc->deviceId());
            if (idx < 0 && !mc->deviceName().isEmpty()) idx = dev->findText(mc->deviceName());
            dev->setCurrentIndex(std::max(0, idx));
        };
        // A táblázat sorai egyszer készülnek; a frissítés csak a feliratokat és az állapotot írja át (a gombok a saját
        // kattintásuk közben nem törlődhetnek).
        const QVector<MidiControl::Action> acts = MidiControl::actions();
        table->setRowCount(int(acts.size()));
        QVector<QPushButton *> learnBtns, clearBtns;
        for (int r = 0; r < acts.size(); ++r) {
            const MidiControl::Action &a = acts[r];
            auto *name = new QTableWidgetItem(a.label);
            name->setToolTip(a.kind == MidiControl::Button ? TR("Gomb") : TR("Fader"));
            table->setItem(r, 0, name);
            table->setItem(r, 1, new QTableWidgetItem);
            auto *learn = new QPushButton(TR("Tanítás"));
            learn->setObjectName(QStringLiteral("midiLearn_") + a.id);
            // A „Mozdítsd meg…” felirat is elférjen (az oszlop a gomb méretéhez igazodik).
            learn->setMinimumWidth(learn->fontMetrics().horizontalAdvance(TR("Mozdítsd meg…")) + 32);
            learn->setToolTip(a.kind == MidiControl::Button ? TR("Kattints rá, majd nyomd meg a gombot az eszközön")
                                                            : TR("Kattints rá, majd mozdítsd meg a fadert az eszközön"));
            connect(learn, &QPushButton::clicked, this, [mc, learnNote, id = a.id] {
                if (mc->learning() == id) {
                    mc->cancelLearn();
                    learnNote->setText(TR("A tanítás megszakadt."));
                } else {
                    mc->startLearn(id);
                }
            });
            table->setCellWidget(r, 2, learn);
            auto *clr = new QPushButton(TR("Törlés"));
            clr->setObjectName(QStringLiteral("midiClear_") + a.id);
            connect(clr, &QPushButton::clicked, this, [mc, id = a.id] { mc->setBinding(id, QString()); });
            table->setCellWidget(r, 3, clr);
            learnBtns << learn;
            clearBtns << clr;
        }
        auto fillTable = [=] {
            for (int r = 0; r < acts.size(); ++r) {
                const MidiControl::Action &a = acts[r];
                const QString key = mc->binding(a.id);
                QTableWidgetItem *ctl = table->item(r, 1);
                ctl->setText(key.isEmpty() ? QStringLiteral("—") : MidiControl::describe(key));
                ctl->setForeground(key.isEmpty() ? theme::muted : theme::text);
                const bool on = mc->learning() == a.id;
                QPushButton *learn = learnBtns[r];
                learn->setText(on ? TR("Mozdítsd meg…") : TR("Tanítás"));
                if (learn->property("active").toBool() != on) {
                    learn->setProperty("active", on);
                    learn->style()->unpolish(learn);
                    learn->style()->polish(learn);
                }
                clearBtns[r]->setEnabled(!key.isEmpty());
            }
            const QString l = mc->learning();
            if (!l.isEmpty())
                learnNote->setText((MidiControl::action(l).kind == MidiControl::Button
                                        ? TR("Tanítás: „%1” – nyomd meg a gombot az eszközön (a „Mozdítsd meg…” gombbal megszakítható).")
                                        : TR("Tanítás: „%1” – mozdítsd meg a fadert az eszközön (a „Mozdítsd meg…” gombbal megszakítható)."))
                                       .arg(MidiControl::action(l).label));
        };
        fillDevices();
        fillTable();
        status->setText(mc->statusText());
        connect(dev, &QComboBox::activated, this, [=] {
            mc->setDevice(dev->currentData().toString(), dev->currentData().toString().isEmpty() ? QString()
                                                                                                  : dev->currentData(Qt::UserRole + 1).toString());
        });
        connect(refreshDev, &QPushButton::clicked, this, [=] { fillDevices(); });
        connect(mc, &MidiControl::statusChanged, this, [=] {
            status->setText(mc->statusText());
            fillTable();
        });
        connect(mc, &MidiControl::bindingsChanged, this, fillTable);
        connect(mc, &MidiControl::messageSeen, this, [monitor](const QString &key, int value) {
            monitor->setText(TR("Utolsó üzenet: %1 · érték %2").arg(MidiControl::describe(key)).arg(value));
        });
        connect(mc, &MidiControl::learned, this, [=](const QString &id, const QString &key, const QStringList &moved) {
            QString t = TR("„%1” ← %2").arg(MidiControl::action(id).label, MidiControl::describe(key));
            if (!moved.isEmpty()) {
                QStringList names;
                for (const QString &m : moved) names << MidiControl::action(m).label;
                t += QLatin1Char(' ') + TR("(eddig ehhez tartozott: %1)").arg(names.join(QStringLiteral(", ")));
            }
            fillTable();
            learnNote->setText(t);
        });
        connect(thr, &QSpinBox::valueChanged, this, [mc](int v) { mc->setFaderStartThreshold(v); });
        connect(clearAll, &QPushButton::clicked, this, [this, mc] {
            if (!qEnvironmentVariableIsSet("ETER_AUTOCONFIRM")
                && QMessageBox::question(this, TR("MIDI"), TR("Minden MIDI-hozzárendelés törlődik. Folytatod?")) != QMessageBox::Yes)
                return;
            mc->clearAll();
        });
        // Az ablak bezárásakor a félbehagyott tanítás megszakad.
        connect(this, &QObject::destroyed, mc, [mc] { mc->cancelLearn(); });
    }

    root = newPage(TR("Lapok"));
    {
        // A főablak fülei: amelyik nem kell, elrejthető. A rejtett lap a háttérben tovább működik.
        auto *cap = new QLabel(TR("LÁTHATÓ LAPOK"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *about = new QLabel(TR("A kikapcsolt lap füle eltűnik a főablakból. A lap a háttérben tovább működik: például "
                                    "az időzített események akkor is lefutnak, ha az Időzítések lap rejtett. A Beállítások "
                                    "fül elrejtése után a beállítások az Eszközök → Beállítások menüből (Ctrl+,) érhetők el."));
        about->setObjectName("small");
        about->setWordWrap(true);
        root->addWidget(about);
        const QStringList hidden = MainWindow::hiddenTabs();
        QVector<QCheckBox *> boxes;
        for (const auto &[key, label] : MainWindow::tabList()) {
            auto *c = new QCheckBox(label);
            c->setObjectName(QStringLiteral("tab_") + key);
            c->setChecked(!hidden.contains(key));
            c->setProperty("tabKey", key);
            root->addWidget(c);
            boxes << c;
        }
        for (QCheckBox *c : boxes)
            connect(c, &QCheckBox::toggled, this, [this, boxes] {
                QStringList keys;
                for (QCheckBox *b : boxes)
                    if (!b->isChecked()) keys << b->property("tabKey").toString();
                MainWindow::setHiddenTabs(keys);
                main_->applyTabVisibility();
            });
        root->addStretch(1);
    }

    root = newPage(TR("Weboldal"));
    {
        // Weboldal: „Most szól”, „Következő számok” és számkérés a WordPress-oldalon (Éter Radio bővítmény).
        Database *db = main_->database();
        const WebSync::Config cur = WebSync::loadConfig();
        auto *cap = new QLabel(TR("WEBOLDAL"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *about = new QLabel(TR("Az Éter elküldi a weboldalnak, mi szól, mi következik és mi szólt a legtöbbször, és onnan "
                                    "gyűjti a látogatók számkéréseit. A kapcsolatot mindig a program kezdeményezi, ezért a stúdió "
                                    "gépét nem kell az internet felől elérhetővé tenni. WordPress-oldalra az Éter Radio "
                                    "bővítményt kell telepíteni (eter-radio.zip): a címet és a kulcsot a WordPressben a "
                                    "Beállítások → Éter Radio oldalon találod. Bármilyen más oldalhoz az Éter Web modul való "
                                    "(eter-web.zip, egy mappa bármely PHP-s tárhelyen): a címet és a kulcsot az admin.php oldala mutatja."));
        about->setObjectName("small");
        about->setWordWrap(true);
        root->addWidget(about);
        auto *on = new QCheckBox(TR("Kapcsolat a weboldallal"));
        on->setObjectName("webOn");
        on->setChecked(cur.enabled);
        root->addWidget(on);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnStretch(1, 1);
        auto *url = new QLineEdit(cur.url);
        url->setObjectName("webUrl");
        url->setPlaceholderText(QStringLiteral("https://radio.example.com"));
        auto *key = new QLineEdit(cur.key);
        key->setObjectName("webKey");
        key->setEchoMode(QLineEdit::Password);
        auto *showKey = new QCheckBox(TR("Megjelenítés"));
        connect(showKey, &QCheckBox::toggled, key, [key](bool v) { key->setEchoMode(v ? QLineEdit::Normal : QLineEdit::Password); });
        g->addWidget(new QLabel(TR("A weboldal címe")), 0, 0);
        g->addWidget(url, 0, 1, 1, 2);
        g->addWidget(new QLabel(TR("Kulcs")), 1, 0);
        g->addWidget(key, 1, 1);
        g->addWidget(showKey, 1, 2);
        auto *nextCount = new QSpinBox;
        nextCount->setObjectName("webNext");
        nextCount->setRange(1, 20);
        nextCount->setValue(cur.nextCount);
        nextCount->setSuffix(TR(" szám"));
        auto *musicOnly = new QCheckBox(TR("csak zenék (jingle, reklám, hírek és hasonlók nélkül)"));
        musicOnly->setChecked(cur.musicOnly);
        auto *nextRow = new QHBoxLayout;
        nextRow->addWidget(nextCount);
        nextRow->addSpacing(12);
        nextRow->addWidget(musicOnly);
        nextRow->addStretch(1);
        g->addWidget(new QLabel(TR("Következő számok")), 2, 0);
        g->addLayout(nextRow, 2, 1, 1, 2);
        root->addLayout(g);

        auto *reqCap = new QLabel(TR("SZÁMKÉRÉS"));
        reqCap->setObjectName("caption");
        root->addWidget(reqCap);
        auto *reqOn = new QCheckBox(TR("A látogatók kérhetnek számot a weboldalon"));
        reqOn->setObjectName("webRequests");
        reqOn->setChecked(cur.requests);
        root->addWidget(reqOn);
        auto *reqAbout = new QLabel(TR("A kért szám a most szóló elem után kerül a playlistbe; ha már vannak kérések a "
                                       "sorban, az utolsó alá. Ha nincs kérés, a playlist a szokásos módon megy tovább. A "
                                       "kérések a felső sáv Kérések gombjával nézhetők meg."));
        reqAbout->setObjectName("small");
        reqAbout->setWordWrap(true);
        root->addWidget(reqAbout);
        auto *rg = new QGridLayout;
        rg->setHorizontalSpacing(10);
        rg->setVerticalSpacing(8);
        rg->setColumnStretch(1, 1);
        rg->setColumnStretch(3, 1);
        auto *approve = new QComboBox;
        approve->setObjectName("webApprove");
        approve->addItem(TR("magától a playlistbe kerül"), false);
        approve->addItem(TR("a stúdióban jóvá kell hagyni"), true);
        approve->setCurrentIndex(cur.approve ? 1 : 0);
        rg->addWidget(new QLabel(TR("A kérés")), 0, 0);
        rg->addWidget(approve, 0, 1, 1, 3, Qt::AlignLeft);
        auto *what = new QComboBox;
        what->setObjectName("webWhat");
        what->addItem(TR("minden zene"), true);
        what->addItem(TR("a kijelölt kategóriák"), false);
        what->setCurrentIndex(cur.allMusic ? 0 : 1);
        auto *when = new QComboBox;
        when->setObjectName("webWhen");
        when->addItem(TR("mindig"), true);
        when->addItem(TR("a kijelölt sémák óráiban"), false);
        when->setCurrentIndex(cur.always ? 0 : 1);
        rg->addWidget(new QLabel(TR("Kérhető")), 1, 0);
        rg->addWidget(what, 1, 1);
        rg->addWidget(new QLabel(TR("Mikor")), 1, 2);
        rg->addWidget(when, 1, 3);
        auto *cats = new QListWidget;
        cats->setObjectName("webCategories");
        for (const Category &c : db->itemCategories()) {
            auto *it = new QListWidgetItem(c.name, cats);
            it->setData(Qt::UserRole, c.id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(cur.categories.contains(c.id) ? Qt::Checked : Qt::Unchecked);
        }
        auto *clocks = new QListWidget;
        clocks->setObjectName("webClocks");
        for (const Clock &c : db->clocks()) {
            auto *it = new QListWidgetItem(c.name, clocks);
            it->setData(Qt::UserRole, c.id);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(cur.clocks.contains(c.id) ? Qt::Checked : Qt::Unchecked);
        }
        cats->setMaximumHeight(130);
        clocks->setMaximumHeight(130);
        clocks->setToolTip(TR("Az órarácsban ezekhez a sémákhoz rendelt órákban lehet kérni"));
        rg->addWidget(cats, 2, 1);
        rg->addWidget(clocks, 2, 3);
        root->addLayout(rg);

        auto *applyRow = new QHBoxLayout;
        auto *apply = new QPushButton(TR("Alkalmazás"));
        apply->setObjectName("webApply");
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
            for (QWidget *w : std::initializer_list<QWidget *>{url, key, showKey, nextCount, musicOnly, reqOn})
                w->setEnabled(on->isChecked());
            const bool r = on->isChecked() && reqOn->isChecked();
            for (QWidget *w : std::initializer_list<QWidget *>{approve, what, when}) w->setEnabled(r);
            cats->setEnabled(r && what->currentIndex() == 1);
            clocks->setEnabled(r && when->currentIndex() == 1);
        };
        refresh();
        connect(on, &QCheckBox::toggled, this, refresh);
        connect(reqOn, &QCheckBox::toggled, this, refresh);
        connect(what, &QComboBox::currentIndexChanged, this, refresh);
        connect(when, &QComboBox::currentIndexChanged, this, refresh);
        auto checked = [](QListWidget *l) {
            QList<int> ids;
            for (int i = 0; i < l->count(); ++i)
                if (l->item(i)->checkState() == Qt::Checked) ids << l->item(i)->data(Qt::UserRole).toInt();
            return ids;
        };
        auto showStatus = [this, status] {
            WebSync *w = main_->webSync();
            status->setText(TR("Állapot: %1").arg(w ? w->status() : QString()));
        };
        connect(apply, &QPushButton::clicked, this, [=, this] {
            WebSync::Config c;
            c.enabled = on->isChecked();
            c.url = url->text().trimmed();
            c.key = key->text().trimmed();
            c.nextCount = nextCount->value();
            c.musicOnly = musicOnly->isChecked();
            c.requests = reqOn->isChecked();
            c.approve = approve->currentData().toBool();
            c.allMusic = what->currentData().toBool();
            c.categories = checked(cats);
            c.always = when->currentData().toBool();
            c.clocks = checked(clocks);
            WebSync::saveConfig(c);
            main_->configureWeb();
            showStatus();
        });
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, showStatus);
        t->start(1000);
        showStatus();
    }

    root = newPage(TR("Automatikus mix"));
    {
        Database *db = main_->database();
        auto caption = [&](const QString &t) {
            auto *c = new QLabel(t);
            c->setObjectName("caption");
            root->addWidget(c);
        };
        auto note = [&](const QString &t) {
            auto *l = new QLabel(t);
            l->setObjectName("small");
            l->setWordWrap(true);
            root->addWidget(l);
        };
        auto *form = new QFormLayout;
        auto dbSpin = [](double v, double lo, double hi, const QString &suffix, int decimals = 0) {
            auto *s = new QDoubleSpinBox;
            s->setRange(lo, hi);
            s->setDecimals(decimals);
            s->setSingleStep(decimals ? 0.5 : 1);
            s->setSuffix(suffix);
            s->setValue(v);
            return s;
        };
        // Sablonok: az automatikus mix beállításainak elmentett változatai, a hangtár kategóriáihoz rendelhetők.
        caption(TR("SABLON"));
        note(TR("A lenti beállítások sablonként is elmenthetők, és a sablon a hangtár kategóriáihoz rendelhető (Hangtár → "
                "Kategóriák…). Az ilyen kategóriájú elem utáni átmenetet a sablonja szerint keveri a program (a bejövő elem "
                "kezdőpontjának küszöbét a bejövő elem sablonja szerint); sablon nélkül az alapbeállítás érvényes. Itt "
                "választhatod ki, melyiket szerkeszted: az „Alkalmazás” abba ment."));
        auto *tplRow = new QHBoxLayout;
        tplRow->setSpacing(6);
        auto *tplSel = new QComboBox;
        tplSel->setObjectName("mixTemplate");
        tplSel->setMinimumWidth(240);
        auto *tplNew = new QPushButton(TR("Mentés új sablonként…"));
        tplNew->setObjectName("mixTemplateNew");
        auto *tplRename = new QPushButton(TR("Átnevezés…"));
        auto *tplDelete = new QPushButton(TR("Törlés"));
        tplDelete->setProperty("danger", true);
        tplRow->addWidget(tplSel, 1);
        tplRow->addWidget(tplNew);
        tplRow->addWidget(tplRename);
        tplRow->addWidget(tplDelete);
        root->addLayout(tplRow);
        auto *tplCats = new QLabel;
        tplCats->setObjectName("small");
        tplCats->setWordWrap(true);
        root->addWidget(tplCats);

        // Előnézet: két mintaelem átmenete a lapon éppen beállított értékekkel (az „Alkalmazás” előtt is).
        caption(TR("ELŐNÉZET"));
        auto *pvRow = new QHBoxLayout;
        pvRow->setSpacing(8);
        auto *pvLabel = new QLabel(TR("Példa átmenet"));
        auto *pvScenario = new QComboBox;
        pvScenario->setObjectName("mixPreviewScenario");
        pvScenario->addItems(MixPreview::scenarioNames());
        pvRow->addWidget(pvLabel);
        pvRow->addWidget(pvScenario, 1);
        root->addLayout(pvRow);
        auto *preview = new MixPreview;
        root->addWidget(preview);
        auto *pvSummary = new QLabel;
        pvSummary->setObjectName("mixPreviewSummary");
        pvSummary->setWordWrap(true);
        pvSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(pvSummary);
        connect(preview, &MixPreview::summaryChanged, pvSummary, &QLabel::setText);
        connect(pvScenario, &QComboBox::currentIndexChanged, preview, &MixPreview::setScenario);

        caption(TR("OKOS MIX"));
        note(TR("Két zene között (automatikus mix módban) a program megnézi a kimenő szám végét és a következő elejét: "
                "lassan elhalkuló végnél korábban keveri rá a következőt, hirtelen végnél szorosan illeszti; lágy, felfutó "
                "intrónál hosszabb a ráúsztatás, erős kezdésnél rövid. A Mix editorban (automatikus mix módban) a "
                "kiválasztott átmenetnél kiírja, mit tervezett."));
        const eter::SmartMixSettings cur = loadSmartMix(db);
        auto *smartOn = new QCheckBox(TR("Okos mix bekapcsolva"));
        smartOn->setObjectName("smartMixOn");
        smartOn->setChecked(cur.enabled);
        root->addWidget(smartOn);
        auto *endDepth = dbSpin(cur.endDepthDb, 6, 50, TR(" dB"));
        endDepth->setToolTip(TR("A szám hallható vége ott van, ahol a jel ennyivel a szám átlagos szintje alá esik"));
        auto *fadeDepth = dbSpin(cur.fadeDepthDb, 2, 30, TR(" dB"));
        fadeDepth->setToolTip(TR("Lecsengő végnél: a következő szám akkor indul, amikor a kimenő ennyit halkult (nagyobb érték: később)"));
        auto *introMax = dbSpin(cur.introMaxMs / 1000.0, 0, 30, TR(" mp"), 1);
        introMax->setToolTip(TR("Lágy, felfutó intrónál a következő szám legfeljebb ennyivel indul a kimenő vége előtt"));
        auto *tight = dbSpin(cur.tightMs / 1000.0, 0, 3, TR(" mp"), 2);
        tight->setSingleStep(0.05);
        tight->setToolTip(TR("Hirtelen véget érő szám és erős kezdés: ennyi az átfedés (0: pontosan a vége után)"));
        auto *maxOverlap = dbSpin(cur.maxOverlapMs / 1000.0, 0, 30, TR(" mp"), 1);
        maxOverlap->setToolTip(TR("Semmilyen esetben nem szól tovább együtt a két szám"));
        form->addRow(TR("Hallható vég: a szám szintje alatt"), endDepth);
        form->addRow(TR("Lecsengő végnél a következő indul, ha a kimenő ennyit halkult"), fadeDepth);
        form->addRow(TR("Lágy intrónál legfeljebb ennyivel korábban indul"), introMax);
        form->addRow(TR("Hirtelen vég, erős kezdés: átfedés"), tight);
        form->addRow(TR("Leghosszabb átfedés"), maxOverlap);
        root->addLayout(form);

        caption(TR("TEMPÓ ÉS HANGNEM"));
        auto *flowOn = new QCheckBox(TR("Tempó és hangnem figyelése"));
        flowOn->setChecked(cur.flowAware);
        root->addWidget(flowOn);
        note(TR("Ha a két szám tempója eltér, vagy a hangnemük nem illik össze (Camelot-kerék), az átfedés rövidebb, és a "
                "kimenő szám az átfedés alatt végig halkul – így nem csúszik szét az ütem, és nem szól össze két nem illő "
                "hangnem. A fél- és a dupla tempó egyezőnek számít. Ismeretlen tempó vagy hangnem nem korlátoz."));
        auto *form2 = new QFormLayout;
        auto *tempoTol = dbSpin(cur.tempoTolPct, 0, 50, TR(" %"));
        auto *clash = dbSpin(cur.clashOverlapMs / 1000.0, 0, 30, TR(" mp"), 1);
        form2->addRow(TR("Egyezőnek számító tempóeltérés"), tempoTol);
        form2->addRow(TR("Ütköző tempónál vagy hangnemnél az átfedés legfeljebb"), clash);
        root->addLayout(form2);

        caption(TR("MINDEN ELEM, REJTETT SZÁM, CSENDVÉDELEM"));
        note(TR("Automatikus mix módban a jingle, reklám, hírek és voice track átmeneteit is a program kezelheti: levágja a "
                "csendet a kimenő elem végéről és a következő elejéről, és szorosan egymás után indítja őket (átúsztatás "
                "nélkül). A rejtett szám (a dal után hosszú csend, majd még valami) a csendnél véget ér. A csendvédelem "
                "akkor is továbblép, ha bármi más miatt hosszabb ideig nem szól semmi."));
        auto *allItems = new QCheckBox(TR("Automatikus mix a nem zenei elemeknél is (jingle, reklám, hírek, voice track)"));
        allItems->setObjectName("smartAllItems");
        allItems->setChecked(cur.allItems);
        root->addWidget(allItems);
        auto *savedWins = new QCheckBox(TR("A Mix editorban mentett átmenet és az elem saját mix pontja ilyenkor is érvényes"));
        savedWins->setObjectName("smartSavedWins");
        savedWins->setChecked(cur.savedWins);
        savedWins->setToolTip(TR("Kikapcsolva automatikus mix módban minden átmenetet a program kever; a mentett voice track akkor is marad"));
        root->addWidget(savedWins);
        auto *hidden = new QCheckBox(TR("Rejtett szám felismerése"));
        hidden->setObjectName("smartHidden");
        hidden->setChecked(cur.hiddenTrack);
        root->addWidget(hidden);
        auto *deadOn = new QCheckBox(TR("Csendvédelem: ha a szóló elem hosszabb ideig néma, induljon a következő"));
        deadOn->setObjectName("deadAirOn");
        deadOn->setChecked(deadAirGuard(db));
        root->addWidget(deadOn);
        auto *form4 = new QFormLayout;
        auto *gap = dbSpin(cur.segueGapMs / 1000.0, 0, 3, TR(" mp"), 2);
        gap->setSingleStep(0.05);
        gap->setToolTip(TR("Nem zenei elemnél ekkora szünet van az egyik hallható vége és a következő eleje között (0: azonnal)"));
        auto *hiddenSil = dbSpin(cur.hiddenSilenceMs / 1000.0, 1, 30, TR(" mp"), 1);
        hiddenSil->setToolTip(TR("Ilyen hosszú csend után a szám második felében a szám véget ér, ha utána még jön valami"));
        auto *deadSec = dbSpin(deadAirMs(db) / 1000.0, 1, 30, TR(" mp"), 1);
        deadSec->setToolTip(TR("Automata módban, ha a szóló elem második felében ennyi ideig −50 dB alatt van a jel"));
        form4->addRow(TR("Szünet nem zenei elemnél"), gap);
        form4->addRow(TR("Rejtett szám előtti csend legalább"), hiddenSil);
        form4->addRow(TR("Csendvédelem ennyi néma idő után"), deadSec);
        root->addLayout(form4);
        auto syncEnabled = [=] {
            gap->setEnabled(allItems->isChecked());
            savedWins->setEnabled(allItems->isChecked());
            hiddenSil->setEnabled(hidden->isChecked());
            deadSec->setEnabled(deadOn->isChecked());
        };
        syncEnabled();
        for (QCheckBox *c : {allItems, hidden, deadOn}) connect(c, &QCheckBox::toggled, this, syncEnabled);

        caption(TR("EGYSZERŰ MÓD"));
        note(TR("Ha az okos mix ki van kapcsolva (vagy a kimenő szám elemzése még nem készült el): a mix pont az utolsó "
                "hely, ahol a jel még a küszöb fölött van, és a kimenő szám onnan úszik ki. A bejövő szám ott indul, ahol "
                "a jel először a kezdőpont küszöbe fölé ér (a mentett kezdőpont ezt felülírja)."));
        auto *form3 = new QFormLayout;
        auto *thr = dbSpin(db->setting("auto_mix_threshold_db", "-24").toDouble(), -60, -3, TR(" dB"));
        auto *cueThr = dbSpin(db->setting("auto_cue_threshold_db", "-45").toDouble(), -80, -10, TR(" dB"));
        cueThr->setObjectName("autoCueThreshold");
        auto *defFade = dbSpin(db->setting("default_fade_out_ms", "2500").toDouble() / 1000.0, 0.1, 15, TR(" mp"), 1);
        form3->addRow(TR("Mix pont küszöbe"), thr);
        form3->addRow(TR("Kezdőpont küszöbe"), cueThr);
        form3->addRow(TR("Alapértelmezett kiúsztatás"), defFade);
        root->addLayout(form3);

        auto *row = new QHBoxLayout;
        auto *apply = new QPushButton(TR("Alkalmazás"));
        apply->setObjectName("smartMixApply");
        auto *defaults = new QPushButton(TR("Alapértékek"));
        row->addWidget(apply);
        row->addWidget(defaults);
        row->addStretch(1);
        root->addLayout(row);
        root->addStretch(1);
        connect(defaults, &QPushButton::clicked, this, [=] {
            const eter::SmartMixSettings d;
            smartOn->setChecked(d.enabled);
            endDepth->setValue(d.endDepthDb);
            fadeDepth->setValue(d.fadeDepthDb);
            introMax->setValue(d.introMaxMs / 1000.0);
            tight->setValue(d.tightMs / 1000.0);
            maxOverlap->setValue(d.maxOverlapMs / 1000.0);
            flowOn->setChecked(d.flowAware);
            tempoTol->setValue(d.tempoTolPct);
            clash->setValue(d.clashOverlapMs / 1000.0);
            thr->setValue(-24);
            cueThr->setValue(-45);
            defFade->setValue(2.5);
            allItems->setChecked(d.allItems);
            savedWins->setChecked(d.savedWins);
            hidden->setChecked(d.hiddenTrack);
            deadOn->setChecked(true);
            gap->setValue(d.segueGapMs / 1000.0);
            hiddenSil->setValue(d.hiddenSilenceMs / 1000.0);
            deadSec->setValue(3.0);
        });
        // A sablonba kerülő értékek az űrlapról (a csendvédelem minden elemre közös, az csak az alapbeállításban van).
        auto collect = [=] {
            auto ms = [](QDoubleSpinBox *s) { return QString::number(qRound(s->value() * 1000)); };
            QJsonObject v{{"smart_mix", smartOn->isChecked() ? "1" : "0"},
                          {"smart_end_depth_db", QString::number(endDepth->value())},
                          {"smart_fade_depth_db", QString::number(fadeDepth->value())},
                          {"smart_intro_max_ms", ms(introMax)},
                          {"smart_tight_ms", ms(tight)},
                          {"smart_max_overlap_ms", ms(maxOverlap)},
                          {"smart_flow", flowOn->isChecked() ? "1" : "0"},
                          {"smart_tempo_tol_pct", QString::number(qRound(tempoTol->value()))},
                          {"smart_clash_overlap_ms", ms(clash)},
                          {"auto_mix_threshold_db", QString::number(thr->value())},
                          {"auto_cue_threshold_db", QString::number(cueThr->value())},
                          {"default_fade_out_ms", ms(defFade)},
                          {"smart_all_items", allItems->isChecked() ? "1" : "0"},
                          {"smart_saved_wins", savedWins->isChecked() ? "1" : "0"},
                          {"smart_hidden_track", hidden->isChecked() ? "1" : "0"},
                          {"smart_segue_gap_ms", ms(gap)},
                          {"smart_hidden_silence_ms", ms(hiddenSil)}};
            return v;
        };
        // Az előnézet a lap aktuális értékeivel (minden változtatásra).
        auto updatePreview = [=] {
            const QJsonObject v = collect();
            preview->setValues(loadSmartMix(db, &v), thr->value(), cueThr->value(), qRound(defFade->value() * 1000));
        };
        for (QDoubleSpinBox *sp : {endDepth, fadeDepth, introMax, tight, maxOverlap, tempoTol, clash, thr, cueThr, defFade, gap, hiddenSil})
            connect(sp, &QDoubleSpinBox::valueChanged, this, updatePreview);
        for (QCheckBox *c : {smartOn, flowOn, allItems, hidden}) connect(c, &QCheckBox::toggled, this, updatePreview);
        updatePreview();
        // Az űrlap kitöltése egy sablon (vagy az alapbeállítás) értékeivel.
        auto fill = [=](const QJsonObject *tpl) {
            const MixValues v{db, tpl};
            const eter::SmartMixSettings c = loadSmartMix(db, tpl);
            smartOn->setChecked(c.enabled);
            endDepth->setValue(c.endDepthDb);
            fadeDepth->setValue(c.fadeDepthDb);
            introMax->setValue(c.introMaxMs / 1000.0);
            tight->setValue(c.tightMs / 1000.0);
            maxOverlap->setValue(c.maxOverlapMs / 1000.0);
            flowOn->setChecked(c.flowAware);
            tempoTol->setValue(c.tempoTolPct);
            clash->setValue(c.clashOverlapMs / 1000.0);
            allItems->setChecked(c.allItems);
            savedWins->setChecked(c.savedWins);
            hidden->setChecked(c.hiddenTrack);
            gap->setValue(c.segueGapMs / 1000.0);
            hiddenSil->setValue(c.hiddenSilenceMs / 1000.0);
            thr->setValue(v.get("auto_mix_threshold_db", "-24").toDouble());
            cueThr->setValue(v.get("auto_cue_threshold_db", "-45").toDouble());
            defFade->setValue(v.get("default_fade_out_ms", "2500").toDouble() / 1000.0);
        };
        // A sablonválasztó: 0 = alapbeállítás; mellette, mely kategóriák használják.
        auto refreshTemplates = [=](int select) {
            const QVector<MixTemplate> list = loadMixTemplates(db);
            QSignalBlocker b(tplSel);
            tplSel->clear();
            tplSel->addItem(TR("Alapbeállítás (minden kategória, amelyhez nincs sablon)"), 0);
            for (const MixTemplate &t : list) tplSel->addItem(t.name, t.id);
            tplSel->setCurrentIndex(std::max(0, tplSel->findData(select)));
            const int id = tplSel->currentData().toInt();
            QStringList cats;
            for (const Category &c : db->categories())
                if (categoryMixTemplate(db, c.id) == id && id > 0) cats << c.name;
            tplCats->setText(id == 0 ? QString()
                             : cats.isEmpty() ? TR("Ehhez a sablonhoz még nincs kategória rendelve (Hangtár → Kategóriák…).")
                                              : TR("Ezek a kategóriák használják: %1").arg(cats.join(QStringLiteral(", "))));
            tplCats->setVisible(id != 0);
            tplRename->setEnabled(id != 0);
            tplDelete->setEnabled(id != 0);
            deadOn->setEnabled(id == 0);
            deadSec->setEnabled(id == 0 && deadOn->isChecked());
            deadOn->setToolTip(id == 0 ? QString() : TR("A csendvédelem minden elemre közös: az alapbeállításban állítható"));
        };
        auto templateValues = [db](int id) {
            for (const MixTemplate &t : loadMixTemplates(db))
                if (t.id == id) return t.values;
            return QJsonObject();
        };
        refreshTemplates(0);
        connect(tplSel, &QComboBox::activated, this, [=] {
            const int id = tplSel->currentData().toInt();
            const QJsonObject vals = templateValues(id);
            fill(id ? &vals : nullptr);
            refreshTemplates(id);
        });
        connect(tplNew, &QPushButton::clicked, this, [=, this] {
            bool ok = false;
            QString name = qEnvironmentVariable("ETER_TEMPLATE_NAME");
            if (name.isEmpty())
                name = QInputDialog::getText(this, TR("Mix sablon"), TR("A sablon neve (például Dance, Ballada, Rock):"),
                                             QLineEdit::Normal, QString(), &ok).trimmed();
            if (name.isEmpty()) return;
            QVector<MixTemplate> list = loadMixTemplates(db);
            MixTemplate t;
            for (const MixTemplate &x : list) t.id = std::max(t.id, x.id);
            ++t.id;
            t.name = name;
            t.values = collect();
            list.push_back(t);
            saveMixTemplates(db, list);
            refreshTemplates(t.id);
            main_->reloadMixSettings();
        });
        connect(tplRename, &QPushButton::clicked, this, [=, this] {
            const int id = tplSel->currentData().toInt();
            if (!id) return;
            bool ok = false;
            const QString name = QInputDialog::getText(this, TR("Mix sablon"), TR("A sablon új neve:"), QLineEdit::Normal,
                                                       tplSel->currentText(), &ok).trimmed();
            if (!ok || name.isEmpty()) return;
            QVector<MixTemplate> list = loadMixTemplates(db);
            for (MixTemplate &t : list)
                if (t.id == id) t.name = name;
            saveMixTemplates(db, list);
            refreshTemplates(id);
        });
        connect(tplDelete, &QPushButton::clicked, this, [=, this] {
            const int id = tplSel->currentData().toInt();
            if (!id) return;
            if (QMessageBox::question(this, TR("Mix sablon"), TR("Törlöd a(z) „%1” sablont? Az ehhez rendelt kategóriák ezután az "
                                                               "alapbeállítás szerint keverednek.").arg(tplSel->currentText()),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            QVector<MixTemplate> list = loadMixTemplates(db);
            list.erase(std::remove_if(list.begin(), list.end(), [id](const MixTemplate &t) { return t.id == id; }), list.end());
            saveMixTemplates(db, list);
            for (const Category &c : db->categories())
                if (categoryMixTemplate(db, c.id) == id) setCategoryMixTemplate(db, c.id, 0);
            fill(nullptr);
            refreshTemplates(0);
            main_->reloadMixSettings();
        });
        connect(apply, &QPushButton::clicked, this, [=, this] {
            const int id = tplSel->currentData().toInt();
            if (id) {
                // Sablon: az űrlap értékei a sablonba kerülnek; az alapbeállítás nem változik.
                QVector<MixTemplate> list = loadMixTemplates(db);
                for (MixTemplate &t : list)
                    if (t.id == id) t.values = collect();
                saveMixTemplates(db, list);
                main_->reloadMixSettings();
                return;
            }
            const QJsonObject v = collect();
            for (auto it = v.begin(); it != v.end(); ++it) db->setSetting(it.key(), it.value().toString());
            auto ms = [](QDoubleSpinBox *s) { return QString::number(qRound(s->value() * 1000)); };
            db->setSetting("dead_air_guard", deadOn->isChecked() ? "1" : "0");
            db->setSetting("dead_air_ms", ms(deadSec));
            main_->reloadMixSettings();
        });
    }

    addExternalPage(newPage(TR("Külső playlistek")));

    root = newPage(TR("Elemzés"));
    if (LibraryAnalyzer *an = main_->analyzer()) {
        // A hangtár háttérelemzése (tempó, hangnem) és a hangnem jelölése.
        const LibraryAnalyzer::Config cur = an->config();
        auto *cap = new QLabel(TR("HÁTTÉRELEMZÉS"));
        cap->setObjectName("caption");
        root->addWidget(cap);
        auto *about = new QLabel(TR("A program a háttérben, a legkisebb prioritással végignézi a hangtár zenéit, és megbecsüli a "
                                    "hiányzó tempót (BPM) és hangnemet. Két szám között szünetet tart, hogy az adást ne zavarja; "
                                    "az új importokat magától felveszi. Csak az üres mezőt tölti ki: amit kézzel vagy a fájl "
                                    "címkéjéből (TBPM, TKEY) kapott, azt nem írja felül."));
        about->setObjectName("small");
        about->setWordWrap(true);
        root->addWidget(about);
        auto *on = new QCheckBox(TR("Háttérelemzés bekapcsolva"));
        on->setObjectName("analysisOn");
        on->setChecked(cur.enabled);
        root->addWidget(on);
        auto *winRow = new QHBoxLayout;
        auto *useWin = new QCheckBox(TR("Csak ebben az időablakban:"));
        useWin->setObjectName("analysisWindow");
        useWin->setChecked(cur.useWindow);
        auto hourSpin = [](int v) {
            auto *s = new QSpinBox;
            s->setRange(0, 23);
            s->setSuffix(QStringLiteral(":00"));
            s->setValue(v);
            return s;
        };
        QSpinBox *from = hourSpin(cur.fromHour), *to = hourSpin(cur.toHour);
        winRow->addWidget(useWin);
        winRow->addWidget(from);
        winRow->addWidget(new QLabel(QStringLiteral("–")));
        winRow->addWidget(to);
        winRow->addStretch(1);
        root->addLayout(winRow);
        auto *winNote = new QLabel(TR("Például 1:00 – 6:00 éjszaka, vagy 22:00 – 6:00 (éjfélen átnyúlik). Az ablakon kívül a "
                                      "folyamatban lévő elemzés is leáll, és a következő ablakban folytatódik."));
        winNote->setObjectName("small");
        winNote->setWordWrap(true);
        root->addWidget(winNote);

        auto *cap2 = new QLabel(TR("HANGNEM JELÖLÉSE"));
        cap2->setObjectName("caption");
        root->addWidget(cap2);
        auto *notation = new QComboBox;
        notation->setObjectName("keyNotation");
        notation->addItem(TR("Camelot (8A, 5B …)"), true);
        notation->addItem(TR("Hagyományos (Am, C# …)"), false);
        notation->setCurrentIndex(cur.camelot ? 0 : 1);
        auto *notRow = new QHBoxLayout;
        notRow->addWidget(notation);
        notRow->addStretch(1);
        root->addLayout(notRow);
        auto *notNote = new QLabel(TR("A Camelot-kerékben a szomszédos számok (8A → 7A, 9A) és az azonos szám másik betűje "
                                      "(8A → 8B) illik egymáshoz. Az ütemező hangnem-szabálya (Ütemező → Szabályok) ezt használja."));
        notNote->setObjectName("small");
        notNote->setWordWrap(true);
        root->addWidget(notNote);

        auto *status = new QLabel;
        status->setObjectName("analysisStatus");
        status->setWordWrap(true);
        root->addWidget(status);
        auto *applyRow = new QHBoxLayout;
        auto *apply = new QPushButton(TR("Alkalmazás"));
        apply->setObjectName("analysisApply");
        auto *again = new QPushButton(TR("Újrapróbálás"));
        again->setToolTip(TR("A korábban sikertelen (üresen maradt) tempót és hangnemet újra megpróbálja"));
        applyRow->addWidget(apply);
        applyRow->addWidget(again);
        applyRow->addStretch(1);
        root->addLayout(applyRow);
        root->addStretch(1);
        auto show = [status, an] { status->setText(TR("Állapot: %1").arg(an->status())); };
        connect(an, &LibraryAnalyzer::statusChanged, status, show);
        connect(apply, &QPushButton::clicked, this, [=, this] {
            LibraryAnalyzer::Config c;
            c.enabled = on->isChecked();
            c.useWindow = useWin->isChecked();
            c.fromHour = from->value();
            c.toHour = to->value();
            c.camelot = notation->currentData().toBool();
            const bool notationChanged = c.camelot != an->config().camelot;
            LibraryAnalyzer::saveConfig(c);
            an->configure(c);
            if (notationChanged) main_->database()->notifyItemsChanged();
            show();
        });
        connect(again, &QPushButton::clicked, an, [an, show] {
            an->restart();
            show();
        });
        show();
    }

    root = newPage(TR("Előadói adatok"));
    if (ArtistInfoService *info = main_->artistInfo()) {
        // TheAudioDB: életrajz, fotó, ország, műfaj, stílus, hangulat a lejátszóban.
        auto cap = [&](const QString &t) {
            auto *c = new QLabel(t);
            c->setObjectName("caption");
            root->addWidget(c);
        };
        auto small = [&](const QString &t) {
            auto *l = new QLabel(t);
            l->setObjectName("small");
            l->setWordWrap(true);
            l->setOpenExternalLinks(true);
            root->addWidget(l);
            return l;
        };
        cap(TR("ELŐADÓI ADATOK"));
        small(TR("A lejátszóban az előadó fotója, országa, évei, műfaja (és ha van, stílusa, hangulata) látszik, az "
                 "„Életrajz” gombra pedig az életrajza, a program nyelvén, ha van, különben angolul. A számok albuma és éve "
                 "is kitöltődik. Az adatok az adatbázisba kerülnek; a meglévő adatot nem írják felül (az éneket, az albumot "
                 "és az évet csak ott töltik ki, ahol még üres). Amelyik forrás ki van kapcsolva, annak a program egyetlen "
                 "kérést sem küld."));
        const ArtistInfoService::Settings cur = info->settings();
        auto *modeForm = new QFormLayout;
        auto *mode = new QComboBox;
        mode->setObjectName("adbMode");
        mode->addItem(TR("Automatikusan, a háttérben (az egész hangtár)"), ArtistInfoService::Auto);
        mode->addItem(TR("Amikor egy elem a lejátszóba kerül"), ArtistInfoService::OnLoad);
        mode->addItem(TR("Csak kézzel (hangtár: jobb klikk → Előadói adatok lekérése)"), ArtistInfoService::Manual);
        mode->setCurrentIndex(std::max(0, mode->findData(cur.mode)));
        modeForm->addRow(TR("Keresés módja"), mode);
        root->addLayout(modeForm);
        cap(TR("MUSICBRAINZ (INGYENES, KULCS NÉLKÜL)"));
        auto *mb = new QCheckBox(TR("Előadói adatok és a számok albuma, éve a MusicBrainzből"));
        mb->setObjectName("mbEnabled");
        mb->setChecked(cur.mbEnabled);
        root->addWidget(mb);
        auto *wiki = new QCheckBox(TR("Életrajz és fotó a Wikipédiából (a MusicBrainz hivatkozása alapján)"));
        wiki->setObjectName("wikiEnabled");
        wiki->setChecked(cur.wikiEnabled);
        root->addWidget(wiki);
        small(TR("A MusicBrainz kulcs nélkül használható, másodpercenként legfeljebb egy lekéréssel (ezt a program betartja). "
                 "A MusicBrainz adatai szabadon felhasználhatók; a szolgáltatás nem kereskedelmi célra ingyenes. A Wikipédia "
                 "szövegei a CC BY-SA licenc alatt állnak."));
        cap(TR("THEAUDIODB"));
        auto *on = new QCheckBox(TR("Előadói adatok a TheAudioDB-ből"));
        on->setObjectName("adbEnabled");
        on->setChecked(cur.enabled);
        root->addWidget(on);
        auto *form = new QFormLayout;
        auto *key = new QLineEdit(cur.key);
        key->setObjectName("adbKey");
        key->setPlaceholderText(QStringLiteral("123"));
        auto *freeKey = new QToolButton;
        freeKey->setText(TR("Ingyenes kulcs"));
        freeKey->setToolTip(TR("Vissza az ingyenes „123” kulcsra"));
        auto *keyRow = new QHBoxLayout;
        keyRow->addWidget(key, 1);
        keyRow->addWidget(freeKey);
        form->addRow(TR("TheAudioDB API-kulcs"), keyRow);
        root->addLayout(form);
        small(TR("Az ingyenes kulcs (123) jelenleg csak mintaadatot ad (bármire ugyanazt az előadót); a nem egyező választ a "
                 "program eldobja, így valódi adathoz saját (Premium) kulcs kell. Azzal percenként legfeljebb 100 lekérés "
                 "mehet, és a számok albuma és éve is kitöltődik. Kulcs: "
                 "<a href=\"https://www.theaudiodb.com/free_music_api\">theaudiodb.com</a>. A felhasználási feltételeket "
                 "kereskedelmi rádióban érdemes elolvasni."));
        auto *state = small(QString());
        state->setObjectName("adbState");
        auto *clear = new QPushButton(TR("Letöltött adatok törlése"));
        clear->setToolTip(TR("A lekért előadói adatok és fotók törlése; bekapcsolva a program újra lekéri őket."));
        auto *row = new QHBoxLayout;
        row->addWidget(clear);
        row->addStretch(1);
        root->addLayout(row);
        root->addStretch(1);
        auto refresh = [state, info] {
            state->setText(TR("MusicBrainz: %1").arg(info->mbStatusText()) + QStringLiteral("\n") + TR("TheAudioDB: %1").arg(info->statusText()));
        };
        refresh();
        connect(info, &ArtistInfoService::statusChanged, state, refresh);
        auto apply = [=, this] {
            ArtistInfoService::Settings ns;
            ns.enabled = on->isChecked();
            ns.key = key->text().trimmed().isEmpty() ? QStringLiteral("123") : key->text().trimmed();
            ns.mode = mode->currentData().toInt();
            ns.mbEnabled = mb->isChecked();
            ns.wikiEnabled = wiki->isChecked();
            wiki->setEnabled(ns.mbEnabled);
            const ArtistInfoService::Settings old = info->settings();
            if (ns.enabled == old.enabled && ns.key == old.key && ns.mode == old.mode && ns.mbEnabled == old.mbEnabled
                && ns.wikiEnabled == old.wikiEnabled)
                return;
            info->setSettings(ns);
            main_->refreshPlayersArtistInfo();
        };
        connect(on, &QCheckBox::toggled, this, apply);
        connect(mb, &QCheckBox::toggled, this, apply);
        connect(wiki, &QCheckBox::toggled, this, apply);
        wiki->setEnabled(cur.mbEnabled);
        connect(key, &QLineEdit::editingFinished, this, apply);
        connect(mode, &QComboBox::activated, this, apply);
        connect(freeKey, &QToolButton::clicked, this, [key, apply] {
            key->setText(QStringLiteral("123"));
            apply();
        });
        connect(clear, &QPushButton::clicked, this, [this, info] {
            if (QMessageBox::question(this, TR("Előadói adatok"), TR("Törlöd a lekért előadói adatokat és fotókat?"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            info->clearCache();
            main_->refreshPlayersArtistInfo();
        });
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

    root = newPage(TR("SQL szerver"));
    {
        // Külső MySQL / MariaDB szerver: fő tároló, helyi tartalékkal (db/ServerSync.h).
        auto cap = [&](const QString &t) {
            auto *c = new QLabel(t);
            c->setObjectName("caption");
            root->addWidget(c);
        };
        auto small = [&](const QString &t) {
            auto *l = new QLabel(t);
            l->setObjectName("small");
            l->setWordWrap(true);
            root->addWidget(l);
            return l;
        };
        cap(TR("KÜLSŐ SQL SZERVER (MYSQL / MARIADB)"));
        small(TR("A szerver a fő tároló: a hangtár, az ütemező, a playlistek, a mixek, a beállítások és az adásnapló minden "
                 "változása néhány másodpercen belül felkerül rá. Ezen a gépen helyi tartalék is marad, így ha a szerver vagy "
                 "a hálózat kiesik, az adás zavartalanul megy tovább, a változások pedig a kapcsolat visszatértével kerülnek "
                 "fel. Egyszerre egy gép ír a szerverre; induláskor az Éter letölti, ha egy másik gép újabb adatot hagyott ott. "
                 "A hangfájlok nem kerülnek fel: a hangtár az elérési útjukat tárolja."));
        const sqlsrv::Config cfg = sqlsrv::Config::load();
        auto *form = new QFormLayout;
        auto *host = new QLineEdit(cfg.host);
        host->setObjectName("sqlHost");
        host->setPlaceholderText(TR("például db.example.com vagy 192.168.1.10"));
        auto *port = new QSpinBox;
        port->setObjectName("sqlPort");
        port->setRange(1, 65535);
        port->setValue(cfg.port);
        port->setMaximumWidth(120);
        auto *user = new QLineEdit(cfg.user);
        user->setObjectName("sqlUser");
        auto *pass = new QLineEdit(cfg.password);
        pass->setObjectName("sqlPassword");
        pass->setEchoMode(QLineEdit::Password);
        auto *dbName = new QLineEdit(cfg.database);
        dbName->setObjectName("sqlDatabase");
        form->addRow(TR("Kiszolgáló (cím)"), host);
        form->addRow(TR("Port"), port);
        form->addRow(TR("Felhasználó"), user);
        form->addRow(TR("Jelszó"), pass);
        form->addRow(TR("Adatbázis neve"), dbName);
        root->addLayout(form);
        auto current = [=] {
            sqlsrv::Config c = sqlsrv::Config::load();
            c.host = host->text().trimmed();
            c.port = port->value();
            c.user = user->text();
            c.password = pass->text();
            c.database = dbName->text().trimmed();
            return c;
        };
        auto *testRow = new QHBoxLayout;
        auto *test = new QPushButton(TR("Kapcsolat tesztelése"));
        test->setObjectName("sqlTest");
        testRow->addWidget(test);
        testRow->addStretch(1);
        root->addLayout(testRow);
        auto *result = small(QString());
        result->setObjectName("sqlTestResult");
        result->setTextInteractionFlags(Qt::TextSelectableByMouse);

        cap(TR("ADATOK"));
        auto *state = small(QString());
        state->setObjectName("sqlState");
        auto *actRow = new QHBoxLayout;
        auto *up = new QPushButton(TR("Feltöltés a szerverre (migrálás)"));
        up->setObjectName("sqlUpload");
        up->setToolTip(TR("Ennek a gépnek a teljes adatbázisa felkerül a szerverre (ami ott volt, lecserélődik), és ettől kezdve "
                          "a szerver a fő tároló."));
        auto *down = new QPushButton(TR("Letöltés a szerverről"));
        down->setObjectName("sqlDownload");
        down->setToolTip(TR("A szerveren lévő adatbázis kerül ide a helyi helyére (előtte biztonsági mentés készül); a program "
                            "újraindul, és ettől kezdve a szerverrel dolgozik."));
        auto *off = new QPushButton(TR("Kapcsolat bontása"));
        off->setObjectName("sqlDisconnect");
        off->setToolTip(TR("Az Éter ezután csak a helyi adatbázissal dolgozik; a szerveren lévő adat megmarad."));
        actRow->addWidget(up);
        actRow->addWidget(down);
        actRow->addWidget(off);
        actRow->addStretch(1);
        root->addLayout(actRow);
        small(TR("A jelszót a program ezen a gépen, a felhasználó beállításai között tárolja. A felhasználónak a megadott "
                 "adatbázisban táblát létrehozni, módosítani, írni és olvasni kell tudnia. Az Éter táblái „eter_” előtaggal "
                 "jönnek létre."));
        root->addStretch(1);

        ServerSync *sync = main_->serverSync();
        auto refresh = [=] {
            const sqlsrv::Config c = sqlsrv::Config::load();
            const QString st = !c.enabled ? TR("Kikapcsolva: az Éter csak a helyi adatbázissal dolgozik.")
                               : sync      ? TR("Bekapcsolva · %1").arg(sync->statusText())
                                           : TR("Bekapcsolva");
            state->setText(st);
            off->setEnabled(c.enabled);
        };
        refresh();
        if (sync) connect(sync, &ServerSync::statusChanged, state, refresh);
        // A mezők mentése (a kapcsolódás adatai); bekapcsolt szinkronnál a háttérszál újrakapcsolódik.
        auto saveFields = [=] {
            const sqlsrv::Config c = current();
            const sqlsrv::Config old = sqlsrv::Config::load();
            if (c.host == old.host && c.port == old.port && c.user == old.user && c.password == old.password && c.database == old.database)
                return;
            c.save();
            if (c.enabled && sync) {
                sync->stop();
                sync->start();
            }
        };
        for (QLineEdit *e : {host, user, pass, dbName}) connect(e, &QLineEdit::editingFinished, this, saveFields);
        connect(port, &QSpinBox::editingFinished, this, saveFields);

        auto describe = [](const sqlsrv::ServerInfo &i) {
            if (!i.ok) return TR("Nem sikerült kapcsolódni: %1").arg(i.error);
            QString t = TR("Kapcsolódva: %1.").arg(i.version);
            if (!i.hasData) return t + QLatin1Char(' ') + TR("A szerveren még nincs Éter-adat.");
            const QDateTime hb = QDateTime::fromSecsSinceEpoch(i.heartbeat);
            return t + QLatin1Char(' ') + TR("Éter-adat a szerveren: %1 elem a hangtárban; utoljára a(z) „%2” gép írta (%3).")
                                             .arg(i.items).arg(i.writerName, i18n::locale().toString(hb, QLocale::ShortFormat));
        };
        connect(test, &QPushButton::clicked, this, [=] {
            saveFields();
            const sqlsrv::Config c = current();
            if (!c.complete()) {
                result->setText(TR("Add meg a kiszolgálót, a felhasználót és az adatbázis nevét."));
                return;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const sqlsrv::ServerInfo i = sqlsrv::probe(c);
            QApplication::restoreOverrideCursor();
            result->setText(describe(i));
            result->setStyleSheet(i.ok ? QStringLiteral("color: #45d483;") : QStringLiteral("color: #ff8f85;"));
        });
        auto progressRun = [this](const QString &title, const std::function<bool(const sqlsrv::Progress &)> &job) {
            QProgressDialog dlg(title, TR("Mégse"), 0, 100, this);
            dlg.setWindowModality(Qt::WindowModal);
            dlg.setMinimumDuration(0);
            dlg.setValue(0);
            return job([&dlg](int pct, const QString &what) {
                dlg.setValue(pct);
                if (!what.isEmpty()) dlg.setLabelText(TR("%1 (%2)").arg(dlg.windowTitle(), what));
                QApplication::processEvents();
                return !dlg.wasCanceled();
            });
        };
        connect(up, &QPushButton::clicked, this, [=, this] {
            saveFields();
            sqlsrv::Config c = current();
            if (!c.complete()) {
                result->setText(TR("Add meg a kiszolgálót, a felhasználót és az adatbázis nevét."));
                return;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const sqlsrv::ServerInfo i = sqlsrv::probe(c);
            QApplication::restoreOverrideCursor();
            result->setText(describe(i));
            if (!i.ok) return;
            QString ask = TR("Ennek a gépnek a teljes adatbázisa felkerül a szerverre, és ettől kezdve a szerver a fő tároló.");
            if (i.hasData)
                ask += QStringLiteral("\n\n") + TR("A szerveren már van Éter-adat (%1 elem, utoljára a(z) „%2” gép írta). Ez lecserélődik.")
                                                     .arg(i.items).arg(i.writerName);
            if (!qEnvironmentVariableIsSet("ETER_SQL_AUTOCONFIRM")
                && QMessageBox::question(this, TR("Feltöltés a szerverre"), ask + QStringLiteral("\n\n") + TR("Folytatod?"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            if (sync) sync->stop();
            QString err;
            const QString path = main_->database()->path();
            const bool ok = progressRun(TR("Feltöltés a szerverre"), [&](const sqlsrv::Progress &p) { return sqlsrv::upload(c, path, p, &err); });
            if (!ok) {
                QMessageBox::warning(this, TR("Feltöltés a szerverre"), TR("A feltöltés nem sikerült:\n%1").arg(err));
                if (sync && sqlsrv::Config::load().enabled) sync->start();
                return;
            }
            c.enabled = true;
            c.save();
            if (sync) sync->start();
            result->setText(TR("Feltöltve. A szerver mostantól a fő tároló; ez a gép ír rá."));
            result->setStyleSheet(QStringLiteral("color: #45d483;"));
            refresh();
        });
        connect(down, &QPushButton::clicked, this, [=, this] {
            saveFields();
            sqlsrv::Config c = current();
            if (!c.complete()) {
                result->setText(TR("Add meg a kiszolgálót, a felhasználót és az adatbázis nevét."));
                return;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const sqlsrv::ServerInfo i = sqlsrv::probe(c);
            QApplication::restoreOverrideCursor();
            result->setText(describe(i));
            if (!i.ok) return;
            if (!i.hasData) {
                QMessageBox::information(this, TR("Letöltés a szerverről"), TR("A szerveren még nincs Éter-adat. Előbb tölts fel egy adatbázist."));
                return;
            }
            if (!qEnvironmentVariableIsSet("ETER_SQL_AUTOCONFIRM")
                && QMessageBox::question(this, TR("Letöltés a szerverről"),
                                      TR("A helyi adatbázis lecserélődik a szerveren lévőre (%1 elem a hangtárban). Előtte biztonsági "
                                         "mentés készül róla. A program ezután újraindul.\n\nFolytatod?").arg(i.items),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            if (sync) sync->stop();
            QString err;
            const QString path = main_->database()->path();
            const QString tmp = path + QStringLiteral(".sqlsrv");
            const bool ok = progressRun(TR("Letöltés a szerverről"), [&](const sqlsrv::Progress &p) { return sqlsrv::download(c, path, tmp, p, &err); });
            if (!ok || !main_->stageDatabaseImport(tmp, &err)) {
                QFile::remove(tmp);
                QMessageBox::warning(this, TR("Letöltés a szerverről"), TR("A letöltés nem sikerült:\n%1").arg(err));
                if (sync && sqlsrv::Config::load().enabled) sync->start();
                return;
            }
            QFile::remove(tmp);
            c.enabled = true;
            c.save();
            if (qEnvironmentVariableIsSet("ETER_SQL_NORELAUNCH")) {
                result->setText(TR("Letöltve; újraindításkor lép életbe."));
                return;
            }
            QTimer::singleShot(300, main_, [m = main_] { m->relaunch(QCoreApplication::arguments().mid(1)); });
        });
        connect(off, &QPushButton::clicked, this, [=, this] {
            if (!qEnvironmentVariableIsSet("ETER_SQL_AUTOCONFIRM")
                && QMessageBox::question(this, TR("Kapcsolat bontása"),
                                      TR("Az Éter ezután csak a helyi adatbázissal dolgozik; ami a szerveren van, megmarad. Folytatod?"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            if (sync) sync->stop();
            sqlsrv::Config c = current();
            c.enabled = false;
            c.save();
            QSqlDatabase local = QSqlDatabase::database();
            sqlsrv::removeTriggers(local);
            refresh();
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
    makeColumnsResizable(users, QStringLiteral("remoteUsers"));
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
