#include "audio/Engine.h"
#include "db/Database.h"
#include "ui/Library.h"
#include "ui/MainWindow.h"
#include "ui/MixEditorPage.h"
#include "ui/SchedulerPage.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include "ui/MiniMix.h"
#include "ui/TimedEventsPage.h"
#include <QKeyEvent>
#include <QSettings>
#include "i18n.h"
#include <QSpinBox>
#include <QLineEdit>
#include "ui/ItemEditor.h"
#include "ui/CartwallPanel.h"
#include "ui/PlaylistTable.h"
#include "ui/PlaylistEditorPage.h"
#include <QTableView>
#include <QCommandLineParser>
#include <QDateTime>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QToolButton>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTableWidget>
#include <QCursor>
#include <QAbstractButton>
#include <random>
#include <QPointer>
#include <QContextMenuEvent>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QLockFile>
#include "ui/RemoteEditor.h"
#include "net/Remote.h"
#include <QTemporaryDir>
#include <QNetworkProxy>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

// Hangmotor önteszt felület nélkül: betöltés, lejátszás, pozicionálás,
// fájlvég, kiúsztatás és két csatorna keverése egy kimenetre.
static int selfTest(const QString &file, const QString &backend)
{
    using namespace std::chrono_literals;
    using eter::Engine;
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        std::printf("%s  %s\n", ok ? "OK  " : "HIBA", what);
        if (!ok) ++failures;
    };

    Engine engine;
    std::string err;
    check(engine.init(backend.toStdString(), &err), "hangrendszer indul");
    std::printf("      háttérrendszer: %s\n", engine.backendName().c_str());
    for (const auto &d : engine.devices())
        std::printf("      kimenet: %s%s\n", d.name.c_str(), d.isDefault ? " (alapértelmezett)" : "");
    for (int ch = 0; ch < Engine::ChannelCount; ++ch)
        check(engine.setRoute(static_cast<Engine::Channel>(ch), {}, &err), "csatorna kimenetre kötve");

    // Frissen megnyitott kimenet (főleg alvó állapotból) akár 1 mp késéssel kezd adatot kérni.
    std::this_thread::sleep_for(1500ms);

    auto &a = engine.source(Engine::Player1);
    auto &b = engine.source(Engine::Player2);
    check(a.load(file.toStdString(), &err), "fájl betöltése az 1-es lejátszóba");
    check(b.load(file.toStdString(), &err), "fájl betöltése a 2-es lejátszóba");
    if (failures) {
        std::printf("      %s\n", err.c_str());
        return 1;
    }

    std::vector<float> peaks;
    int64_t total = 0;
    std::atomic<bool> cancel{false};
    check(eter::scanPeaks(file.toStdString(), 1200, peaks, total, cancel), "hullámforma elkészül");
    std::printf("      hossz: %.2f s (konténer szerint %.2f s)\n", total / 48000.0, a.duration() / 48000.0);
    a.setDuration(total);
    b.setDuration(total);

    float l, r;
    a.play();
    std::this_thread::sleep_for(1500ms);
    const double pos = a.position() / 48000.0;
    std::printf("      1,5 s után a pozíció: %.2f s\n", pos);
    check(pos > 1.2 && pos < 1.8, "a lejátszás valós időben halad");
    a.takePeaks(l, r);
    check(l > 0.f || r > 0.f, "van jel a szintmérőn");

    // Két csatorna egyszerre ugyanazon a kimeneten
    b.play();
    std::this_thread::sleep_for(500ms);
    b.takePeaks(l, r);
    check(a.playing() && b.playing() && (l > 0.f || r > 0.f), "két lejátszó együtt szól egy kimeneten");

    b.fadeOutAndStop(400);
    std::this_thread::sleep_for(900ms);
    check(!b.playing() && b.takeFinished(), "kiúsztatás után megáll");

    const int64_t target = std::max<int64_t>(0, total - 48000);
    a.seek(target);
    std::this_thread::sleep_for(300ms);
    const double after = a.position() / 48000.0;
    std::printf("      pozicionálás %.2f s-re, 0,3 s múlva: %.2f s\n", target / 48000.0, after);
    check(after >= target / 48000.0 && after < target / 48000.0 + 0.6, "pozicionálás pontos");
    std::this_thread::sleep_for(1500ms);
    check(!a.playing() && a.takeFinished(), "a fájl végén megáll és jelez");
    std::printf("      végpozíció: %.2f s\n", a.position() / 48000.0);
    check(std::llabs(a.position() - total) < 4800, "a végpozíció egyezik a hosszal");

    a.stop();
    std::this_thread::sleep_for(100ms);
    check(a.position() == 0, "stop után az elejére áll");

    // Útválasztás váltása: külön eszközre, majd vissza közösre
    const auto devs = engine.devices();
    if (!devs.empty()) {
        check(engine.setRoute(Engine::Player2, devs.front().name, &err), "2-es lejátszó átkötése névvel megadott kimenetre");
        check(engine.setRoute(Engine::Player2, {}, &err), "visszakötés az alapértelmezettre");
    }
    if (devs.size() >= 2) {
        // Két külön kimenet egyszerre: mindkét lejátszó a sajátján halad.
        check(engine.setRoute(Engine::Player1, devs[0].name, &err), "1-es lejátszó az első kimeneten");
        check(engine.setRoute(Engine::Player2, devs[1].name, &err), "2-es lejátszó a második kimeneten");
        b.stop();
        std::this_thread::sleep_for(1500ms);
        a.play();
        b.play();
        std::this_thread::sleep_for(1000ms);
        const double pa = a.position() / 48000.0, pb = b.position() / 48000.0;
        std::printf("      1 s után: 1-es %.2f s, 2-es %.2f s\n", pa, pb);
        check(pa > 0.7 && pa < 1.3 && pb > 0.7 && pb < 1.3, "két külön kimenet egyszerre szól");
        a.stop();
        b.stop();
        check(engine.setRoute(Engine::Player1, {}, &err) && engine.setRoute(Engine::Player2, {}, &err),
              "vissza az alapértelmezett kimenetre");
    }
    check(!engine.setRoute(Engine::Pfl, "nem létező kimenet", &err), "nem létező kimenet hibát ad");
    check(engine.setRoute(Engine::Pfl, {}, &err), "hiba után újra beköthető");

    engine.shutdown();
    std::printf("%s\n", failures ? "ÖNTESZT SIKERTELEN" : "ÖNTESZT RENDBEN");
    return failures ? 1 : 0;
}

// Szerkesztő oldali forgatókönyv: csatlakozás, majd sorban: átrendezés, mix mentése (a Mix editoron át),
// ráfektetés, törlés; végül a playouttól visszakapott állapot.
static void runEditorTest(RemoteEditorWindow &editor)
{
    auto step = std::make_shared<int>(0);
    QObject::connect(editor.client(), &remote::Client::failed, &editor, [](const QString &why) {
        std::printf("szerkesztő: nem sikerült: %s\n", qPrintable(why));
        std::fflush(stdout);
        QApplication::exit(2);
    });
    QObject::connect(editor.client(), &remote::Client::loggedIn, &editor, [](const QString &v) {
        std::printf("szerkesztő: belépve (playout %s)\n", qPrintable(v));
    });
    QObject::connect(&editor, &RemoteEditorWindow::opAnswered, &editor, [](int id, bool ok, const QString &err) {
        std::printf("szerkesztő: válasz #%d: %s %s\n", id, ok ? "rendben" : "elutasítva", qPrintable(err));
        std::fflush(stdout);
    });
    auto idOf = [&editor](const QString &title) {
        QSqlQuery q;
        q.prepare("SELECT e.id FROM playlist_entries e JOIN items i ON i.id = e.item_id WHERE i.title = ? ORDER BY e.id LIMIT 1");
        q.addBindValue(title);
        q.exec();
        Q_UNUSED(editor);
        return q.next() ? q.value(0).toInt() : 0;
    };
    QObject::connect(&editor, &RemoteEditorWindow::stateApplied, &editor, [&editor, step, idOf] {
        if (*step != 0) return;
        *step = 1;
        std::printf("szerkesztő: állapot: %s\n", qPrintable(editor.debugState()));
        {
            QSqlQuery q("SELECT file_path FROM items WHERE title = 'a'");
            if (q.next()) std::printf("szerkesztő: „a” fájlja itt: %s (megvan: %d)\n", qPrintable(q.value(0).toString()),
                                      QFileInfo::exists(q.value(0).toString()));
        }
        const int a = idOf("a"), b = idOf("hirek"), c = idOf("b"), d = idOf("c");
        QTimer::singleShot(500, &editor, [&editor, d, c] {
            std::printf("szerkesztő: „c” áthelyezése „b” elé\n");
            editor.requestMove(d, c);
        });
        QTimer::singleShot(1800, &editor, [&editor, a, b] {
            std::printf("szerkesztő: egyedi mix a → hirek, 5,0 mp (a Mix editoron át)\n");
            editor.mixPage()->quickSetMix(a, b, 5000);
        });
        QTimer::singleShot(3100, &editor, [&editor, b] {
            std::printf("szerkesztő: „hirek” ráfektetése\n");
            editor.requestOverlay(b, true);
        });
        QTimer::singleShot(4400, &editor, [&editor, c] {
            std::printf("szerkesztő: „b” törlése\n");
            editor.requestRemove(c);
        });
        QTimer::singleShot(5700, &editor, [&editor, a] {
            std::printf("szerkesztő: a lejátszott/szóló elem áthelyezése (el kell utasítani)\n");
            editor.requestMove(a, 0);
        });
        QTimer::singleShot(7500, &editor, [&editor] {
            std::printf("szerkesztő: végső állapot: %s\n", qPrintable(editor.debugState()));
            std::printf("szerkesztő: figyelmeztetés: %s\n", editor.warningText().isEmpty() ? "nincs" : qPrintable(editor.warningText()));
            editor.grab().save("editor.png");
            std::fflush(stdout);
            QApplication::exit(0);
        });
    });
}

int main(int argc, char **argv)
{
    // Alapból X11 (Wayland alatt XWayland) – ott a rendszer rajzolja az ablakkeretet;
    // ha az nem érhető el, natív Wayland. A QT_QPA_PLATFORM változóval felülbírálható.
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb;wayland");
#endif
    // Az önteszthez nem kell kijelző: felület nélküli alkalmazásként fut.
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]).startsWith("--selftest")) headless = true;
    std::unique_ptr<QCoreApplication> holder(headless ? new QCoreApplication(argc, argv)
                                                      : new QApplication(argc, argv));
    QCoreApplication &app = *holder;
    QApplication::setOrganizationName("Eter");
    QApplication::setApplicationName("Eter");
    if (!headless) QApplication::setApplicationDisplayName(QStringLiteral("Éter"));
    QApplication::setApplicationVersion(ETER_VERSION);
    if (!headless) QApplication::setDesktopFileName("eter");

    // A felület nyelve: alapból angol, a Beállításokban váltható (újraindítás után érvényes).
    // A fejlesztői forgatókönyvek a magyar feliratokra hivatkoznak, ezért azok magyarul futnak.
    {
        bool testRun = false;
        for (int i = 1; i < argc; ++i)
            if (QByteArray(argv[i]).startsWith("--") && QByteArray(argv[i]).contains("test")) testRun = true;
        QString lang = qEnvironmentVariable("ETER_LANG");
        if (lang.isEmpty()) lang = testRun ? QStringLiteral("hu") : QSettings().value("ui/language", QStringLiteral("en")).toString();
        i18n::setLanguage(lang);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Éter – rádiós adáslebonyolító"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption backendOpt("backend", QStringLiteral("Hangrendszer: pulseaudio, alsa, jack (Linux), wasapi, dsound (Windows) vagy null."), "név");
    QCommandLineOption selfTestOpt("selftest", QStringLiteral("Hangmotor önteszt a megadott hangfájllal."), "fájl");
    QCommandLineOption shotOpt("screenshot", QStringLiteral("Képernyőkép mentése és kilépés (fejlesztéshez)."), "png");
    QCommandLineOption iconOpt("write-icon", QStringLiteral("Az alkalmazásikon mentése PNG-be."), "png");
    QCommandLineOption uiTestOpt("uitest", QStringLiteral("Felületi forgatókönyv futtatása (fejlesztéshez)."));
    QCommandLineOption dbOpt("db", QStringLiteral("Az adatbázisfájl helye (alapból ~/.local/share/eter/library.sqlite)."), "fájl");
    QCommandLineOption importTestOpt("importtest", QStringLiteral("Importálás kipróbálása a megadott mappával (fejlesztéshez)."), "mappa");
    QCommandLineOption plTestOpt("pltest", QStringLiteral("Playlist szerkesztő forgatókönyv (fejlesztéshez)."));
    QCommandLineOption cartTestOpt("carttest", QStringLiteral("Cartwall forgatókönyv (fejlesztéshez)."));
    QCommandLineOption editTestOpt("edittest", QStringLiteral("Elemszerkesztő forgatókönyv (fejlesztéshez)."));
    QCommandLineOption nextTestOpt("nexttest", QStringLiteral("Asszisztált váltás forgatókönyv (fejlesztéshez)."));
    QCommandLineOption eventTestOpt("eventtest", QStringLiteral("Időzítések forgatókönyve (fejlesztéshez)."));
    QCommandLineOption editorOpt("editor", QStringLiteral("Szerkesztő mód: egy másik gépen futó playout playlistjének szerkesztése a hálózaton át."));
    QCommandLineOption serveTestOpt("servetest", QStringLiteral("Távoli szerkesztés: playout oldali forgatókönyv (fejlesztéshez)."));
    QCommandLineOption overlayTestOpt("overlaytest", QStringLiteral("Ráfektetés forgatókönyv (fejlesztéshez)."));
    QCommandLineOption lookTestOpt("looktest", QStringLiteral("A playlist megjelenése (fejlesztéshez)."));
    QCommandLineOption mixTestOpt("mixtest", QStringLiteral("Mix forgatókönyv (fejlesztéshez)."));
    QCommandLineOption recTestOpt("rectest", QStringLiteral("Felvétel forgatókönyv (fejlesztéshez)."));
    QCommandLineOption schedTestOpt("schedtest", QStringLiteral("Ütemező forgatókönyv (fejlesztéshez)."));
    QCommandLineOption fakeTimeOpt("fake-time", QStringLiteral("A program órája ettől az időponttól indul (fejlesztéshez)."), "ÓÓ:pp:mm");
    parser.addOptions({backendOpt, dbOpt, selfTestOpt, shotOpt, iconOpt, uiTestOpt, importTestOpt, schedTestOpt, fakeTimeOpt, mixTestOpt, recTestOpt, plTestOpt, cartTestOpt, editTestOpt, nextTestOpt, eventTestOpt, lookTestOpt, overlayTestOpt, editorOpt, serveTestOpt});
    parser.addPositionalArgument("fájlok", QStringLiteral("Induláskor importálandó hangfájlok vagy mappák."), "[fájlok…]");
    parser.process(app);

    if (parser.isSet(iconOpt))
        return theme::appIcon().pixmap(256, 256).save(parser.value(iconOpt)) ? 0 : 1;
    if (parser.isSet(selfTestOpt))
        return selfTest(parser.value(selfTestOpt), parser.isSet(backendOpt) ? parser.value(backendOpt) : "null");

    theme::apply(*static_cast<QApplication *>(holder.get()));
    // A távoli szerkesztés közvetlenül az IP-címre csatlakozik: proxy sosem kell (és így nem is keresünk).
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    if (parser.isSet(fakeTimeOpt)) {
        const QDateTime real = QDateTime::currentDateTime();
        const QDateTime fake(real.date(), QTime::fromString(parser.value(fakeTimeOpt), "HH:mm:ss"));
        if (fake.isValid()) eter::setClockOffsetMs(real.msecsTo(fake));
    }
    if (parser.isSet(editorOpt) && qEnvironmentVariableIsSet("ETER_MAPTEST")) {
        // Útvonal-megfeleltetés próbája: a playout útvonala → a szerkesztő gépen (fejlesztéshez).
        const QStringList cases = qEnvironmentVariable("ETER_MAPTEST").split(QLatin1Char('|'));
        for (int i = 0; i + 2 < cases.size(); i += 3) {
            QString problem;
            const QString root = RemoteEditorWindow::localFolder(cases[i + 1], &problem);
            std::printf("%s  [%s → %s]\n   = %s\n%s", qPrintable(cases[i + 2]), qPrintable(cases[i]), qPrintable(cases[i + 1]),
                        qPrintable(RemoteEditorWindow::mapWith(cases[i + 2], cases[i], root)),
                        problem.isEmpty() ? "" : qPrintable(QStringLiteral("   ! %1\n").arg(problem)));
        }
        return 0;
    }
    if (parser.isSet(editorOpt)) {
        // Szerkesztő mód: nincs saját adás és saját adatbázis; a playout adatainak tükre egy ideiglenes fájlban.
        QTemporaryDir tmp;
        Database mirror;
        QString err;
        if (!tmp.isValid() || !mirror.open(tmp.filePath(QStringLiteral("mirror.sqlite")), &err)) {
            QMessageBox::critical(nullptr, QStringLiteral("Éter"), TR("A szerkesztő nem indítható: %1").arg(err));
            return 1;
        }
        eter::Engine engine;                           // az előhallgatáshoz, ezen a gépen
        std::string aerr;
        if (engine.init(parser.value(backendOpt).toStdString(), &aerr)) engine.setRoute(eter::Engine::Pfl, {}, &aerr);
        RemoteEditorWindow editor(&mirror, &engine);
        editor.show();
        const QString testHost = qEnvironmentVariable("ETER_EDITOR_HOST");
        if (!testHost.isEmpty()) {
            RemoteTarget t;
            t.host = testHost;
            t.port = quint16(qEnvironmentVariableIntValue("ETER_EDITOR_PORT"));
            t.password = qEnvironmentVariable("ETER_EDITOR_PASS");
            t.pathFrom = qEnvironmentVariable("ETER_EDITOR_FROM");
            t.pathTo = qEnvironmentVariable("ETER_EDITOR_TO");
            editor.connectTo(t);
            if (qEnvironmentVariableIsSet("ETER_EDITOR_TEST")) runEditorTest(editor);
            if (qEnvironmentVariableIsSet("ETER_EDITOR_WATCH")) {
                // Figyelő szerkesztő: kiírja a kapcsolat és a playlist változásait, majd a megadott idő után kilép.
                QObject::connect(editor.client(), &remote::Client::loggedIn, &editor, [](const QString &) { std::printf("figyelő: belépve\n"); std::fflush(stdout); });
                QObject::connect(editor.client(), &remote::Client::lost, &editor, [](const QString &r) { std::printf("figyelő: megszakadt (%s)\n", qPrintable(r)); std::fflush(stdout); });
                auto last = std::make_shared<QString>();
                QObject::connect(&editor, &RemoteEditorWindow::stateApplied, &editor, [&editor, last] {
                    const QString st = editor.debugState();
                    if (st != *last) std::printf("figyelő: %s\n", qPrintable(st));
                    *last = st;
                    std::fflush(stdout);
                });
                QTimer::singleShot(qEnvironmentVariableIntValue("ETER_EDITOR_WATCH") * 1000, [] { QApplication::exit(0); });
            }
        } else if (qEnvironmentVariableIsSet("ETER_EDITOR_SHOT")) {
            // A csatlakozási ablak képe (fejlesztéshez).
            auto *dlg = new RemoteConnectDialog(&editor);
            dlg->show();
            QTimer::singleShot(800, &editor, [dlg] {
                dlg->grab().save(qEnvironmentVariable("ETER_EDITOR_SHOT"));
                QApplication::exit(0);
            });
        } else {
            QTimer::singleShot(0, &editor, [&editor] {
                RemoteConnectDialog dlg(&editor);
                if (dlg.exec() == QDialog::Accepted) editor.connectTo(dlg.target());
            });
        }
        const int rc = QCoreApplication::exec();
        engine.shutdown();
        return rc;
    }
    Database db;
    QString dbErr;
    const QString dbPath = parser.isSet(dbOpt)
        ? parser.value(dbOpt)
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/eter/library.sqlite";
    // Egy adatbázissal egyszerre csak egy példány futhat: két példány ugyanazt a playlistet játszaná
    // egymáshoz képest eltolva (visszhangszerűen), és egymás mentéseit írná felül.
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    QLockFile instanceLock(dbPath + QStringLiteral(".lock"));
    instanceLock.setStaleLockTime(0);                 // csak egy ténylegesen futó folyamat zárol (összeomlás után nem ragad be)
    if (!instanceLock.tryLock(200)) {
        qint64 pid = 0;
        QString host, app;
        instanceLock.getLockInfo(&pid, &host, &app);
        std::fprintf(stderr, "Az Éter már fut ezzel az adatbázissal (folyamat: %lld)\n", static_cast<long long>(pid));
        QMessageBox::warning(nullptr, QStringLiteral("Éter"),
                             TR("Az Éter már fut ezzel az adatbázissal (folyamat: %1).\n\nKét példány ugyanazt a playlistet "
                                "játszaná egyszerre, ezért ez a példány nem indul el.").arg(pid));
        return 1;
    }
    if (!db.open(dbPath, &dbErr)) {
        QMessageBox::critical(nullptr, QStringLiteral("Éter"),
                              TR("Az adatbázis nem nyitható meg:\n%1\n\n%2").arg(dbPath, dbErr));
        return 1;
    }
    MainWindow window(&db, parser.value(backendOpt));
    window.show();
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty() && !parser.isSet(uiTestOpt) && !parser.isSet(schedTestOpt) && !parser.isSet(mixTestOpt) && !parser.isSet(recTestOpt) && !parser.isSet(plTestOpt) && !parser.isSet(cartTestOpt) && !parser.isSet(editTestOpt) && !parser.isSet(eventTestOpt) && !parser.isSet(lookTestOpt) && !parser.isSet(nextTestOpt) && !parser.isSet(overlayTestOpt) && !parser.isSet(serveTestOpt)) window.library()->importPaths(files);

    if (parser.isSet(importTestOpt)) {
        // Az importáló ablak teljes útja: mappa, kategóriaválasztás, beolvasás.
        if (db.categories().isEmpty()) db.addCategory(QStringLiteral("Importált"), "music", "#45d483");
        window.library()->importPaths({parser.value(importTestOpt)}, [&db](const QVector<int> &ids) {
            std::printf("visszaadott elemek: %d\n", int(ids.size()));
            for (const Item &it : db.items())
                std::printf("  %-28s | %-14s | %-10s | %lld ms\n", qPrintable(it.title), qPrintable(it.artist),
                            qPrintable(it.categoryName), static_cast<long long>(it.durationMs));
            QTimer::singleShot(200, [] { QApplication::exit(0); });
        });
        QTimer::singleShot(500, &window, [] {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *dlg = qobject_cast<ImportDialog *>(w);
                if (!dlg) continue;
                QPushButton *go = nullptr;
                for (QPushButton *b : dlg->findChildren<QPushButton *>())
                    if (b->text() == QStringLiteral("Importálás")) go = b;
                std::printf("ablak: %s | gomb kategória nélkül: %s\n", qPrintable(dlg->findChild<QLabel *>()->text()),
                            go && go->isEnabled() ? "engedélyezve (HIBA)" : "tiltva");
                auto *combo = dlg->findChild<QComboBox *>();
                combo->setCurrentIndex(1);
                if (go) go->click();
            }
        });
    }
    if (parser.isSet(schedTestOpt)) {
        // Ütemező forgatókönyv: zene / jingle / hírek kategória, séma fix :00 hírekkel, generálás, automata adás.
        auto ensureCat = [&db](const QString &name, const QString &kind, const QString &color) {
            for (const Category &c : db.categories())
                if (c.name == name) return c.id;
            return db.addCategory(name, kind, color);
        };
        const int music = ensureCat(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = ensureCat(QStringLiteral("Jingle"), "jingle", "#b99cff");
        const int news = ensureCat(QStringLiteral("Hírek"), "news", "#45d483");
        int newsItem = 0;
        for (const QString &f : files) {
            if (db.itemIdByPath(f)) continue;
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            const QString base = QFileInfo(f).completeBaseName();
            Item it;
            it.categoryId = base.startsWith("hirek") ? news : base.startsWith("jingle") ? jingle : music;
            it.path = f;
            it.title = QString::fromStdString(dec.title());
            if (it.title.isEmpty()) it.title = base;
            it.artist = QString::fromStdString(dec.artist());
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            const int id = db.addItem(it);
            if (it.categoryId == news) newsItem = id;
        }
        db.notifyItemsChanged();
        if (db.clocks().isEmpty()) {
            const int clock = db.addClock(QStringLiteral("Teszt óra"), "#f5a524");
            ClockSlot s;
            s.clockId = clock;
            s.type = "item"; s.itemId = newsItem; s.fixedOffsetS = 0;
            db.addSlot(s);
            s = ClockSlot(); s.clockId = clock; s.type = "category"; s.categoryId = jingle;
            db.addSlot(s);
            s = ClockSlot(); s.clockId = clock; s.type = "fill"; s.categoryId = music;
            db.addSlot(s);
            for (int d = 0; d < 7; ++d)
                for (int h = 0; h < 24; ++h) db.setGrid(d, h, clock);
        }
        auto report = [&window](const char *when) {
            const QStringList parts = window.debugState().split("  ");
            QStringList shown;
            for (const QString &p : parts)
                if (!p.endsWith("=pending")) shown << p;
            std::printf("%s  %-22s %s  (+%d várakozó)\n", qPrintable(eter::now().toString("HH:mm:ss")), when,
                        qPrintable(shown.join("  ")), int(parts.size() - shown.size()));
        };
        window.schedulerPage()->generateNow();
        if (qEnvironmentVariableIsSet("ETER_TEST_UNDERRUN")) {
            // A folyó órából csak az első elem marad: a műsor a fix időpont előtt elfogy.
            QSqlQuery q;
            q.exec("DELETE FROM playlist_entries WHERE position > 1 AND playlist_id = "
                   "(SELECT id FROM playlists ORDER BY air_date, hour LIMIT 1)");
            emit window.schedulerPage()->generated({});
        }
        if (qEnvironmentVariableIsSet("ETER_TEST_WAIT")) db.setSetting("fixed_time_policy", "wait");
        report("generálás után");
        window.setMode(MainWindow::Auto);
        QTimer::singleShot(1000, &window, [=, &window] { window.next(); });
        for (int ms : {2000, 20000, 29000, 31500, 36000, 40000, 50000})
            QTimer::singleShot(ms, &window, [=] { report(""); });
        QTimer::singleShot(51000, &window, [=, &db] {
            for (const LogRow &l : db.log(8))
                std::printf("  napló: %s  %-14s %2lld mp  %s\n", qPrintable(QDateTime::fromSecsSinceEpoch(l.startedAt).addMSecs(eter::now().msecsTo(QDateTime::currentDateTime()) * -1).toString("HH:mm:ss")),
                            qPrintable(l.title), static_cast<long long>(l.endedAt ? l.endedAt - l.startedAt : -1), qPrintable(l.endReason));
            QApplication::exit(0);
        });
    }
    if (parser.isSet(mixTestOpt)) {
        // Mix forgatókönyv: egyedi átmenet, számhoz mentett alapértelmezés, előhallgatás, majd adás.
        const int cat = db.categories().isEmpty() ? db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff")
                                                  : db.categories().first().id;
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = cat;
            it.path = f;
            it.title = QString::fromStdString(dec.title());
            if (it.title.isEmpty()) it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        const QVector<Entry> entries = db.pendingEntries();
        if (entries.size() >= 3) {
            Transition t;
            t.fromEntryId = entries[0].id;
            t.toEntryId = entries[1].id;
            t.mixOutMs = 3000; t.fadeOutMs = 1500; t.fadeOutCurve = "scurve";
            t.cueInMs = 2000; t.fadeInMs = 1000; t.startLevelDb = -12;
            db.saveTransition(t);
            if (qEnvironmentVariableIsSet("ETER_TEST_VT") && ids.size() >= 4) {
                // Voice track az első átmenetre (a negyedik fájl), és intro a második számra.
                t.vtItemId = ids[3];
                t.vtOffsetMs = qEnvironmentVariable("ETER_TEST_VT").toInt();
                t.vtDuckDb = -12;
                db.saveTransition(t);
                db.setItemIntro(ids[1], 4000);
                QSqlQuery q;
                q.exec(QStringLiteral("DELETE FROM playlist_entries WHERE item_id = %1").arg(ids[3]));
                emit window.schedulerPage()->generated({});
            }
            if (qEnvironmentVariableIsSet("ETER_TEST_END")) {
                // A kimenő elem a mix pont után még tovább szól: vége a megadott ponton.
                t.outEndMs = qEnvironmentVariable("ETER_TEST_END").toInt();
                db.saveTransition(t);
            }
            db.setItemMixDefaults(ids[1], 5000, 1000, "linear", 0);
            db.setItemCueIn(ids[2], 1000);
            emit window.mixPage()->mixSaved({ids[0], ids[1], ids[2]});
        }
        auto &pfl1 = window.engine()->source(eter::Engine::Pfl);
        auto &pfl2 = window.engine()->source(eter::Engine::Pfl2);
        auto &pfl4 = window.engine()->source(eter::Engine::Pfl4);
        auto &pfl3 = window.engine()->source(eter::Engine::Pfl3);
        auto &vtAir = window.engine()->source(eter::Engine::Vt);
        auto report = [&window, &pfl1, &pfl2, &pfl3, &pfl4, &vtAir](const char *when) {
            std::printf("%-24s %s | előh.: %s@%.1f + %s@%.1f + %s@%.1f + VT %s@%.1f | adás VT: %s@%.1f\n", when, qPrintable(window.debugState()),
                        pfl1.playing() ? "szól" : "áll", pfl1.position() / 48000.0, pfl2.playing() ? "szól" : "áll", pfl2.position() / 48000.0,
                        pfl4.playing() ? "szól" : "áll", pfl4.position() / 48000.0,
                        pfl3.playing() ? "szól" : "áll", pfl3.position() / 48000.0, vtAir.playing() ? "szól" : "áll", vtAir.position() / 48000.0);
        };
        auto clickPreview = [&window] {
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("Előhallgatás") || b->text() == QStringLiteral("Leállítás")) { b->click(); return; }
        };
        for (QToolButton *b : window.findChildren<QToolButton *>())
            if (b->text() == QStringLiteral("Mix editor")) b->click();
        window.mixPage()->selectPair(0);          // a fent közvetlenül az adatbázisba írt értékek betöltése
        report("betöltés után");
        if (qEnvironmentVariableIsSet("ETER_TEST_TRIPLE")) {
            // Hármas nézet: az elemek szélének húzása egérrel, majd mentés.
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("3 elem") && !b->isChecked()) b->click();
            QApplication::processEvents();
            MixTimeline *tl = window.mixPage()->timeline();
            auto show = [tl](const char *when) {
                std::printf("%-34s", when);
                for (int j = 0; j < tl->transitions(); ++j) {
                    const MixParams &p = tl->params(j);
                    std::printf(" | %d→%d: mix %.1f, kiúsztatás %.1f, kezdőpont %.1f", j + 1, j + 2, p.mixOutMs / 1000.0, p.fadeOutMs / 1000.0, p.cueInMs / 1000.0);
                }
                std::printf(" | 1. elem vége %.1f, 2. elem vége %.1f | 2. elem adásban: %.1f mp\n", tl->outEndTrack(0) / 1000.0,
                            tl->outEndTrack(1) / 1000.0, (tl->clipEnd(1) - tl->clipStart(1)) / 1000.0);
            };
            auto drag = [tl](MixTimeline::Handle h, int j, int dx) {
                const QPointF from = tl->handlePos(h, j), to = from + QPointF(dx, 0);
                QMouseEvent press(QEvent::MouseButtonPress, from, tl->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(tl, &press);
                QMouseEvent move(QEvent::MouseMove, to, tl->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(tl, &move);
                QMouseEvent release(QEvent::MouseButtonRelease, to, tl->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(tl, &release);
            };
            std::printf("idővonal: %d átmenet, szélesség %d px, ablak %lld mp\n", tl->transitions(), tl->width(), static_cast<long long>(tl->windowMs() / 1000));
            if (auto *bar = window.mixPage()->findChild<QScrollBar *>("mixScroll")) {
                const long long before = tl->viewStart();
                std::printf("görgetősáv: %.1f … %.1f mp, lap %.1f mp, most %.1f mp\n", bar->minimum() / 100.0, bar->maximum() / 100.0,
                            bar->pageStep() / 100.0, bar->value() / 100.0);
                bar->setValue(bar->maximum());
                std::printf("  a végére húzva: a nézet bal széle %.1f mp (előtte %.1f)\n", tl->viewStart() / 1000.0, before / 1000.0);
                bar->setValue(bar->minimum());
                std::printf("  az elejére húzva: %.1f mp\n", tl->viewStart() / 1000.0);
                tl->recenter();
                std::printf("  középre igazítva: sáv %.1f mp, nézet %.1f mp\n", bar->value() / 100.0, tl->viewStart() / 1000.0);
            }
            {
                // Nagyítás a görgővel: a kurzor alatti időpont a helyén marad.
                const QPointF at(tl->width() * 0.8, tl->height() / 2.0);
                const long long before = tl->msAt(at.x());
                for (int dy : {120, 120, -120, -120}) {
                    QWheelEvent wheel(at, tl->mapToGlobal(at), QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    QApplication::sendEvent(tl, &wheel);
                    std::printf("görgő %+d: ablak %lld mp, a kurzor alatt %.2f mp (előtte %.2f)\n", dy, static_cast<long long>(tl->windowMs() / 1000),
                                tl->msAt(at.x()) / 1000.0, before / 1000.0);
                }
            }
            show("kiinduló állapot");
            const int px = std::max(1, int(tl->width() * 1000 / tl->windowMs()));        // 1 mp ennyi képpont
            drag(MixTimeline::TrimLeft, 0, px);
            show("2. elem bal széle +1 mp");
            drag(MixTimeline::TrimRight, 1, -px);
            show("2. elem jobb széle −1 mp");
            drag(MixTimeline::Block, 0, -px / 2);
            show("2. elem teste −0,5 mp");
            drag(MixTimeline::FadeOutEnd, 1, px / 2);
            show("2→3 kiúsztatás kezdete +0,5 mp");
            drag(MixTimeline::Block, 0, -2 * px);
            show("2. elem teste −2 mp (csúsztatás)");
            drag(MixTimeline::Block, 1, -2 * px);
            show("3. elem teste −2 mp (csúsztatás)");
            drag(MixTimeline::TrimRight, 0, px);
            show("1. elem jobb széle +1 mp");
            drag(MixTimeline::FadeOutEnd, 0, px / 2);
            show("1. elem kiúsztatás kezdete +0,5 mp");
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("Mentés")) b->click();
            QSqlQuery q("SELECT from_entry_id, to_entry_id, mix_out_ms, fade_out_ms, cue_in_ms, out_end_ms FROM transitions ORDER BY to_entry_id");
            while (q.next())
                std::printf("  mentve: %d→%d sor: mix %d ms, vége %d ms, kiúsztatás %d ms, kezdőpont %d ms\n", q.value(0).toInt(), q.value(1).toInt(),
                            q.value(2).toInt(), q.value(5).toInt(), q.value(3).toInt(), q.value(4).toInt());
        }
        QTimer::singleShot(300, &window, clickPreview);
        QTimer::singleShot(2300, &window, [=] { report("előhallgatás 2,0 mp"); });
        QTimer::singleShot(4300, &window, [=] { report("előhallgatás 4,0 mp"); });
        QTimer::singleShot(4500, &window, clickPreview);
        QTimer::singleShot(4800, &window, [=, &window] { report("előhallgatás leállítva"); window.setMode(MainWindow::Auto); window.next(); });
        for (int ms : {2000, 3500, 5200, 7000, 8500, 11000})
            QTimer::singleShot(4800 + ms, &window, [=] { report(qPrintable(QStringLiteral("adás +%1 mp").arg(ms / 1000.0, 0, 'f', 1))); });
        QTimer::singleShot(17500, &window, [=, &db] {
            for (const LogRow &l : db.log(5))
                std::printf("  napló: %-12s %2lld mp  %s\n", qPrintable(l.title), static_cast<long long>(l.endedAt ? l.endedAt - l.startedAt : -1), qPrintable(l.endReason));
            QApplication::exit(0);
        });
    }
    if (parser.isSet(plTestOpt)) {
        // Playlist szerkesztő: összeállítás, mentés playlist kategóriába, beillesztés blokként, kinyitás, áthúzás, adás.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Szignálok"), "jingle", "#f2c94c");
        const int plCat = db.addCategory(QStringLiteral("Reggeli blokkok"), "playlist", "#7c5cff");
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = QFileInfo(f).suffix() == "wav" ? jingle : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QString::fromStdString(dec.title());
            if (it.title.isEmpty()) it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        PlaylistTable *table = window.playlistTable();
        auto show = [table](const char *when) {
            std::printf("%s\n", when);
            for (int r = 0; r < table->rowCount(); ++r)
                std::printf("    %-9s | %-40s | %-8s | %-14s | %5s | %s\n", qPrintable(table->item(r, 0)->text()), qPrintable(table->item(r, 1)->text()),
                            qPrintable(table->item(r, 2)->text()), qPrintable(table->item(r, 3)->text()), qPrintable(table->item(r, 4)->text()),
                            qPrintable(table->item(r, 5)->text()));
        };
        std::printf("kategóriák a hangtár szűrőjében: %d, playlist kategória: %d (id %d)\n", int(db.itemCategories().size()),
                    int(db.playlistCategories().size()), plCat);
        window.appendItems({ids[0]});                              // egy sima sor a blokk elé
        for (QToolButton *b : window.findChildren<QToolButton *>())
            if (b->text() == QStringLiteral("Playlist szerkesztő")) b->click();
        PlaylistEditorPage *ed = window.playlistPage();
        ed->newPlaylist();
        ed->addItems({ids[1], ids[2], ids[3]});
        ed->addItems({ids[4]}, 1);                                 // beszúrás a második helyre
        std::printf("mentés név nélkül: %d\n", ed->save());
        ed->setName(QStringLiteral("Reggeli nyitó"));
        const bool saved = ed->save();
        std::printf("mentés: %d, azonosító %d, %d elem, módosítva: %d\n", saved, ed->currentId(), ed->itemCount(), ed->dirty());
        for (const SavedPlaylist &p : db.savedPlaylists())
            std::printf("  mentve: %s · %s · %d elem · %lld ms\n", qPrintable(p.name), qPrintable(p.categoryName), p.count, static_cast<long long>(p.durationMs));
        for (const Category &c : db.categories())
            if (c.kind == "playlist") std::printf("  kategória: %s, benne %d playlist\n", qPrintable(c.name), c.itemCount);
        for (QPushButton *b : ed->findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Beillesztés a fő playlistbe")) b->click();
        window.appendItems({ids[5]});                              // egy sima sor a blokk után
        if (!qEnvironmentVariableIsSet("ETER_PL_STAY")) {
        for (QToolButton *b : window.findChildren<QToolButton *>())
            if (b->text() == QStringLiteral("Adás")) b->click();
        show("beillesztés után (becsukva):");
        int group = 0;
        {
            QSqlQuery q("SELECT DISTINCT group_id, group_name FROM playlist_entries WHERE group_id IS NOT NULL");
            while (q.next()) { group = q.value(0).toInt(); std::printf("  adatbázis: blokk %d „%s”\n", group, qPrintable(q.value(1).toString())); }
        }
        window.toggleGroup(group);
        show("kinyitva:");
        emit table->rowMoved(7, 4);                                // az utolsó sima sor a blokk két eleme közé
        show("sima sor a blokk belsejébe húzva (a blokk tagja lesz):");
        emit table->rowMoved(4, 2);                                // a blokk fejléce elé
        show("ugyanaz a sor a blokk fejléce elé húzva (kikerül a blokkból):");
        emit table->rowMoved(2, table->rowCount());                // vissza a lista végére
        show("és vissza a lista végére (nem lesz a blokk tagja):");
        window.toggleGroup(group);
        emit table->rowMoved(2, table->rowCount());                // a becsukott blokk a fejlécénél fogva a végére
        show("a becsukott blokk a végére húzva:");
        emit table->rowMoved(3, 2);                                // és vissza a közepére
        show("a blokk visszahúzva a második helyre:");
        if (qEnvironmentVariableIsSet("ETER_PL_OPEN")) window.toggleGroup(group);
        window.setMode(MainWindow::Auto);
        QTimer::singleShot(500, &window, [&window] { window.next(); });
        auto report = [&window, show](const char *when) { std::printf("%-12s %s\n", when, qPrintable(window.debugState())); show(""); };
        QTimer::singleShot(2500, &window, [=] { report("adás +2 mp"); });
        QTimer::singleShot(16000, &window, [=] { report("adás +15,5 mp"); });
        QTimer::singleShot(16500, &window, [table] {
            // A fejléc törlése: a blokk még várakozó elemei eltűnnek, az adásban lévő marad.
            for (int r = 0; r < table->rowCount(); ++r)
                if (table->item(r, 3)->text() == QStringLiteral("Playlist blokk")) { table->setCurrentCell(r, 0); break; }
            emit table->deletePressed();
        });
        QTimer::singleShot(17000, &window, [=] { report("blokk törölve"); QApplication::exit(0); });
        }
    }
    if (parser.isSet(cartTestOpt)) {
        // Cartwall: gombok feltöltése a hangtárból és fájlból, második fal más színnel, indítás és leállítás.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Szignálok"), "jingle", "#f2c94c");
        QVector<int> ids;
        QStringList loose;                                         // ezek nem kerülnek a hangtárba: tallózott fájlok
        for (const QString &f : files) {
            if (QFileInfo(f).suffix() == "wav") { loose << QFileInfo(f).absoluteFilePath(); continue; }
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QString::fromStdString(dec.title());
            if (it.title.isEmpty()) it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        Q_UNUSED(jingle);
        db.notifyItemsChanged();
        window.appendItems({ids[0]});
        CartwallPanel *cw = window.cartwall();
        auto report = [&window, cw](const char *when) {
            float l = 0, r = 0;
            for (int c = eter::Engine::Cart1; c <= eter::Engine::CartLast; ++c) {
                auto &s = window.engine()->source(static_cast<eter::Engine::Channel>(c));
                if (s.playing()) { l += 1; r = std::max<float>(r, s.position() / 48000.0f); }
            }
            std::printf("%-26s %s | szóló hangok: %d (leghosszabb %.1f mp) | %s\n", when, qPrintable(cw->debugState()), int(l), r,
                        qPrintable(window.debugState()));
        };
        {
            // A cartwall kimenete: alapból az 1-es lejátszót követi, de külön eszközre is állítható.
            auto routes = [&window](const char *when) {
                auto *e = window.engine();
                std::printf("%-34s beállítás „%s” | lejátszó 1: „%s” | cart 1: „%s” | cart 8: „%s”\n", when, qPrintable(window.routeOf(MainWindow::kCartRoute)),
                            e->route(eter::Engine::Player1).c_str(), e->route(eter::Engine::Cart1).c_str(), e->route(eter::Engine::CartLast).c_str());
            };
            const QStringList devs = window.deviceNames();
            std::printf("kimenetek: %s\n", qPrintable(devs.join(" | ")));
            routes("induláskor");
            if (devs.size() >= 2) {
                window.changeRoute(0, devs[0]);
                routes("lejátszó 1 az első eszközre");
                window.changeRoute(MainWindow::kCartRoute, devs[1]);
                routes("cartwall a második eszközre");
                window.changeRoute(0, QString());
                routes("lejátszó 1 vissza (cart marad)");
                std::printf("nem létező eszköz: %d\n", window.changeRoute(MainWindow::kCartRoute, QStringLiteral("nincs ilyen")));
                routes("hiba után");
                if (!qEnvironmentVariableIsSet("ETER_CART_KEEP")) {
                    window.changeRoute(MainWindow::kCartRoute, QStringLiteral("@player1"));
                    routes("cartwall újra követi");
                }
            }
        }
        window.showLeft(1);
        std::printf("bal oldal: %s\n", window.playlistTable()->isVisible() ? "playlist" : cw->isVisible() ? "cartwall" : "?");
        cw->assignPaths(0, loose);                                  // tallózott fájlok az 1. helytől
        cw->assignItems(4, {ids[1], ids[2]});                       // hangtárelemek az 5. helytől
        report("első fal feltöltve");
        const int second = cw->addWall(QStringLiteral("Műsor"), QStringLiteral("#45d483"));
        cw->assignItems(0, {ids[0]});
        cw->assignPaths(1, {loose.first(), files.last() + ".nincs"});
        report("második fal");
        cw->setWallColor(second, QStringLiteral("#b99cff"));
        const int first = db.cartwalls().first().id;
        cw->selectWall(first);
        report("vissza az elsőre");
        {
            QSqlQuery q("SELECT wall_id, slot, item_id, file_path, label, duration_ms FROM carts ORDER BY wall_id, slot");
            while (q.next())
                std::printf("  adatbázis: fal %d, hely %d, elem %s, fájl %s, felirat „%s”, %d ms\n", q.value(0).toInt(), q.value(1).toInt() + 1,
                            q.value(2).isNull() ? "–" : qPrintable(q.value(2).toString()),
                            q.value(3).isNull() ? "–" : qPrintable(QFileInfo(q.value(3).toString()).fileName()), qPrintable(q.value(4).toString()),
                            q.value(5).toInt());
        }
        window.setMode(MainWindow::Assist);
        QTimer::singleShot(500, &window, [&window] { window.next(); });             // közben a playlist is szól
        QTimer::singleShot(1000, &window, [cw] { cw->trigger(4); cw->trigger(0); }); // két cart egyszerre
        QTimer::singleShot(2000, &window, [=] { report("két cart szól"); });
        QTimer::singleShot(2200, &window, [cw] { cw->trigger(4); });                 // újabb kattintás: leáll
        QTimer::singleShot(2700, &window, [=] { report("az 5. leállítva"); });
        QTimer::singleShot(2800, &window, [cw, second] { cw->selectWall(second); cw->trigger(0); });
        QTimer::singleShot(3800, &window, [=] { report("másik falon is szól"); });
        QTimer::singleShot(4000, &window, [cw] { cw->stopAll(); });
        QTimer::singleShot(4600, &window, [=] { report("mind leállítva"); });
        QTimer::singleShot(4700, &window, [cw, first] { cw->selectWall(first); cw->trigger(1); });   // rövid hang: magától ér véget
        QTimer::singleShot(4700 + 9000, &window, [=] { report("rövid cart lejárt"); QApplication::exit(0); });
    }
    if (parser.isSet(editTestOpt)) {
        // Elem szerkesztője: adatok és jelölőpontok mentése, húzás egérrel, előhallgatás, hatás a playlistre.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int other = db.addCategory(QStringLiteral("Retró"), "music", "#45d483");
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QString::fromStdString(dec.title());
            if (it.title.isEmpty()) it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        auto dump = [&db](const char *when, int id) {
            QSqlQuery q(QStringLiteral("SELECT title, artist, album, year, category_id, cue_in_ms, intro_ms, mix_out_ms, auto_mix_out_ms, duration_ms FROM items WHERE id = %1").arg(id));
            if (q.next())
                std::printf("%-28s cím „%s”, előadó „%s”, album „%s”, év %s, kategória %d | kezdőpont %d, intro %s, outro %s (auto %s), hossz %d\n", when,
                            qPrintable(q.value(0).toString()), qPrintable(q.value(1).toString()), qPrintable(q.value(2).toString()),
                            q.value(3).isNull() ? "–" : qPrintable(q.value(3).toString()), q.value(4).toInt(), q.value(5).toInt(),
                            q.value(6).isNull() ? "–" : qPrintable(q.value(6).toString()), q.value(7).isNull() ? "–" : qPrintable(q.value(7).toString()),
                            q.value(8).isNull() ? "–" : qPrintable(q.value(8).toString()), q.value(9).toInt());
        };
        const int id = ids[1];
        dump("megnyitás előtt", id);
        ItemEditorDialog *ed = window.openItemEditor(id);
        QTimer::singleShot(1500, &window, [=, &window, &db] {
            std::printf("hullámforma kész: %d\n", ed->waveReady());
            ed->setTitleText(QStringLiteral("Hajnali vonat (rádió változat)"));
            for (QLineEdit *e : ed->findChildren<QLineEdit *>())
                if (e->text().isEmpty()) { e->setText(QStringLiteral("Vasúti dalok")); break; }
            for (QSpinBox *s : ed->findChildren<QSpinBox *>()) s->setValue(1998);
            for (QComboBox *c : ed->findChildren<QComboBox *>()) c->setCurrentIndex(c->findData(other));
            ed->setMarkerSeconds(MarkerWave::CueIn, 1.5);
            ed->setMarkerSeconds(MarkerWave::Intro, 1.0);
            std::printf("mentés hibás introval: %d\n", ed->save());
            ed->setMarkerSeconds(MarkerWave::Intro, 6.0);
            // Az outro jelölő húzása egérrel a hullámformán: a hossz 75%-ára.
            MarkerWave *w = ed->wave();
            const QPointF from = w->markerPos(MarkerWave::Outro) + QPointF(-20, 0), to(w->width() * 0.75, from.y());
            QMouseEvent press(QEvent::MouseButtonPress, from, w->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &press);
            QMouseEvent move(QEvent::MouseMove, to, w->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, to, w->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(w, &release);
            std::printf("outro húzás után: %.1f mp\n", w->marker(MarkerWave::Outro) / 1000.0);
            std::printf("mentés: %d\n", ed->save());
            dump("mentés után", id);
            std::printf("playlist: %s\n", qPrintable(window.debugState()));
            ed->playFrom(5000);
        });
        QTimer::singleShot(2700, &window, [=, &window] {
            auto &pfl = window.engine()->source(eter::Engine::Pfl);
            std::printf("előhallgatás 5 mp-től: %s @ %.1f mp, a szerkesztő szerint szól: %d\n", pfl.playing() ? "szól" : "áll", pfl.position() / 48000.0, ed->previewing());
            for (QPushButton *b : ed->findChildren<QPushButton *>())
                if (b->text().startsWith(QStringLiteral("Ide:"))) { b->click(); break; }   // kezdőpont a lejátszási pozícióra
            std::printf("kezdőpont a lejátszási pozícióra: %.1f mp\n", ed->wave()->marker(MarkerWave::CueIn) / 1000.0);
            ed->setMarkerSeconds(MarkerWave::CueIn, 1.5);
            ed->setMarkerSeconds(MarkerWave::Outro, 0);                                    // vissza automatikusra
            std::printf("mentés (outro automatikus): %d\n", ed->save());
        });
        QTimer::singleShot(3200, &window, [=, &window] {
            dump("outro automatikus", id);
            if (qEnvironmentVariableIsSet("ETER_EDIT_STAY")) return;
            if (qEnvironmentVariableIsSet("ETER_EDIT_QUIT")) {
                // Kilépés nyitott, előhallgató szerkesztővel: a bontás nem nyúlhat a leállított hangmotorhoz.
                std::printf("kilépés nyitott szerkesztővel\n");
                QApplication::exit(0);
                return;
            }
            ed->close();
            QTimer::singleShot(300, &window, [&window] {
                auto &pfl = window.engine()->source(eter::Engine::Pfl);
                std::printf("bezárás után az előhallgatás: %s\n", pfl.playing() ? "szól" : "áll");
                QApplication::exit(0);
            });
        });
    }
    if (parser.isSet(overlayTestOpt)) {
        // Ráfektetés: A → C átmenet, közöttük B a saját csatornáján; mindhárom egyszerre szólhat.
        // ETER_OVL_PHASE: auto (alap), assist, dissolve, early (kézi váltás a tervezett mix pont előtt).
        const QString phase = qEnvironmentVariableIsSet("ETER_OVL_PHASE") ? qEnvironmentVariable("ETER_OVL_PHASE") : QStringLiteral("auto");
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jcat = db.addCategory(QStringLiteral("Szignál"), "jingle", "#f2c94c");
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = ids.size() == 1 ? jcat : cat;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);                       // A, B, C, D …
        const QVector<Entry> en = db.pendingEntries();
        if (en.size() < 4) { std::printf("kevés fájl\n"); return 1; }
        const int A = en[0].id, B = en[1].id, C = en[2].id;
        const bool drag = phase == "drag" || phase == "dragdissolve";
        if (!drag) {
            Transition t;
            t.fromEntryId = A;
            t.toEntryId = C;
            t.mixOutMs = 6000;
            t.outEndMs = 8000;
            t.fadeOutMs = 2000;
            t.fadeOutCurve = "scurve";
            db.saveTransition(t);
            emit window.mixPage()->mixSaved({});
            const qint64 offset = phase == "assist" ? 1500 : -1000;
            std::printf("ráfektetés: %d\n", window.setRowOverlay(B, true, offset, -6));
        } else {
            // A Mix editor hármas nézete: a harmadik elem (C) húzása a második (B) eleje elé, valódi egéreseményekkel.
            Transition ab;
            ab.fromEntryId = A;
            ab.toEntryId = B;
            ab.mixOutMs = 6000;
            ab.outEndMs = 9000;
            ab.fadeOutMs = 2000;
            ab.fadeOutCurve = "scurve";
            db.saveTransition(ab);
            emit window.mixPage()->mixSaved({});
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == QStringLiteral("Mix editor")) b->click();
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("3 elem") && !b->isChecked()) b->click();
            window.mixPage()->selectEntries(A, B);
            // A hullámformák (és velük az automatikus mix pontok) betöltése
            for (QElapsedTimer w; !w.isValid() || w.elapsed() < 2500;) {
                if (!w.isValid()) w.start();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            MixTimeline *tl = window.mixPage()->timeline();
            const double pxPerMs = tl->width() / double(tl->windowMs());
            const QPointF from = tl->handlePos(MixTimeline::Block, 1);
            const double dx = (tl->clipStart(2) - tl->clipStart(1) + 1500) * pxPerMs;   // C 1,5 mp-cel B eleje elé
            const QPointF to = from - QPointF(dx, 0);
            std::printf("húzás előtt: B kezd %lld ms, C kezd %lld ms (ablak %lld ms, %d px)\n", static_cast<long long>(tl->clipStart(1)),
                        static_cast<long long>(tl->clipStart(2)), static_cast<long long>(tl->windowMs()), tl->width());
            QMouseEvent press(QEvent::MouseButtonPress, from, tl->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tl, &press);
            for (int k = 1; k <= 8; ++k) {
                const QPointF at = from + (to - from) * (k / 8.0);
                QMouseEvent move(QEvent::MouseMove, at, tl->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(tl, &move);
            }
            std::printf("húzás közben: ráfektetésre vár: %d\n", tl->overlayPending());
            tl->grab().save("ovl_drag.png");
            QMouseEvent release(QEvent::MouseButtonRelease, to, tl->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tl, &release);
            for (int k = 0; k < 5; ++k) QApplication::processEvents();
            QSqlQuery q;
            q.prepare("SELECT overlay_offset_ms, overlay_duck_db FROM playlist_entries WHERE id = ?");
            q.addBindValue(B);
            q.exec();
            q.next();
            std::printf("B a húzás után: eltolás %s ms\n", q.value(0).isNull() ? "nincs" : qPrintable(q.value(0).toString()));
            const Transition ac = db.transition(C);
            std::printf("új átmenet: %d → %d, mix pont %lld ms\n", ac.fromEntryId == A ? 1 : 0, ac.toEntryId == C ? 3 : 0,
                        static_cast<long long>(ac.mixOutMs));
            std::printf("szerkesztett pár: %d sáv, ráfektetett: %s\n", tl->transitions() + 1,
                        tl->hasOverlay() ? qPrintable(tl->params(0).ovlTitle) : "nincs");
            QApplication::processEvents();
            window.grab().save("ovl_editor.png");
            if (phase == "dragdissolve") {
                for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                    if (b->text() == QStringLiteral("Önálló elemként")) b->click();
                for (int k = 0; k < 5; ++k) QApplication::processEvents();
                q.exec();
                q.next();
                std::printf("feloldás után B eltolás: %s · átmenet A→C: %d · nézet: %d elem\n", q.value(0).isNull() ? "nincs" : "van",
                            db.transition(C).fromEntryId == A, tl->transitions() + 1);
                std::printf("  %s\n", qPrintable(window.debugState()));
                QApplication::exit(0);
                return 0;
            }
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == QStringLiteral("Adás")) b->click();
        }
        if (phase == "load") {
            // Kézi betöltés a 2-es lejátszóba: a ráfektetés megszűnik, az elem a lejátszóban marad.
            window.loadRowInto(B, 1);
            QSqlQuery q;
            q.prepare("SELECT overlay_offset_ms FROM playlist_entries WHERE id = ?");
            q.addBindValue(B);
            q.exec();
            q.next();
            std::printf("kézi betöltés után: ráfektetés %s · %s\n", q.value(0).isNull() ? "nincs" : "van", qPrintable(window.debugState()));
            return 0;
        }
        if (phase == "dissolve") {
            std::printf("feloldás: %d\n", window.setRowOverlay(B, false));
            QSqlQuery q("SELECT COUNT(*) FROM transitions");
            q.next();
            std::printf("átmenetek a feloldás után: %d\n", q.value(0).toInt());
        }
        std::printf("indulás előtt  %s\n", qPrintable(window.debugState()));
        window.setMode(phase == "assist" || phase == "early" ? MainWindow::Assist : MainWindow::Auto);
        auto *e = window.engine();
        struct Stat { int maxTogether = 0; double ovlStart = -1, cStart = -1, ovlEnd = -1; qint64 t0 = 0; } ;
        auto st = std::make_shared<Stat>();
        QTimer::singleShot(300, &window, [&window, st] {
            st->t0 = QDateTime::currentMSecsSinceEpoch();
            window.next();
        });
        if (phase == "assist") QTimer::singleShot(300 + 3000, &window, [&window] { std::printf("  kézi Következő\n"); window.next(); });
        if (phase == "early") {
            // Kézi váltás jóval a mix pont előtt: a mix pont előtt induló ráfektetett elem C-vel együtt indul.
            // Utána a már lejátszott elem nem fektethető rá újra.
            QTimer::singleShot(300 + 1500, &window, [&window] { window.next(); });
            QTimer::singleShot(300 + 6500, &window, [&window, B] { std::printf("  újra ráfektetés lejátszás után: %d\n", window.setRowOverlay(B, true, 0, 0)); });
        }
        QTimer::singleShot(150, &window, [&window] {
            window.expandStandbyTransition();         // A → C, a ráfektetett elemmel
            QTimer::singleShot(100, &window, [&window] { window.grab().save("ovl_pending.png"); });
        });
        QTimer::singleShot(300 + 7000, &window, [&window] { window.grab().save("ovl_live.png"); });
        auto *sampler = new QTimer(&window);
        QObject::connect(sampler, &QTimer::timeout, &window, [e, st, &window] {
            if (!st->t0) return;
            const double t = (QDateTime::currentMSecsSinceEpoch() - st->t0) / 1000.0;
            const bool p1 = e->source(eter::Engine::Player1).playing(), p2 = e->source(eter::Engine::Player2).playing();
            const bool o1 = e->source(eter::Engine::Ovl1).playing(), o2 = e->source(eter::Engine::Ovl2).playing();
            const int n = int(p1) + int(p2) + int(o1) + int(o2);
            st->maxTogether = std::max(st->maxTogether, n);
            if ((o1 || o2) && st->ovlStart < 0) st->ovlStart = t;
            if (st->ovlStart >= 0 && !o1 && !o2 && st->ovlEnd < 0) st->ovlEnd = t;
            if (p2 && st->cStart < 0) st->cStart = t;
            static int k = 0;
            if (++k % 10 == 0)
                std::printf("  %5.2f s  P1 %d  P2 %d  RÁF %d%d  (%d szól)  %s\n", t, p1, p2, o1, o2, n, qPrintable(window.debugState()));
        });
        sampler->start(50);
        QTimer::singleShot(300 + 12500, &window, [st, &db] {
            std::printf("legtöbb egyszerre szóló: %d\n", st->maxTogether);
            std::printf("C indul: %.2f s · ráfektetett indul: %.2f s · véget ér: %.2f s\n", st->cStart, st->ovlStart, st->ovlEnd);
            QSqlQuery q("SELECT title, player, end_reason FROM play_log ORDER BY id");
            while (q.next()) std::printf("  napló: %s · lejátszó %d · %s\n", qPrintable(q.value(0).toString()), q.value(1).toInt(), qPrintable(q.value(2).toString()));
            QApplication::exit(0);
        });
    }
    if (parser.isSet(serveTestOpt)) {
        // Playout oldal: a, hirek, b, c, d a playlistben; távoli szerkesztés a „teszt” jelszóval; „a” szól.
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = cat;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        QSettings st;
        const QByteArray salt = remote::newSalt();
        st.setValue("remote/enabled", true);
        st.setValue("remote/port", qEnvironmentVariableIsSet("ETER_REMOTE_PORT") ? qEnvironmentVariableIntValue("ETER_REMOTE_PORT") : 7399);
        st.setValue("remote/salt", salt.toBase64());
        st.setValue("remote/key", remote::deriveKey(salt, QStringLiteral("teszt")).toBase64());
        window.configureRemote();
        std::printf("playout: %s\n", qPrintable(window.remoteStatus()));
        std::fflush(stdout);
        window.setMode(!qEnvironmentVariable("ETER_SERVE_AUTO").isEmpty() ? MainWindow::Auto : MainWindow::Assist);
        QTimer::singleShot(300, &window, [&window] { window.next(); });
        const int secs = qEnvironmentVariableIsSet("ETER_SERVE_SECS") ? qEnvironmentVariableIntValue("ETER_SERVE_SECS") : 20;
        QTimer::singleShot(secs * 1000, &window, [&window, &db] {
            std::printf("playout: végső állapot: %s\n", qPrintable(window.debugState()));
            QSqlQuery q("SELECT t.from_entry_id, t.to_entry_id, t.mix_out_ms FROM transitions t");
            while (q.next()) std::printf("playout: átmenet %d → %d, mix %d ms\n", q.value(0).toInt(), q.value(1).toInt(), q.value(2).toInt());
            QSqlQuery o("SELECT i.title, e.overlay_offset_ms FROM playlist_entries e JOIN items i ON i.id = e.item_id WHERE e.overlay_offset_ms IS NOT NULL");
            while (o.next()) std::printf("playout: ráfektetve: %s (%d ms)\n", qPrintable(o.value(0).toString()), o.value(1).toInt());
            Q_UNUSED(db);
            std::fflush(stdout);
            QApplication::exit(0);
        });
    }
    if (parser.isSet(nextTestOpt)) {
        // Asszisztált mód: a Szóköz (Következő) elindítja a készenlétben állót, és kiúsztatja, ami szól.
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = cat;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        window.setMode(MainWindow::Assist);
        auto report = [&window](const char *when) {
            auto &a = window.engine()->source(eter::Engine::Player1);
            auto &b = window.engine()->source(eter::Engine::Player2);
            std::printf("%-22s %s | P1 %s%s  P2 %s%s\n", when, qPrintable(window.debugState()), a.playing() ? "szól" : "áll", a.fading() ? " (úszik)" : "",
                        b.playing() ? "szól" : "áll", b.fading() ? " (úszik)" : "");
        };
        const bool key = qEnvironmentVariableIsSet("ETER_TEST_KEY");     // valódi billentyűleütéssel
        auto press = [&window, key] {
            if (!key) { window.next(); return; }
            QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : &window;
            std::printf("  szóköz ide: %s (%s)\n", target->metaObject()->className(), qPrintable(target->objectName()));
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
            QApplication::sendEvent(target, &down);
            QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
            QApplication::sendEvent(target, &up);
        };
        QTimer::singleShot(500, &window, press);
        QTimer::singleShot(1500, &window, [=] { report("első indítás után"); });
        QTimer::singleShot(3000, &window, press);
        QTimer::singleShot(3500, &window, [=] { report("második után 0,5 mp"); });
        QTimer::singleShot(6500, &window, [=] { report("második után 3,5 mp"); });
        QTimer::singleShot(7000, &window, press);
        QTimer::singleShot(10500, &window, [=] { report("harmadik után 3,5 mp"); QApplication::exit(0); });
    }
    if (parser.isSet(eventTestOpt)) {
        // Időzítések és a playlist követése: események a következő másodpercekre, kézi görgetés.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int plCat = db.addCategory(QStringLiteral("Blokkok"), "playlist", "#7c5cff");
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        const int pl = db.saveSavedPlaylist(0, plCat, QStringLiteral("Hírblokk"), {ids[1], ids[2]});
        // Hosszú lista, hogy legyen mit görgetni: 30 sor.
        QVector<int> many;
        for (int i = 0; i < 30; ++i) many << ids[i % ids.size()];
        window.appendItems(many);
        const QDateTime now = eter::now();
        auto ev = [&db, now](int item, int playlist, const char *action, int inSec, bool weekly, bool enabled, int dayShift = 0) {
            TimedEvent e;
            e.itemId = item;
            e.playlistId = playlist;
            e.action = action;
            const QDateTime at = now.addSecs(inSec);
            e.timeS = at.time().hour() * 3600 + at.time().minute() * 60 + at.time().second();
            if (weekly) e.weekdays = 1 << ((at.date().addDays(dayShift).dayOfWeek() - 1));
            else e.date = at.date().addDays(dayShift);
            e.enabled = enabled;
            return db.saveTimedEvent(e);
        };
        const int e1 = ev(ids[3], 0, "next", 3, false, true);        // egyszeri, következőként
        const int e2 = ev(0, pl, "end", 5, true, true);              // hetente ma, playlist a végére
        const int e3 = ev(ids[4], 0, "start", 8, false, true);       // egyszeri, azonnal indul
        const int e4 = ev(ids[0], 0, "next", 4, false, false);       // kikapcsolt: nem fut
        const int e5 = ev(ids[0], 0, "next", 4, true, true, 1);      // hetente, de holnap: nem fut
        const int e6 = ev(ids[0], 0, "next", -120, false, true);     // két perce lett volna: kimarad
        Q_UNUSED(e2); Q_UNUSED(e4); Q_UNUSED(e5);
        PlaylistTable *table = window.playlistTable();
        auto rowsText = [&window]() {
            QString s = window.debugState();
            QStringList parts = s.split("  ");
            QStringList out;
            for (const QString &p : parts) if (!p.contains("=played") && !p.startsWith("P")) out << p;
            return out.mid(0, 6).join("  ") + QStringLiteral("  … (%1 sor)").arg(parts.size() - 2);
        };
        auto report = [&window, table, rowsText](const char *when) {
            std::printf("%-30s követés: %d | görgetés %d/%d | %s\n", when, window.following(), table->verticalScrollBar()->value(),
                        table->verticalScrollBar()->maximum(), qPrintable(rowsText()));
        };
        window.setMode(MainWindow::Assist);
        QTimer::singleShot(500, &window, [&window] { window.next(); });
        QTimer::singleShot(1500, &window, [=] { report("indulás után"); });
        QTimer::singleShot(4200, &window, [=] { report("3 mp: következőként"); });
        QTimer::singleShot(6200, &window, [=] { report("5 mp: playlist a végére"); });
        QTimer::singleShot(9500, &window, [=] { report("8 mp: azonnal indul"); });
        QTimer::singleShot(10000, &window, [=, &window, &db] {
            for (const TimedEvent &e : db.timedEvents())
                std::printf("  esemény %d: aktív %d, lefutott %s | %s | %s | %s\n", e.id, e.enabled, e.lastFiredAt ? "igen" : "nem",
                            qPrintable(TimedEventsPage::daysText(e)), qPrintable(TimedEventsPage::actionText(e.action)), qPrintable(e.label));
            std::printf("  (e1=%d e3=%d e6=%d)\n", e1, e3, e6);
            // Kézi görgetés a lista aljára: a követés kikapcsol.
            table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());
            report("kézzel a lista aljára");
            window.next();
        });
        QTimer::singleShot(11500, &window, [=] { report("új elem indult (nem ugrik)"); });
        QTimer::singleShot(11600, &window, [=] {
            // Vissza az adásban lévő elemhez: a követés újra bekapcsol.
            table->verticalScrollBar()->setValue(0);
            report("kézzel vissza a tetejére");
        });
        QTimer::singleShot(11700, &window, [&window] { window.next(); });
        QTimer::singleShot(13000, &window, [=] { report("új elem indult (követ)"); QApplication::exit(0); });
    }
    if (parser.isSet(lookTestOpt)) {
        // Az új playlist-megjelenés: különféle elemek, blokk, következő óra, egyedi mix, lenyitott mini mix editor.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Szignálok"), "jingle", "#f2c94c");
        const int news = db.addCategory(QStringLiteral("Hírek"), "news", "#ff8f85");
        const int ads = db.addCategory(QStringLiteral("Reklám"), "ad", "#b99cff");
        const int plCat = db.addCategory(QStringLiteral("Blokkok"), "playlist", "#7c5cff");
        const char *titles[] = {"Üvegváros", "Hajnali vonat", "Lassú folyó", "Papírsárkány", "Csendes utca", "Esti fény"};
        const char *artists[] = {"Szél Anna", "Kormorán Híd", "Holdkomp", "Napfolt", "Rézkulcs", "Délibáb"};
        QVector<int> songs, others;
        int n = 0;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            const bool wav = QFileInfo(f).suffix() == "wav";
            it.categoryId = wav ? (others.size() == 0 ? jingle : others.size() == 1 ? news : ads) : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = wav ? (others.size() == 0 ? QStringLiteral("Állomásazonosító") : others.size() == 1 ? QStringLiteral("Hírek")
                                                                                       : QStringLiteral("Reklám – Sarki pékség"))
                           : QString::fromUtf8(titles[n % 6]);
            it.artist = wav ? QString() : QString::fromUtf8(artists[n % 6]);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            const int id = db.addItem(it);
            if (wav) others << id; else { songs << id; ++n; }
        }
        db.setItemIntro(songs[1], 9000);
        db.notifyItemsChanged();
        const int pl = db.saveSavedPlaylist(0, plCat, QStringLiteral("Esti blokk"), {songs[2], others[0], songs[3]});
        window.appendItems({others[0], songs[0], songs[1], songs[2]});
        window.insertSavedPlaylist(pl);
        // A következő óra elemei
        const QDateTime later = eter::now().addSecs(3600);
        const int next = db.playlistFor(later.date(), later.time().hour());
        for (int id : {others[1], others[2], songs[4], songs[0]}) db.addEntry(next, id, "scheduler");
        emit window.schedulerPage()->generated({});
        // A reklám a következő órában ráfektetve a hírek → zene átmenetre (2 mp-cel a mix pont után).
        for (const Entry &e : db.pendingEntries())
            if (e.item.id == others[2]) std::printf("ráfektetés a megjelenési tesztben: %d\n", window.setRowOverlay(e.id, true, 2000, 0));
        const QVector<Entry> entries = db.pendingEntries();
        if (entries.size() >= 4) {
            Transition t;
            t.fromEntryId = entries[2].id;
            t.toEntryId = entries[3].id;
            t.mixOutMs = 8000; t.fadeOutMs = 2000; t.fadeOutCurve = "scurve"; t.outEndMs = 10000;
            t.vtItemId = others[0];
            db.saveTransition(t);
            emit window.mixPage()->mixSaved({});
        }
        window.setMode(MainWindow::Assist);
        QTimer::singleShot(300, &window, [&window] { window.next(); });
        QTimer::singleShot(900, &window, [&window] { window.next(); });
        QTimer::singleShot(1300, &window, [&window, &db] {
            if (qEnvironmentVariableIsSet("ETER_LOOK_SELECT")) {
                auto *t = window.findChild<QTableWidget *>("playlistTable");
                if (t) {
                    t->setCurrentCell(2, 0);
                    t->viewport()->grab().save("selgrab.png");
                    std::printf("kijelölve; grab mentve\n");
                }
            }
            if (qEnvironmentVariableIsSet("ETER_LOOK_CLICKALL")) {
                // Valódi egéresemények a playlisten: minden sor nyilára kattintás (nyitás, csukás), kijelölés,
                // dupla kattintás a nyílra, és a mini szerkesztő gombjai.
                auto *t = window.findChild<QTableWidget *>("playlistTable");
                if (!t) return;
                auto click = [t](QPoint pt, bool dbl) {
                    QWidget *vp = t->viewport();
                    QCursor::setPos(vp->mapToGlobal(pt));
                    for (int k = 0; k < (dbl ? 2 : 1); ++k) {
                        QMouseEvent press(QEvent::MouseButtonPress, pt, vp->mapToGlobal(pt), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(vp, &press);
                        if (k == 1) {
                            QMouseEvent dc(QEvent::MouseButtonDblClick, pt, vp->mapToGlobal(pt), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                            QApplication::sendEvent(vp, &dc);
                        }
                        QMouseEvent rel(QEvent::MouseButtonRelease, pt, vp->mapToGlobal(pt), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(vp, &rel);
                        QApplication::processEvents();
                    }
                    for (int k = 0; k < 5; ++k) QApplication::processEvents();
                };
                int clicks = 0;
                for (int round = 0; round < 3; ++round)
                    for (int v = 0; v < t->rowCount(); ++v) {
                        const QRect r = t->visualRect(t->model()->index(v, 0));
                        if (!r.isValid()) continue;
                        t->scrollTo(t->model()->index(v, 0));
                        const QRect r2 = t->visualRect(t->model()->index(v, 0));
                        if (r2.height() > 60) continue;              // a mini sora
                        click(QPoint(r2.width() / 2, r2.center().y()), false);
                        click(QPoint(t->viewport()->width() - 15, r2.center().y()), round == 1);
                        ++clicks;
                        if (MiniMixCanvas *c = window.miniCanvas()) {
                            c->update();
                            QApplication::processEvents();
                        }
                        if (v >= t->rowCount()) break;
                    }
                std::printf("kattintások: %d, mini nyitva: %d\n", clicks, window.miniOpen());
                QTimer::singleShot(3000, [] { std::printf("vége, nincs összeomlás\n"); QApplication::exit(0); });
                return;
            }
            if (qEnvironmentVariableIsSet("ETER_LOOK_NOEXPAND")) return;
            window.expandStandbyTransition();
            std::printf("mini lenyitva: %d | %s\n", window.miniOpen(), qPrintable(window.debugState()));
            if (qEnvironmentVariableIsSet("ETER_LOOK_DRAG")) {
                // A következő elem húzása a mini szerkesztőben: 2 másodperccel korábbra.
                MiniMixCanvas *c = window.miniCanvas();
                if (!c) return;
                const qint64 before = c->mixMs();
                const double pxPerSec = c->width() / 24.0;
                const QPointF from(c->width() * 14 / 24.0 + 30, c->height() * 0.75), to = from - QPointF(2 * pxPerSec, 0);
                QMouseEvent press(QEvent::MouseButtonPress, from, c->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(c, &press);
                QMouseEvent move(QEvent::MouseMove, to, c->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(c, &move);
                QMouseEvent release(QEvent::MouseButtonRelease, to, c->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(c, &release);
                QApplication::processEvents();                 // a mentés a következő eseménykörben történik
                QSqlQuery q("SELECT from_entry_id, to_entry_id, mix_out_ms, out_end_ms FROM transitions ORDER BY to_entry_id");
                std::printf("húzás: mix %lld → %lld ms\n", static_cast<long long>(before), static_cast<long long>(c->mixMs()));
                while (q.next()) std::printf("  átmenet %d→%d: mix %d, vége %d\n", q.value(0).toInt(), q.value(1).toInt(), q.value(2).toInt(), q.value(3).toInt());
                Q_UNUSED(db);
            }
        });
        if (qEnvironmentVariableIsSet("ETER_MONKEY")) {
            // Véletlen kattintások, görgetés és billentyűk az egész felületen (hibakereséshez). A felugró
            // menüket és párbeszédablakokat a következő lépés bezárja; minden lépés a szabványos hibakimenetre kerül.
            struct Monkey { std::mt19937 rng; int left; int done = 0; };
            auto mk = std::make_shared<Monkey>();
            mk->rng.seed(qEnvironmentVariable("ETER_MONKEY").toUInt());
            mk->left = qEnvironmentVariableIsSet("ETER_MONKEY_N") ? qEnvironmentVariable("ETER_MONKEY_N").toInt() : 2000;
            auto *timer = new QTimer(&window);
            QObject::connect(timer, &QTimer::timeout, &window, [mk, timer, &window] {
                auto rnd = [&](int n) { return n <= 0 ? 0 : int(mk->rng() % unsigned(n)); };
                if (mk->left-- <= 0) {
                    timer->stop();
                    std::printf("majom: %d lépés, nincs összeomlás\n", mk->done);
                    std::fflush(stdout);
                    QApplication::exit(0);
                    return;
                }
                ++mk->done;
                if (QWidget *pop = QApplication::activePopupWidget()) {
                    if (rnd(3)) { std::fprintf(stderr, "#%d popup bezárás %s\n", mk->done, pop->metaObject()->className()); pop->close(); return; }
                }
                // Nyitott felugró menü mellett ablakot nem zárunk (a felhasználó sem tudja).
                if (QWidget *modal = QApplication::activePopupWidget() ? nullptr : QApplication::activeModalWidget()) {
                    if (qobject_cast<QMessageBox *>(modal) || rnd(4) == 0) {
                        std::fprintf(stderr, "#%d modális bezárás %s\n", mk->done, modal->metaObject()->className());
                        if (auto *d = qobject_cast<QDialog *>(modal)) d->reject(); else modal->close();
                        return;
                    }
                }
                // Célpont: a legfelső modális ablak, felugró elem, vagy a főablak bármely látható része.
                QWidget *root = QApplication::activePopupWidget() ? QApplication::activePopupWidget()
                              : QApplication::activeModalWidget() ? QApplication::activeModalWidget() : static_cast<QWidget *>(&window);
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (w->isVisible() && w != &window && !QApplication::activeModalWidget() && qobject_cast<QDialog *>(w) && rnd(2)) root = w;
                QList<QWidget *> all = root->findChildren<QWidget *>();
                all.push_back(root);
                QList<QWidget *> vis;
                for (QWidget *w : all)
                    if (w->isVisible() && w->width() > 2 && w->height() > 2) vis.push_back(w);
                if (vis.isEmpty()) return;
                QPointer<QWidget> picked = vis[rnd(vis.size())];
                const QPoint pick(rnd(picked->width()), rnd(picked->height()));
                const int a = rnd(100), r1 = rnd(1 << 20), r2 = rnd(1 << 20), r3 = rnd(1 << 20);
                // A lépés külön eseményként fut: ha párbeszédablakot nyit, az időzítő tovább ketyeg és bezárhatja.
                QTimer::singleShot(0, &window, [mk, picked, pick, a, r1, r2, r3] {
                if (!picked) return;
                auto rnd2 = [](int v, int n) { return n <= 0 ? 0 : v % n; };
                QWidget *w = picked;
                QPoint pt = pick;
                if (QWidget *c = w->childAt(pt)) { pt = c->mapFrom(w, pt); w = c; }
                const QPoint g = w->mapToGlobal(pt);
                QPointer<QWidget> target(w);
                std::fprintf(stderr, "#%d %s %s '%s' %d,%d\n", mk->done, a < 55 ? "kattint" : a < 65 ? "dupla" : a < 72 ? "jobb" : a < 82 ? "görget" : a < 90 ? "húz" : "billentyű",
                             w->metaObject()->className(), qPrintable(w->objectName()), pt.x(), pt.y());
                std::fflush(stderr);
                QCursor::setPos(g);
                auto mouse = [&](QEvent::Type t, Qt::MouseButton b, Qt::MouseButtons bs, QPoint p) {
                    if (!target) return;
                    QMouseEvent e(t, p, target->mapToGlobal(p), b, bs, Qt::NoModifier);
                    QApplication::sendEvent(target, &e);
                };
                if (a < 65) {
                    mouse(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, pt);
                    mouse(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, pt);
                    if (a >= 55) {
                        mouse(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, pt);
                        mouse(QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::LeftButton, pt);
                        mouse(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, pt);
                    }
                } else if (a < 72) {
                    mouse(QEvent::MouseButtonPress, Qt::RightButton, Qt::RightButton, pt);
                    mouse(QEvent::MouseButtonRelease, Qt::RightButton, Qt::NoButton, pt);
                    if (target) {
                        QContextMenuEvent ce(QContextMenuEvent::Mouse, pt, target->mapToGlobal(pt));
                        QApplication::sendEvent(target, &ce);
                    }
                } else if (a < 82) {
                    if (target) {
                        QWheelEvent we(QPointF(pt), QPointF(g), QPoint(), QPoint(0, rnd2(r1, 2) ? 120 : -120), Qt::NoButton,
                                       rnd2(r2, 4) == 0 ? Qt::ShiftModifier : Qt::NoModifier, Qt::NoScrollPhase, false);
                        QApplication::sendEvent(target, &we);
                    }
                } else if (a < 90) {
                    const QPoint to = pt + QPoint(rnd2(r1, 161) - 80, rnd2(r2, 61) - 30);
                    mouse(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, pt);
                    for (int k = 1; k <= 4; ++k) mouse(QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, pt + (to - pt) * k / 4);
                    mouse(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, to);
                } else {
                    static const int keys[] = {Qt::Key_Space, Qt::Key_Delete, Qt::Key_Return, Qt::Key_Escape, Qt::Key_Up, Qt::Key_Down,
                                               Qt::Key_Left, Qt::Key_Right, Qt::Key_A, Qt::Key_1, Qt::Key_Tab, Qt::Key_PageDown};
                    const int key = keys[rnd2(r3, int(std::size(keys)))];
                    QWidget *f = QApplication::focusWidget() ? QApplication::focusWidget() : target.data();
                    if (f) {
                        const QString text = key == Qt::Key_A ? QStringLiteral("a") : key == Qt::Key_1 ? QStringLiteral("1") : key == Qt::Key_Space ? QStringLiteral(" ") : QString();
                        QKeyEvent kp(QEvent::KeyPress, key, Qt::NoModifier, text);
                        QApplication::sendEvent(f, &kp);
                        QKeyEvent kr(QEvent::KeyRelease, key, Qt::NoModifier, text);
                        if (QApplication::focusWidget()) QApplication::sendEvent(QApplication::focusWidget(), &kr);
                    }
                }
                });
            });
            QTimer::singleShot(1500, timer, [timer] { timer->start(12); });
        }
        if (!parser.isSet(shotOpt) && !qEnvironmentVariableIsSet("ETER_LOOK_STAY") && !qEnvironmentVariableIsSet("ETER_LOOK_CLICKALL") && !qEnvironmentVariableIsSet("ETER_MONKEY")) QTimer::singleShot(2500, &window, [] { QApplication::exit(0); });
    }
    if (parser.isSet(recTestOpt)) {
        // Felvétel a Mix editorban: indítás, néhány másodperc, leállítás; létrejött-e a voice track.
        const int cat = db.categories().isEmpty() ? db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff")
                                                  : db.categories().first().id;
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = cat;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        for (QToolButton *b : window.findChildren<QToolButton *>())
            if (b->text() == QStringLiteral("Mix editor")) b->click();
        auto click = [&window](const QString &text) {
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == text) { b->click(); return true; }
            return false;
        };
        QTimer::singleShot(1500, &window, [=] { std::printf("felvétel indul: %d\n", click(QStringLiteral("Felvétel"))); });
        QTimer::singleShot(9500, &window, [=] { std::printf("felvétel vége: %d\n", click(QStringLiteral("Felvétel vége"))); });
        QTimer::singleShot(10500, &window, [=, &window, &db] {
            for (const Item &it : db.items())
                if (it.kind == "voicetrack")
                    std::printf("voice track a hangtárban: %s | %lld ms | %s | fájl megvan: %d\n", qPrintable(it.title),
                                static_cast<long long>(it.durationMs), qPrintable(it.path), QFileInfo::exists(it.path));
            for (QLabel *l : window.mixPage()->findChildren<QLabel *>())
                if (l->text().contains("Voice track") || l->text().contains("voice track")) std::printf("  felirat: %s\n", qPrintable(l->text()));
            for (QDoubleSpinBox *sp : window.mixPage()->findChildren<QDoubleSpinBox *>())
                if (sp->toolTip().contains("kezdése")) std::printf("  kezdés a mix ponthoz képest: %.1f mp\n", sp->value());
            std::printf("  mentés: %d\n", click(QStringLiteral("Mentés")));
            QTimer::singleShot(300, [&db] {
                QSqlQuery q("SELECT vt_item_id, vt_offset_ms, vt_duck_db FROM transitions");
                while (q.next()) std::printf("  átmenet az adatbázisban: vt=%d kezdés=%d ms halkítás=%.1f dB\n", q.value(0).toInt(), q.value(1).toInt(), q.value(2).toDouble());
                QApplication::exit(0);
            });
        });
    }
    if (parser.isSet(uiTestOpt)) {
        // Forgatókönyv: kategória, importálás, playlist, átrendezés, automata végigjátszás.
        auto report = [&window](const char *when) { std::printf("%-30s %s\n", when, qPrintable(window.debugState())); };
        const int cat = db.categories().isEmpty() ? db.addCategory(QStringLiteral("Teszt zene"), "music", "#5cb0ff")
                                                  : db.categories().first().id;
        QVector<int> ids;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (int id = db.itemIdByPath(f)) { ids << id; continue; }
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = cat;
            it.path = f;
            it.title = QString::fromStdString(dec.title());
            if (it.title.isEmpty()) it.title = QFileInfo(f).completeBaseName();
            it.artist = QString::fromStdString(dec.artist());
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        report("indulás (előző futásból)");
        window.appendItems(ids);
        report("hozzáadás után");
        window.moveRow(3, 0);
        report("4. sor az elejére húzva");
        window.setMode(MainWindow::Auto);
        QTimer::singleShot(1500, &window, [=, &window] { window.next(); });
        QTimer::singleShot(2500, &window, [=] { report("indítás után (automata)"); });
        QTimer::singleShot(12000, &window, [=] { report("12 mp-nél"); });
        QTimer::singleShot(20000, &window, [=, &db] {
            report("20 mp-nél");
            for (const LogRow &l : db.log(10))
                std::printf("  napló: %s  %s  %lld mp  %s\n", qPrintable(QDateTime::fromSecsSinceEpoch(l.startedAt).toString("HH:mm:ss")),
                            qPrintable(l.title), static_cast<long long>(l.endedAt ? l.endedAt - l.startedAt : -1), qPrintable(l.endReason));
            QApplication::exit(0);
        });
    }
    if (parser.isSet(shotOpt)) {
        const QString out = parser.value(shotOpt);
        // Az első elem elindul, hogy a kép adás közbeni állapotot mutasson.
        QTimer::singleShot(600, &window, [&window] {
            QMetaObject::invokeMethod(&window, "next", Qt::DirectConnection);
        });
        if (qEnvironmentVariableIsSet("ETER_SHOT_SCHED")) {
            const QString which = qEnvironmentVariable("ETER_SHOT_SCHED");
            const QString tab = which == "mix" ? TR("Mix editor") : which == "pl" ? TR("Playlist szerkesztő")
                              : which == "live" ? TR("Adás") : which == "set" ? TR("Beállítások") : which == "ev" ? TR("Időzítések") : which == "cart" ? TR("Adás") : TR("Ütemező");
            if (which == "edit" && db.itemCount() > 0) window.openItemEditor(db.items().first().id);
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == tab) b->click();
            if (which == "cart")
                for (QAbstractButton *b : window.findChildren<QAbstractButton *>())
                    if (b->text() == TR("Cartwall")) b->click();
        }
        QTimer::singleShot(1500, &window, [&window] {
            if (qEnvironmentVariableIsSet("ETER_SHOT_PICKER")) {
                // A voice track hangtár-választója; a megadott kategóriára váltunk, hogy a megjegyzés is próbára kerüljön.
                for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                    if (b->text() == QStringLiteral("Hangtár…") || b->text() == QStringLiteral("Hangtár...")) b->click();
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *combo = w->findChild<QComboBox *>("pickerCategory")) {
                        std::printf("választó megnyitva, kategória: %s\n", qPrintable(combo->currentText()));
                        const int want = qEnvironmentVariable("ETER_SHOT_PICKER").toInt();
                        if (want > 0) combo->setCurrentIndex(want);
                        std::printf("  beállítva: %s, sorok: %d\n", qPrintable(combo->currentText()), w->findChild<QTableView *>()->model()->rowCount());
                    }
            }
            if (qEnvironmentVariableIsSet("ETER_SHOT_SCHED")) return;
            window.library()->openCategories();
            window.library()->importPaths({QDir::currentPath()});
        });
        const int shotDelay = qEnvironmentVariableIsSet("ETER_SHOT_DELAY") ? qEnvironmentVariableIntValue("ETER_SHOT_DELAY") : 3200;
        QTimer::singleShot(shotDelay, &window, [&window, out] {
            window.grab().save(out);
            int n = 0;
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->isVisible() && w != &window && qobject_cast<QDialog *>(w))
                    w->grab().save(QString(out).replace(".png", QStringLiteral("-%1.png").arg(++n)));
            QApplication::exit(0);
        });
    }
    return QCoreApplication::exec();
}
