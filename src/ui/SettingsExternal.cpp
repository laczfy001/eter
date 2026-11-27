// Beállítások → Külső playlistek: figyelt mappa, fájlnév minta, formátum, beolvasás, útvonalak, kategóriák.
#include "ui/SettingsDialog.h"

#include "i18n.h"
#include "sched/ExternalPlaylist.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString mmss(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QTableWidget *table(const QStringList &headers, const char *name)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setStretchLastSection(true);
    t->horizontalHeader()->setHighlightSections(false);
    t->setShowGrid(false);
    return t;
}

} // namespace

void SettingsDialog::addExternalPage(QVBoxLayout *root)
{
    Database *db = main_->database();
    ExternalPlaylists *ext = main_->externalPlaylists();
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
    auto check = [&](const QString &text, const char *name, const QString &key, const QString &def) {
        auto *c = new QCheckBox(text);
        c->setObjectName(name);
        c->setChecked(db->setting(key, def) == "1");
        root->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [db, key](bool on) { db->setSetting(key, on ? "1" : "0"); });
        return c;
    };

    small(TR("Más ütemező programok (például StationPlaylist Creator) M3U playlistjei egy figyelt mappából. A heti "
             "rácsban a „Külső playlist” sémájú órák (és a séma nélküliek) a fájlokból készülnek; minden óra fix "
             "időponttal indul. Ha egy órához nincs fájl, a tartalék sémából generál."));

    // --------------------------------------------------- figyelt mappa
    cap(TR("FIGYELT MAPPA"));
    auto *enabled = new QCheckBox(TR("Külső playlistek beolvasása"));
    enabled->setObjectName("extEnabled");
    enabled->setChecked(db->setting("ext_enabled", "0") == "1");
    root->addWidget(enabled);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *dirRow = new QHBoxLayout;
    auto *dir = new QLineEdit(db->setting("ext_dir"));
    dir->setObjectName("extDir");
    dir->setPlaceholderText(TR("a mappa, ahová az ütemező a playlisteket írja"));
    auto *browse = new QPushButton(TR("Tallózás…"));
    dirRow->addWidget(dir, 1);
    dirRow->addWidget(browse);
    form->addRow(TR("Mappa"), dirRow);
    auto *patRow = new QHBoxLayout;
    auto *pattern = new QLineEdit(db->setting("ext_pattern", ext::defaultPattern()));
    pattern->setObjectName("extPattern");
    patRow->addWidget(pattern, 1);
    for (const QString &tok : {TR("{év}"), TR("{hónap}"), TR("{hó}"), TR("{nap}"), TR("{óra}")}) {
        auto *b = new QPushButton(tok);
        b->setObjectName("tokenButton");
        b->setFocusPolicy(Qt::NoFocus);
        patRow->addWidget(b);
        connect(b, &QPushButton::clicked, pattern, [pattern, tok] {
            pattern->insert(tok);
            pattern->setFocus();
            emit pattern->editingFinished();
        });
    }
    form->addRow(TR("Fájlnév minta"), patRow);
    root->addLayout(form);
    small(TR("{hónap} = jan, feb … nov, dec (angolul vagy magyarul), {hó} = 01–12, {nap} = 1–31, {óra} = 00–23, {év} = "
             "2026 vagy 26 (ha nincs, a mához legközelebbi év). A 00 órás fájl, ha jóval hosszabb egy óránál, egész "
             "napos: a program a hosszak alapján bontja órákra."));

    auto *files = table({TR("Fájl"), TR("Adás"), TR("Formátum"), TR("Elem"), TR("Hossz"), TR("Állapot")}, "extFiles");
    files->setMinimumHeight(180);
    root->addWidget(files);

    // --------------------------------------------------- formátum
    cap(TR("FORMÁTUM ÉS IDŐZÍTÉS"));
    auto *fform = new QFormLayout;
    auto *encoding = new QComboBox;
    encoding->setObjectName("extEncoding");
    encoding->addItem(TR("Automatikus (UTF-8, különben a Windows kódlapja)"), "auto");
    encoding->addItem(QStringLiteral("UTF-8"), "utf8");
    encoding->addItem(TR("Windows-1250 (közép-európai)"), "cp1250");
    encoding->addItem(TR("Windows-1252 (nyugat-európai)"), "cp1252");
    encoding->setCurrentIndex(std::max(0, encoding->findData(db->setting("ext_encoding", "auto"))));
    fform->addRow(TR("Formátum"), new QLabel(TR("M3U és #EXTM3U (felismeri)")));
    fform->addRow(TR("Kódolás"), encoding);
    root->addLayout(fform);
    check(TR("Minden óra fix időponttal indul (óra:00:00); túlfutásnál a „Fix időpontkor” szabály dönt"), "extFixedHour", "ext_fixed_hour", "1");
    check(TR("Egész napos fájl órákra bontása a hosszak alapján"), "extSplitDay", "ext_split_day", "1");
    check(TR("Az új elem címe és előadója az #EXTINF sorból (különben a fájl címkéiből)"), "extTags", "ext_tags", "1");

    // --------------------------------------------------- beolvasás
    cap(TR("BEOLVASÁS"));
    auto *bform = new QFormLayout;
    bform->addRow(TR("Mikor"), new QLabel(TR("az Ütemező „Előre” szabálya szerint (most: %1 óra)")
                                              .arg(db->setting("generate_ahead_hours", "24"))));
    auto *noFile = new QComboBox;
    noFile->setObjectName("extFallback");
    noFile->addItem(TR("az Éter a tartalék sémából generál"), "1");
    noFile->addItem(TR("az óra üresen marad"), "0");
    noFile->setCurrentIndex(std::max(0, noFile->findData(db->setting("ext_fallback", "1"))));
    bform->addRow(TR("Ha nincs fájl"), noFile);
    auto *fallback = new QComboBox;
    fallback->setObjectName("extFallbackClock");
    bform->addRow(TR("Tartalék séma"), fallback);
    root->addLayout(bform);
    auto fillFallback = [db, fallback, ext] {
        fallback->clear();
        const int extClock = ExternalPlaylists::clockId(db);
        const int chosen = ext->fallbackClock();
        for (const Clock &c : db->clocks())
            if (c.id != extClock) fallback->addItem(c.name, c.id);
        if (!fallback->count()) fallback->addItem(TR("(nincs séma az Ütemezőben)"), 0);
        fallback->setCurrentIndex(std::max(0, fallback->findData(chosen)));
    };
    fillFallback();
    check(TR("Ha a fájl megváltozik, újra beolvassa, amíg az óra el nem kezdődik (a kézzel betett elemek maradnak)"),
          "extReload", "ext_reload", "1");
    auto *runRow = new QHBoxLayout;
    auto *runNow = new QPushButton(TR("Beolvasás most"));
    runNow->setObjectName("extRunNow");
    runNow->setProperty("accent", true);
    auto *oneFile = new QPushButton(TR("Egy fájl beolvasása…"));
    oneFile->setObjectName("extImportFile");
    runRow->addWidget(runNow);
    runRow->addWidget(oneFile);
    runRow->addStretch(1);
    root->addLayout(runRow);

    // --------------------------------------------------- útvonalak
    cap(TR("ÚTVONALAK"));
    small(TR("Csak akkor kell, ha az ütemező másik gépen fut, vagy más meghajtóbetűvel látja a zenéket: a playlistben "
             "álló útvonal eleje ezen a gépen erre cserélődik."));
    auto *paths = table({TR("A playlistben"), TR("Ezen a gépen")}, "extPaths");
    paths->setEditTriggers(QAbstractItemView::AllEditTriggers);
    paths->setMinimumHeight(90);
    paths->setMaximumHeight(130);
    paths->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    root->addWidget(paths);
    {
        const QJsonArray map = QJsonDocument::fromJson(db->setting("ext_path_map", "[]").toUtf8()).array();
        for (const QJsonValue &v : map) {
            const int r = paths->rowCount();
            paths->insertRow(r);
            paths->setItem(r, 0, new QTableWidgetItem(v.toArray().at(0).toString()));
            paths->setItem(r, 1, new QTableWidgetItem(v.toArray().at(1).toString()));
        }
    }
    auto *pathRow = new QHBoxLayout;
    auto *addPath = new QPushButton(TR("+ Megfeleltetés"));
    auto *delPath = new QPushButton(TR("Törlés"));
    pathRow->addWidget(addPath);
    pathRow->addWidget(delPath);
    pathRow->addStretch(1);
    root->addLayout(pathRow);
    auto savePaths = [db, paths] {
        QJsonArray map;
        for (int r = 0; r < paths->rowCount(); ++r) {
            const QString from = paths->item(r, 0) ? paths->item(r, 0)->text().trimmed() : QString();
            const QString to = paths->item(r, 1) ? paths->item(r, 1)->text().trimmed() : QString();
            if (!from.isEmpty()) map.append(QJsonArray{from, to});
        }
        db->setSetting("ext_path_map", QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
    };
    connect(paths, &QTableWidget::itemChanged, this, savePaths);
    connect(addPath, &QPushButton::clicked, this, [paths] {
        const int r = paths->rowCount();
        paths->insertRow(r);
        paths->setItem(r, 0, new QTableWidgetItem(QStringLiteral("C:\\")));
        paths->setItem(r, 1, new QTableWidgetItem(QString()));
        paths->editItem(paths->item(r, 0));
    });
    connect(delPath, &QPushButton::clicked, this, [paths, savePaths] {
        if (paths->currentRow() >= 0) paths->removeRow(paths->currentRow());
        savePaths();
    });

    // --------------------------------------------------- kategóriák
    cap(TR("HANGTÁR ÉS KATEGÓRIÁK"));
    small(TR("Ami már a hangtárban van, azt útvonal szerint találja meg (mix pontokkal, hullámformával). Ami nincs, "
             "beolvassa és elemzi; a kategóriája a playlist kategóriájából jön (a Creator „Studio” formátuma megadja)."));
    auto *cats = table({TR("A playlistben"), TR("Éter kategória"), TR("Elem")}, "extCats");
    cats->setMinimumHeight(120);
    cats->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    cats->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(cats);
    root->addStretch(1);

    // --------------------------------------------------- frissítés
    auto refresh = [this, db, ext, files, cats] {
        const QVector<ExtFile> list = ext->scan();
        const QDateTime now = eter::now();
        const int ahead = std::clamp(db->setting("generate_ahead_hours", "24").toInt(), 1, 168);
        const QDateTime thisHour(now.date(), QTime(now.time().hour(), 0));
        files->setRowCount(0);
        QHash<QString, int> catCount;
        for (const ExtFile &f : list) {
            const ext::Parsed p = ext::parse([&] {
                QFile file(f.path);
                return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
            }(), db->setting("ext_encoding", "auto"));
            qint64 total = 0;
            bool known = true;
            for (const ext::Line &l : p.lines) {
                if (l.durationMs < 0) known = false;
                else total += l.durationMs;
                ++catCount[l.category];
            }
            const bool day = f.target.hour < 0;
            const QDateTime start(f.target.date, QTime(day ? 0 : f.target.hour, 0));
            const QDateTime end = start.addSecs(day ? 86400 : 3600);
            QString state;
            QString dot = QStringLiteral("#3fb950");
            if (end <= now) {
                state = day ? TR("elmúlt nap, kimarad") : TR("elmúlt óra, kimarad");
                dot = QStringLiteral("#4a5160");
            } else if (day) {
                int done = 0;
                for (int h = 0; h < 24; ++h)
                    if (ext->sourceOf(f.target.date.toString("yyyyMMdd").toInt() * 100 + h) == f.name) ++done;
                state = TR("egész nap · %1/24 óra beolvasva").arg(done);
                if (!done) dot = QStringLiteral("#5cb0ff");
            } else if (ext->sourceOf(f.target.date.toString("yyyyMMdd").toInt() * 100 + f.target.hour) == f.name) {
                state = start <= now ? TR("beolvasva, adásban") : TR("beolvasva");
                if (known && total > 3600 * 1000 + 15000)
                    state += TR(" · túlfut: +%1 (a következő óra fix indulása levágja)").arg(mmss(total - 3600 * 1000));
            } else {
                const QDateTime when = start.addSecs(-qint64(ahead) * 3600);
                state = when <= thisHour ? TR("vár: a következő percben olvassa be")
                                         : TR("vár: %1-kor olvassa be (Előre %2 óra)").arg(QLocale().toString(when, QStringLiteral("MMM d. HH:mm"))).arg(ahead);
                dot = QStringLiteral("#5cb0ff");
            }
            const int r = files->rowCount();
            files->insertRow(r);
            const QString when = day ? TR("%1, egész nap").arg(QLocale().toString(f.target.date, QStringLiteral("MMM d."))) :
                                       QStringLiteral("%1, %2:00").arg(QLocale().toString(f.target.date, QStringLiteral("MMM d."))).arg(f.target.hour, 2, 10, QLatin1Char('0'));
            const QString fmt = !p.extended ? TR("M3U (útvonalak)") : p.creator ? TR("#EXTM3U (Creator)") : QStringLiteral("#EXTM3U");
            const QStringList cells{f.name, when, fmt + QStringLiteral(" · ") + p.encoding, QString::number(p.lines.size()),
                                    known ? mmss(total) : QStringLiteral("–"), state};
            for (int c = 0; c < cells.size(); ++c) {
                auto *it = new QTableWidgetItem(cells[c]);
                if (c == 5) {
                    QPixmap pm(10, 10);
                    pm.fill(theme::c(dot.toLatin1().constData()));
                    it->setIcon(QIcon(pm));
                }
                if (c == 4 && !day && known && total > 3600 * 1000 + 15000) it->setForeground(theme::c("#f5a524"));
                files->setItem(r, c, it);
            }
        }
        files->resizeColumnsToContents();
        if (!list.isEmpty()) files->horizontalHeader()->setStretchLastSection(true);

        // Kategóriák: a fájlokban szereplők, mindegyikhez egy Éter kategória (vagy „új kategória”).
        const QJsonObject map = QJsonDocument::fromJson(db->setting("ext_cat_map", "{}").toUtf8()).object();
        const QVector<Category> ecats = db->itemCategories();
        cats->setRowCount(0);
        for (const QString &name : ext->categoriesIn(list)) {
            const int r = cats->rowCount();
            cats->insertRow(r);
            cats->setItem(r, 0, new QTableWidgetItem(name.isEmpty() ? TR("(nincs megadva)") : name));
            auto *combo = new QComboBox;
            const QString shown = name.isEmpty() ? TR("Külső playlist") : name;
            combo->addItem(TR("Új kategória: %1").arg(shown), 0);
            int sel = 0;
            for (const Category &c : ecats) {
                QPixmap pm(10, 10);
                pm.fill(QColor(c.color));
                combo->addItem(QIcon(pm), c.name, c.id);
                if (map.value(name).toInt() == c.id || (!map.contains(name) && c.name.compare(shown, Qt::CaseInsensitive) == 0))
                    sel = combo->count() - 1;
            }
            combo->setCurrentIndex(sel);
            connect(combo, &QComboBox::activated, this, [db, combo, name] {
                QJsonObject m = QJsonDocument::fromJson(db->setting("ext_cat_map", "{}").toUtf8()).object();
                if (combo->currentData().toInt()) m.insert(name, combo->currentData().toInt());
                else m.remove(name);
                db->setSetting("ext_cat_map", QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
            });
            cats->setCellWidget(r, 1, combo);
            cats->setItem(r, 2, new QTableWidgetItem(QString::number(catCount.value(name))));
        }
    };

    auto applyEnabled = [=](bool on) {
        for (QWidget *w : std::initializer_list<QWidget *>{dir, browse, pattern, files, encoding, noFile, fallback, runNow, oneFile, paths, addPath, delPath, cats})
            w->setEnabled(on);
    };
    connect(enabled, &QCheckBox::toggled, this, [=](bool on) {
        db->setSetting("ext_enabled", on ? "1" : "0");
        if (on) ext->clockId(true);       // a heti rács „Külső playlist” sémája
        emit db->clocksChanged();
        fillFallback();
        applyEnabled(on);
        refresh();
    });
    connect(dir, &QLineEdit::editingFinished, this, [=] {
        db->setSetting("ext_dir", dir->text().trimmed());
        refresh();
    });
    connect(browse, &QPushButton::clicked, this, [=] {
        const QString d = QFileDialog::getExistingDirectory(this, TR("A playlistek mappája"), dir->text());
        if (d.isEmpty()) return;
        dir->setText(QDir::toNativeSeparators(d));
        emit dir->editingFinished();
    });
    connect(pattern, &QLineEdit::editingFinished, this, [=] {
        db->setSetting("ext_pattern", pattern->text().trimmed().isEmpty() ? ext::defaultPattern() : pattern->text().trimmed());
        refresh();
    });
    connect(encoding, &QComboBox::activated, this, [=] {
        db->setSetting("ext_encoding", encoding->currentData().toString());
        refresh();
    });
    connect(noFile, &QComboBox::activated, this, [=] {
        db->setSetting("ext_fallback", noFile->currentData().toString());
        fallback->setEnabled(noFile->currentData().toString() == "1");
    });
    connect(fallback, &QComboBox::activated, this, [=] { db->setSetting("ext_fallback_clock", fallback->currentData().toString()); });
    connect(runNow, &QPushButton::clicked, this, [=] {
        MainWindow::showExternalReport(this, main_->runExternalNow());
        refresh();
    });
    connect(oneFile, &QPushButton::clicked, this, [=] {
        const QString path = QFileDialog::getOpenFileName(this, TR("Playlist beolvasása"), dir->text(),
                                                          TR("Playlistek (*.m3u *.m3u8 *.M3U);;Minden fájl (*)"));
        if (path.isEmpty()) return;
        QString error;
        QVector<ExtReport> reports = main_->importExternalFile(path, &error);
        if (reports.isEmpty() && !error.isEmpty()) {
            ExtReport r;
            r.file = QFileInfo(path).fileName();
            r.error = error;
            reports << r;
        }
        MainWindow::showExternalReport(this, reports);
        refresh();
    });
    applyEnabled(enabled->isChecked());
    fallback->setEnabled(enabled->isChecked() && noFile->currentData().toString() == "1");
    refresh();
}
