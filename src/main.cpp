#include "core/NowPlaying.h"
#include "sched/Scheduler.h"
#include "core/CrashReport.h"
#include "core/VolumeEnvelope.h"
#include "audio/Engine.h"
#include "db/Database.h"
#include "ui/Library.h"
#include "ui/MainWindow.h"
#include "ui/MixEditorPage.h"
#include "ui/SchedulerPage.h"
#include "ui/SettingsDialog.h"
#include "ui/InfoWindow.h"
#include "ui/MixPreview.h"
#include "midi/MidiControl.h"
#include "ui/PlayerWidget.h"
#include "ui/ChannelMixer.h"
#include <QElapsedTimer>
#include <QCheckBox>
#include <QStyle>
#include <QFontInfo>
#include <QScreen>
#include "ui/Theme.h"

#include <QApplication>
#include "ui/MiniMix.h"
#include "ui/TimedEventsPage.h"
#include <QKeyEvent>
#include <QSettings>
#include <QScrollArea>
#include "core/ArtistSplit.h"
#include "db/ServerSync.h"
#include "net/ArtistInfo.h"
#include "core/Vocal.h"
#include <QHeaderView>
#include <QMimeData>
#include <QDropEvent>
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
#include "core/MusicKey.h"
#include "core/Vocal.h"
#include "ui/ChannelMixer.h"
#include "ui/MixdownPage.h"
#include "core/LibraryAnalyzer.h"
#include "ui/BackupWindow.h"
#include "net/WebSync.h"
#include "net/StreamFetcher.h"
#include <QRegularExpression>
#include "net/Remote.h"
#include <QTabWidget>
#include <QProcess>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include "audio/Analysis.h"
#include <QTemporaryDir>
#include <QNetworkProxy>
#include <QTimer>
#include <QStackedWidget>

#include <array>
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

// Szerkesztő mód, második forgatókönyv: beszúrás a hangtárból, fájl és voice track feltöltése, jogok.
static void runEditorTest2(RemoteEditorWindow &editor)
{
    auto step = std::make_shared<int>(0);
    QObject::connect(editor.client(), &remote::Client::failed, &editor, [](const QString &why) {
        std::printf("szerkesztő: nem sikerült: %s\n", qPrintable(why));
        std::fflush(stdout);
        QApplication::exit(2);
    });
    QObject::connect(editor.client(), &remote::Client::loggedIn, &editor, [&editor](const QString &) {
        std::printf("szerkesztő: belépve, jogok: [%s]\n", qPrintable(editor.client()->perms().join(',')));
        std::fflush(stdout);
    });
    QObject::connect(&editor, &RemoteEditorWindow::opAnswered, &editor, [](int id, bool ok, const QString &err) {
        std::printf("szerkesztő: válasz #%d: %s %s\n", id, ok ? "rendben" : "elutasítva", qPrintable(err));
        std::fflush(stdout);
    });
    auto entryOf = [](const QString &title) {
        QSqlQuery q;
        q.prepare("SELECT e.id FROM playlist_entries e JOIN items i ON i.id = e.item_id WHERE i.title = ? ORDER BY e.id LIMIT 1");
        q.addBindValue(title);
        q.exec();
        return q.next() ? q.value(0).toInt() : 0;
    };
    auto itemOf = [](const QString &title) {
        QSqlQuery q;
        q.prepare("SELECT id FROM items WHERE title = ?");
        q.addBindValue(title);
        q.exec();
        return q.next() ? q.value(0).toInt() : 0;
    };
    QObject::connect(&editor, &RemoteEditorWindow::stateApplied, &editor, [&editor, step, entryOf, itemOf] {
        if (*step != 0) return;
        *step = 1;
        std::printf("szerkesztő: állapot: %s\n", qPrintable(editor.debugState()));
        std::fflush(stdout);
        QTimer::singleShot(400, &editor, [&editor, entryOf, itemOf] {
            std::printf("szerkesztő: „c” beszúrása a hangtárból „hirek” elé\n");
            editor.requestInsert({itemOf("c")}, entryOf("hirek"));
        });
        QTimer::singleShot(1200, &editor, [&editor, entryOf] {
            std::printf("szerkesztő: „b” áthelyezése a végére (sorrend joga kell)\n");
            editor.requestMove(entryOf("b"), 0);
        });
        QTimer::singleShot(2000, &editor, [&editor] {
            const QString f = qEnvironmentVariable("ETER_EDITOR_UPLOAD");
            if (f.isEmpty()) return;
            int cat = 0;
            QSqlQuery q("SELECT id FROM categories WHERE kind = 'music' LIMIT 1");
            if (q.next()) cat = q.value(0).toInt();
            std::printf("szerkesztő: feltöltés: %s\n", qPrintable(QFileInfo(f).fileName()));
            std::fflush(stdout);
            editor.uploadFile(f, cat, false, QString(), [&editor](int id) {
                std::printf("szerkesztő: feltöltve, új elem: %d\n", id);
                std::fflush(stdout);
                if (id) editor.requestInsert({id}, 0);
            });
        });
        QTimer::singleShot(2200, &editor, [&editor] {
            const QString f = qEnvironmentVariable("ETER_EDITOR_VT");
            if (f.isEmpty()) return;
            std::printf("szerkesztő: voice track feltöltése: %s\n", qPrintable(QFileInfo(f).fileName()));
            editor.uploadFile(f, 0, true, QStringLiteral("Teszt VT"), [](int id) {
                std::printf("szerkesztő: voice track feltöltve, új elem: %d\n", id);
                QSqlQuery q;
                q.prepare("SELECT i.title, c.kind, i.file_path FROM items i JOIN categories c ON c.id = i.category_id WHERE i.id = ?");
                q.addBindValue(id);
                q.exec();
                if (q.next()) std::printf("szerkesztő: a tükörben: %s (%s) %s\n", qPrintable(q.value(0).toString()), qPrintable(q.value(1).toString()),
                                          qPrintable(q.value(2).toString()));
                std::fflush(stdout);
            });
        });
        QTimer::singleShot(qEnvironmentVariableIsSet("ETER_EDITOR_END") ? qEnvironmentVariableIntValue("ETER_EDITOR_END") : 9000, &editor, [&editor] {
            std::printf("szerkesztő: végső állapot: %s\n", qPrintable(editor.debugState()));
            editor.grab().save("editor2.png");
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
    // A felület mérete (Beállítások → Megjelenés): a Qt méretezése az alkalmazás létrehozása előtt.
    if (!headless && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        const int pct = QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("Eter"), QStringLiteral("Eter"))
                            .value(QStringLiteral("ui/scale"), 100).toInt();
        // 100 % alatt (1-nél kisebb szorzó) a Qt hibásan rajzol (elcsúszott, levágott elemek): ott nincs méretezés.
        if (pct > 100 && pct <= 200) qputenv("QT_SCALE_FACTOR", QByteArray::number(pct / 100.0, 'f', 2));
    }
    std::unique_ptr<QCoreApplication> holder(headless ? new QCoreApplication(argc, argv)
                                                      : new QApplication(argc, argv));
    QCoreApplication &app = *holder;
    QApplication::setOrganizationName("Eter");
    QApplication::setApplicationName("Eter");
    if (!headless) QApplication::setApplicationDisplayName(QStringLiteral("Éter"));
    QApplication::setApplicationVersion(ETER_VERSION);
    if (!headless) QApplication::setDesktopFileName("eter");
    // Összeomláskor a hívási lánc a terminálra és a crash.log-ba kerül (hibakereséshez).
    {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty()) dir = QDir::homePath();
        crash::install(QDir(dir).filePath(QStringLiteral("crash.log")));
    }

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
    QCommandLineOption backupOpt("backup", QStringLiteral("Tartalék playout: a fő playout tükrözése, és az adás átvétele, ha a fő gép eltűnik."));
    QCommandLineOption editorOpt("editor", QStringLiteral("Szerkesztő mód: egy másik gépen futó playout playlistjének szerkesztése a hálózaton át."));
    QCommandLineOption flowTestOpt("flowtest", QStringLiteral("Ütemező: energia, tempó, napszak (fejlesztéshez)."));
    QCommandLineOption bpmOpt("bpm", QStringLiteral("A megadott fájlok tempójának becslése (fejlesztéshez)."));
    QCommandLineOption loggerTestOpt("loggertest", QStringLiteral("Adásrögzítés forgatókönyv (fejlesztéshez)."));
    QCommandLineOption fixTestOpt("fixtest", QStringLiteral("Pontos időre igazítás forgatókönyv (fejlesztéshez)."));
    QCommandLineOption fileTestOpt("filetest", QStringLiteral("Hiányzó fájlok forgatókönyv (fejlesztéshez)."));
    QCommandLineOption clickTestOpt("clicktest", QStringLiteral("Mix editor kattintás és nem zenei elem forgatókönyv (fejlesztéshez)."));
    QCommandLineOption countTestOpt("counttest", QStringLiteral("Visszaszámláló forgatókönyv (fejlesztéshez)."));
    QCommandLineOption serveTestOpt("servetest", QStringLiteral("Távoli szerkesztés: playout oldali forgatókönyv (fejlesztéshez)."));
    QCommandLineOption overlayTestOpt("overlaytest", QStringLiteral("Ráfektetés forgatókönyv (fejlesztéshez)."));
    QCommandLineOption lookTestOpt("looktest", QStringLiteral("A playlist megjelenése (fejlesztéshez)."));
    QCommandLineOption mixTestOpt("mixtest", QStringLiteral("Mix forgatókönyv (fejlesztéshez)."));
    QCommandLineOption recTestOpt("rectest", QStringLiteral("Felvétel forgatókönyv (fejlesztéshez)."));
    QCommandLineOption schedTestOpt("schedtest", QStringLiteral("Ütemező forgatókönyv (fejlesztéshez)."));
    QCommandLineOption fillTestOpt("filltest", QStringLiteral("Kitöltés a fix pontig hosszú számokkal (fejlesztéshez)."));
    QCommandLineOption freshTestOpt("freshtest", QStringLiteral("Frissen importált elem a playlistbe adás közben (fejlesztéshez)."));
    QCommandLineOption envTestOpt("envtest", QStringLiteral("Hangerőgörbe a Mix editorban és az elemszerkesztőben (fejlesztéshez)."));
    QCommandLineOption dbTestOpt("dbtest", QStringLiteral("Adatbázis export és import (fejlesztéshez)."));
    QCommandLineOption threeTestOpt("threetest", QStringLiteral("Három lejátszó: zene → jingle → zene, automatikus mix (fejlesztéshez)."));
    QCommandLineOption fakeTimeOpt("fake-time", QStringLiteral("A program órája ettől az időponttól indul (fejlesztéshez)."), "ÓÓ:pp:mm");
    parser.addOptions({backendOpt, dbOpt, selfTestOpt, shotOpt, iconOpt, uiTestOpt, importTestOpt, schedTestOpt, fakeTimeOpt, mixTestOpt, recTestOpt, plTestOpt, cartTestOpt, editTestOpt, nextTestOpt, eventTestOpt, lookTestOpt, overlayTestOpt, editorOpt, serveTestOpt, countTestOpt, clickTestOpt, fileTestOpt, fixTestOpt, loggerTestOpt, bpmOpt, flowTestOpt, backupOpt, threeTestOpt, dbTestOpt, envTestOpt, freshTestOpt, fillTestOpt});
    parser.addPositionalArgument("fájlok", QStringLiteral("Induláskor importálandó hangfájlok vagy mappák."), "[fájlok…]");
    parser.process(app);

    if (parser.isSet(iconOpt))
        return theme::appIcon().pixmap(256, 256).save(parser.value(iconOpt)) ? 0 : 1;
    if (parser.isSet(bpmOpt)) {
        std::atomic<bool> never{false};
        for (const QString &f : parser.positionalArguments()) {
            QElapsedTimer t;
            t.start();
            const double bpm = eter::estimateBpm(f.toStdString(), never);
            std::printf("%-28s %6.1f BPM  (%lld ms)\n", qPrintable(QFileInfo(f).fileName()), bpm, static_cast<long long>(t.elapsed()));
        }
        return 0;
    }
    if (parser.isSet(selfTestOpt))
        return selfTest(parser.value(selfTestOpt), parser.isSet(backendOpt) ? parser.value(backendOpt) : "null");

    theme::apply(*static_cast<QApplication *>(holder.get()));
    if (qEnvironmentVariableIsSet("ETER_SCHEME")) {
        // Fejlesztéshez: színséma indításkor (a képernyőképekhez), pl. builtin:light.
        const theme::Scheme sc = theme::scheme(qEnvironmentVariable("ETER_SCHEME"));
        if (!sc.id.isEmpty()) theme::setCurrentColors(sc.colors, sc.id);
    }
    QtStreamInput::install();   // külső hangstreamek (http/https) a lejátszókban
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
    if (parser.isSet(backupOpt)) {
        // Tartalék mód: készenlétben a fő gép tükre; átvételkor ugyanebben a folyamatban egy rendes főablak indul a másolattal.
        const QString replica = parser.isSet(dbOpt) ? parser.value(dbOpt)
            : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/eter/backup-replica.sqlite";
        QDir().mkpath(QFileInfo(replica).absolutePath());
        static QLockFile lock(replica + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(10000)) {                         // a visszaadás utáni újraindulásnál az előző még kiléphet
            QMessageBox::warning(nullptr, QStringLiteral("Éter"), TR("A tartalék playout már fut ezen a gépen."));
            return 1;
        }
        auto *standby = new BackupWindow(replica);
        standby->show();
        standby->start(qEnvironmentVariable("ETER_BACKUP_PASS"));
        static std::unique_ptr<Database> takenDb;
        static std::unique_ptr<MainWindow> takenWindow;
        const QString backend = parser.value(backendOpt);
        QStringList relaunch{QStringLiteral("--backup")};
        if (parser.isSet(dbOpt)) relaunch << QStringLiteral("--db") << parser.value(dbOpt);
        if (parser.isSet(backendOpt)) relaunch << QStringLiteral("--backend") << backend;
        QObject::connect(standby, &BackupWindow::takeoverRequested, standby, [standby, replica, backend, relaunch] {
            const QJsonObject state = standby->lastState();
            qint64 pos = 0;
            const int entry = standby->playingEntry(&pos);
            const BackupConfig cfg = standby->config();
            standby->stop();
            standby->hide();
            standby->deleteLater();
            takenDb = std::make_unique<Database>();
            QString err;
            if (!takenDb->open(replica, &err)) {
                QMessageBox::critical(nullptr, QStringLiteral("Éter"), TR("A másolat nem nyitható meg:\n%1\n\n%2").arg(replica, err));
                return;
            }
            if (!state.isEmpty()) BackupWindow::applyLiveState(*takenDb, state);
            takenWindow = std::make_unique<MainWindow>(takenDb.get(), backend);
            takenWindow->show();
            takenWindow->takeOver(entry, pos);
            takenWindow->watchOrigin(cfg.host, cfg.port, cfg.user, cfg.salt, cfg.key, relaunch);
            std::printf("tartalék: adásban: %s\n", qPrintable(takenWindow->debugState()));
            std::fflush(stdout);
            if (qEnvironmentVariableIsSet("ETER_BACKUP_WATCH")) {
                auto *t = new QTimer(takenWindow.get());
                QObject::connect(t, &QTimer::timeout, [] {
                    if (takenWindow) std::printf("tartalék: %s\n", qPrintable(takenWindow->debugState()));
                    std::fflush(stdout);
                });
                t->start(1000);
            }
        });
        if (qEnvironmentVariableIsSet("ETER_BACKUP_WATCH")) {
            // Fejlesztéshez: a készenlét állapota másodpercenként, és kilépés a megadott idő után.
            auto *t = new QTimer(standby);
            auto last = std::make_shared<QString>();
            QObject::connect(t, &QTimer::timeout, standby, [standby, last] {
                QString s = standby->debugText();
                s.replace(QRegularExpression(QStringLiteral("\\d+[,.]\\d mp")), QStringLiteral("…"));
                s.replace(QRegularExpression(QStringLiteral("\\d+ mp")), QStringLiteral("…"));
                if (s != *last) std::printf("tartalék: %s\n", qPrintable(standby->debugText()));
                *last = s;
                std::fflush(stdout);
            });
            t->start(500);
            QTimer::singleShot(qEnvironmentVariableIntValue("ETER_BACKUP_WATCH") * 1000, [] {
                if (takenWindow) {
                    takenWindow->grab().save("backup-onair.png");
                    std::printf("tartalék: végső állapot: %s\n", qPrintable(takenWindow->debugState()));
                }
                std::fflush(stdout);
                QApplication::exit(0);
            });
            if (qEnvironmentVariableIsSet("ETER_BACKUP_SHOT"))
                QTimer::singleShot(qEnvironmentVariableIntValue("ETER_BACKUP_SHOT") * 1000, standby, [standby] { standby->grab().save("backup-standby.png"); });
            if (qEnvironmentVariableIsSet("ETER_BACKUP_HANDBACK"))
                QTimer::singleShot(qEnvironmentVariableIntValue("ETER_BACKUP_HANDBACK") * 1000, [] {
                    if (!takenWindow) return;
                    std::printf("tartalék: visszaadás\n");
                    std::fflush(stdout);
                    // A megerősítő kérdés a tesztben magától igent kap.
                    QTimer::singleShot(300, [] {
                        for (QWidget *w : QApplication::topLevelWidgets())
                            if (auto *box = qobject_cast<QMessageBox *>(w))
                                if (auto *yes = box->button(QMessageBox::Yes)) yes->click();
                    });
                    takenWindow->handBack();
                });
        }
        const int rc = QCoreApplication::exec();
        takenWindow.reset();
        takenDb.reset();
        return rc;
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
            t.user = qEnvironmentVariable("ETER_EDITOR_USER");
            t.password = qEnvironmentVariable("ETER_EDITOR_PASS");
            t.pathFrom = qEnvironmentVariable("ETER_EDITOR_FROM");
            t.pathTo = qEnvironmentVariable("ETER_EDITOR_TO");
            editor.connectTo(t);
            if (qEnvironmentVariableIsSet("ETER_EDITOR_TEST")) runEditorTest(editor);
            if (qEnvironmentVariableIsSet("ETER_EDITOR_TEST2")) runEditorTest2(editor);
            if (qEnvironmentVariableIsSet("ETER_EDITOR_CLOSE"))      // az ablak bezárása (fejlesztéshez): ki kell lépnie
                QTimer::singleShot(qEnvironmentVariableIntValue("ETER_EDITOR_CLOSE") * 1000, &editor, [&editor] {
                    std::printf("szerkesztő: bezárás\n");
                    std::fflush(stdout);
                    editor.close();
                });
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
    // Újraindításkor (például adatbázis-import után) megvárja, amíg az előző példány kilép.
    if (!instanceLock.tryLock(qEnvironmentVariableIsSet("ETER_RELAUNCH") ? 20000 : 200)) {
        qint64 pid = 0;
        QString host, app;
        instanceLock.getLockInfo(&pid, &host, &app);
        std::fprintf(stderr, "Az Éter már fut ezzel az adatbázissal (folyamat: %lld)\n", static_cast<long long>(pid));
        QMessageBox::warning(nullptr, QStringLiteral("Éter"),
                             TR("Az Éter már fut ezzel az adatbázissal (folyamat: %1).\n\nKét példány ugyanazt a playlistet "
                                "játszaná egyszerre, ezért ez a példány nem indul el.").arg(pid));
        return 1;
    }
    // Külső SQL szerver: ha a szerveren egy másik gép újabb adatot hagyott, az lesz az importálandó adatbázis.
    const bool sqlActive = sqlsrv::startupCheck(dbPath, !qEnvironmentVariableIsSet("ETER_SQL_NONINTERACTIVE"));
    {
        const bool pending = QFile::exists(dbPath + QStringLiteral(".import"));
        const QString importErr = MainWindow::applyPendingImport(dbPath);
        if (!importErr.isEmpty())
            QMessageBox::warning(nullptr, QStringLiteral("Éter"), TR("Az adatbázis importálása nem sikerült: %1\n\nA program a korábbi adatbázissal indul.").arg(importErr));
        else if (pending)
            std::fprintf(stderr, "Importált adatbázis a helyén: %s\n", qPrintable(dbPath));
    }
    if (!db.open(dbPath, &dbErr)) {
        QMessageBox::critical(nullptr, QStringLiteral("Éter"),
                              TR("Az adatbázis nem nyitható meg:\n%1\n\n%2").arg(dbPath, dbErr));
        return 1;
    }
    // A fejlesztői forgatókönyvek a Mix editor mentett átmeneteivel dolgoznak; az automatikus mixet az
    // ETER_MIX=auto kapcsolja be nekik (a valódi indításnál az adatbázis beállítása számít).
    {
        bool testRun = false;
        for (int i = 1; i < argc; ++i)
            if (QByteArray(argv[i]).startsWith("--") && QByteArray(argv[i]).contains("test")) testRun = true;
        if (testRun && db.setting("mix_mode").isEmpty())
            db.setSetting("mix_mode", qEnvironmentVariable("ETER_MIX", "editor"));
        // A háttérelemzés ne írja át a forgatókönyvek adatait (ETER_ANALYZE=1 bekapcsolja nekik).
        if (testRun && !qEnvironmentVariableIsSet("ETER_ANALYZE")) qputenv("ETER_ANALYZE", "0");
    }
    MainWindow window(&db, parser.value(backendOpt));
    ServerSync serverSync(dbPath);
    window.setServerSync(&serverSync);
    if (sqlActive) serverSync.start();
    window.show();
    if (qEnvironmentVariableIsSet("ETER_DIAG")) {
        // Hibakereséshez: a képernyő, a méretezés és a betűk adatai, majd a főbb elemek helye (a terminálba).
        QTimer::singleShot(3000, &window, [&window] {
            QScreen *sc = window.screen();
            std::printf("platform=%s stílus=%s QT_SCALE_FACTOR=%s ui/scale=%d\n", qPrintable(QGuiApplication::platformName()),
                        qPrintable(QApplication::style()->name()), qgetenv("QT_SCALE_FACTOR").constData(), theme::uiScalePercent());
            if (sc)
                std::printf("képernyő=%s %dx%d dpr=%.2f logikai dpi=%.1f fizikai dpi=%.1f\n", qPrintable(sc->name()), sc->geometry().width(),
                            sc->geometry().height(), sc->devicePixelRatio(), sc->logicalDotsPerInch(), sc->physicalDotsPerInch());
            const QFont f = QApplication::font();
            std::printf("betű=%s px=%d pt=%.1f talált=%s\n", qPrintable(f.family()), f.pixelSize(), f.pointSizeF(),
                        qPrintable(QFontInfo(f).family()));
            std::printf("ablak=%dx%d maximalizált=%d séma=%s\n", window.width(), window.height(), int(window.isMaximized()),
                        qPrintable(theme::currentSchemeId()));
            for (QWidget *w : window.findChildren<QWidget *>()) {
                if (w->objectName().isEmpty() || !w->isVisible()) continue;
                const QRect g(w->mapTo(&window, QPoint(0, 0)), w->size());
                std::printf("  %-22s %4d,%4d %4dx%-4d hint %dx%d min %dx%d\n", qPrintable(w->objectName()), g.x(), g.y(), g.width(), g.height(),
                            w->sizeHint().width(), w->sizeHint().height(), w->minimumSizeHint().width(), w->minimumSizeHint().height());
            }
            std::fflush(stdout);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_MIXDOWN_TEST")) {
        // Fejlesztéshez: a mixdown. A fájlok sorban (a „vt” nevű a 2. átmenet voice trackje), az átmenetek, majd
        // kikeverés MP3-ba és WAV-ba (44,1 kHz), a hosszuk, és képek (ETER_MIXDOWN_TEST = a mappa).
        const QString dir = qEnvironmentVariable("ETER_MIXDOWN_TEST");
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingles = db.addCategory(QStringLiteral("Szignál"), "jingle", "#ff8f85");
        QVector<int> ids;
        QString vt;
        for (const QString &f : parser.positionalArguments()) {
            if (QFileInfo(f).completeBaseName() == QLatin1String("vt")) {
                vt = QFileInfo(f).absoluteFilePath();
                continue;
            }
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = QFileInfo(f).completeBaseName().startsWith(QLatin1String("jingle")) ? jingles : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        MixdownPage *md = window.mixdownPage();
        md->addItems(ids);
        QTimer::singleShot(3000, &window, [&window, md, vt, dir] {
            if (!vt.isEmpty()) {
                md->setVoiceTrack(1, vt, QStringLiteral("VT teszt"));
            }
            std::printf("automatikus:\n%s\n", qPrintable(md->debugState()));
            md->moveClip(2, md->startOf(2) - 3000);   // a 3. elem 3 mp-cel korábban (kézzel)
            std::printf("kézi igazítás után:\n%s\n", qPrintable(md->debugState()));
            const qint64 expect = md->totalMs();
            QElapsedTimer t;
            t.start();
            QString err;
            const bool a = md->exportTo(dir + "/mixdown-teszt.mp3", true, 192, 16, 48000, true, true, &err);
            const qint64 tMp3 = t.restart();
            const bool b = md->exportTo(dir + "/mixdown-teszt.wav", false, 320, 24, 44100, false, false, &err);
            const qint64 tWav = t.elapsed();
            for (const QString &f : {dir + "/mixdown-teszt.mp3", dir + "/mixdown-teszt.wav"}) {
                eter::Decoder dec;
                dec.open(f.toStdString());
                std::vector<float> buf(4096 * 2);
                int64_t n = 0;
                for (int r; (r = dec.read(buf.data(), 4096)) > 0;) n += r;
                std::printf("%s: %lld ms (várt %lld ms)\n", qPrintable(QFileInfo(f).fileName()), (long long)(n * 1000 / eter::kRate), (long long)expect);
            }
            std::printf("MP3 %s %lld ms alatt · WAV %s %lld ms alatt %s\n", a ? "kész" : "HIBA", (long long)tMp3, b ? "kész" : "HIBA",
                        (long long)tWav, qPrintable(err));
            std::fflush(stdout);
            for (QToolButton *bt : window.findChildren<QToolButton *>())
                if (bt->text() == TR("Mixdown")) bt->click();
            md->select(1);
            QTimer::singleShot(800, &window, [&window, md, dir] {
                // Hangerőgörbe egérrel: a görbe mód bekapcsolása, kattintás az 1. elem sávjára (új pont), húzás lefelé.
                for (QPushButton *b : md->findChildren<QPushButton *>())
                    if (b->text() == TR("Hangerőgörbe")) b->setChecked(true);
                QWidget *tl = md->findChild<QWidget *>("mdTimeline");
                auto mouse = [tl](QEvent::Type type, QPointF pt, Qt::MouseButton b, Qt::MouseButtons bs) {
                    QMouseEvent ev(type, pt, tl->mapToGlobal(pt), b, bs, Qt::NoModifier);
                    QApplication::sendEvent(tl, &ev);
                };
                const double y = 22 + 30;   // az 1. sáv teteje közelében (0 dB körül)
                mouse(QEvent::MouseButtonPress, {20, y}, Qt::LeftButton, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, {20, y}, Qt::LeftButton, Qt::NoButton);
                mouse(QEvent::MouseButtonPress, {40, y}, Qt::LeftButton, Qt::LeftButton);
                mouse(QEvent::MouseMove, {40, y + 40}, Qt::NoButton, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, {40, y + 40}, Qt::LeftButton, Qt::NoButton);
                std::printf("egérrel rajzolt görbe az 1. elemen: %s\n", qPrintable(env::toString(md->entries()[0].env)));
                // Behúzás az idővonalra: a szignál a 2. sávra, 20 mp-hez; egy voice track a 3. elem elé
                const int jingle = md->entries()[2].item.id;
                md->dropItems({jingle}, 20000, 1);
                std::printf("behúzás után:\n%s\n", qPrintable(md->debugState()));
                {
                    // Voice track a lejátszási pozícióra (az 1. elem közepére kattintva), majd törlése: az elem marad.
                    const int n0 = md->entries().size();
                    md->select(1);
                    md->setPlayhead(md->startOf(1) + 30000);
                    const QString vtFile = qEnvironmentVariable("ETER_MIXDOWN_VT");
                    if (!vtFile.isEmpty()) {
                        md->placeVoice(vtFile, QStringLiteral("teszt VT"));
                        const int i = md->selected();
                        const auto &e = md->entries()[i];
                        const qint64 a = i + 1 < md->entries().size() ? md->startOf(i + 1) : md->startOf(i) + e.end - e.cueIn;
                        std::printf("VT a(z) %d. elemnél, kezdete %lld ms (várt %lld ms), kijelölve: %d\n", i, (long long)(a + e.vtOffsetMs),
                                    (long long)(md->startOf(1) + 30000), int(md->vtSelected()));
                        md->deleteSelection();
                        std::printf("törlés után: elemek %d (előtte %d), VT maradt: %d\n", int(md->entries().size()), n0,
                                    int(!md->entries()[i].vtPath.isEmpty()));
                    }
                }
                window.grab().save(dir + "/mixdown.png");
                // Pontos görbe a mérhetőséghez: 6–14 mp között −20 dB az 1. elemen, a mixdown WAV-ban mérve
                MixdownPage::Entry e0 = md->entries()[0];
                Q_UNUSED(e0);
                md->select(0);
                while (!md->entries()[0].env.isEmpty()) md->envRemove(0, 0);
                md->envAdd(0, 5000, 0);
                md->envAdd(0, 6000, -20);
                md->envAdd(0, 14000, -20);
                md->envAdd(0, 15000, 0);
                QString err;
                md->exportTo(dir + "/env.wav", false, 320, 16, 48000, false, false, &err);
                eter::Decoder dec;
                dec.open((dir + "/env.wav").toStdString());
                std::vector<float> all;
                std::vector<float> buf(4800 * 2);
                for (int r; (r = dec.read(buf.data(), 4800)) > 0 && all.size() < size_t(eter::kRate) * 2 * 30;)
                    all.insert(all.end(), buf.begin(), buf.begin() + r * 2);
                auto rms = [&all](double a, double b) {
                    double s = 0;
                    size_t n = 0;
                    for (size_t i = size_t(a * eter::kRate) * 2; i < size_t(b * eter::kRate) * 2 && i < all.size(); ++i, ++n) s += double(all[i]) * all[i];
                    return 10 * std::log10(s / std::max<size_t>(1, n));
                };
                std::printf("görbe a kész fájlban: 1–4 mp %.1f dB · 8–12 mp %.1f dB · 18–22 mp %.1f dB\n", rms(1, 4), rms(8, 12), rms(18, 22));
                QApplication::exit(0);
            });
        });
    }
    if (qEnvironmentVariableIsSet("ETER_STREAMDLG_SHOT")) {
        // Fejlesztéshez: az „Élő stream” ablak képe a kapcsolat ellenőrzése után (ETER_STREAMDLG_URL).
        db.addCategory(QStringLiteral("Átvétel"), "other", "#3fd0b5");
        QTimer::singleShot(800, &window, [&window] {
            QTimer::singleShot(400, &window, [] {
                auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dlg) { QApplication::exit(1); return; }
                const QList<QLineEdit *> edits = dlg->findChildren<QLineEdit *>();
                if (edits.size() >= 3) {
                    edits[0]->setText(QStringLiteral("Partnerrádió – esti műsor"));
                    edits[1]->setText(QStringLiteral("Partnerrádió"));
                    edits[2]->setText(qEnvironmentVariable("ETER_STREAMDLG_URL"));
                }
                for (QPushButton *b : dlg->findChildren<QPushButton *>())
                    if (b->text() == TR("Kapcsolat ellenőrzése")) b->click();
                QTimer::singleShot(5000, dlg, [dlg] {
                    dlg->grab().save(qEnvironmentVariable("ETER_STREAMDLG_SHOT"));
                    for (QLabel *l : dlg->findChildren<QLabel *>()) std::printf("címke: %s\n", qPrintable(l->text()));
                    dlg->reject();
                    QApplication::exit(0);
                });
            });
            window.library()->editStream(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_STREAM_TEST")) {
        // Fejlesztéshez: élő stream a playlistben. Sorrend: az első fájl, a stream (ETER_STREAM_TEST = URL,
        // ETER_STREAM_LEN = tervezett hossz ms), a többi fájl; automata mód. Félmásodpercenként kiírja, mi szól, és a
        // stream állapotát; ETER_STREAM_SECS után kilép (ETER_STREAM_SHOT: kép a főablakról közben).
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int relay = db.addCategory(QStringLiteral("Átvétel"), "other", "#3fd0b5");
        QVector<int> ids;
        int n = 0;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QStringLiteral("Dal %1").arg(++n);
            it.artist = QStringLiteral("Előadó %1").arg(n);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        Item st;
        st.categoryId = relay;
        st.path = qEnvironmentVariable("ETER_STREAM_TEST");
        st.title = QStringLiteral("Partnerrádió");
        st.artist = QStringLiteral("Élő");
        st.durationMs = qEnvironmentVariableIsSet("ETER_STREAM_LEN") ? qEnvironmentVariableIntValue("ETER_STREAM_LEN") : 15000;
        const int streamId = db.addItem(st);
        db.notifyItemsChanged();
        QVector<int> order = ids.mid(0, 1);
        order << streamId << ids.mid(1);
        window.appendItems(order);
        QTimer::singleShot(1000, &window, [&window] {
            window.setMode(MainWindow::Auto);
            window.next();
        });
        auto *e = window.engine();
        auto *t = new QTimer(&window);
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        QObject::connect(t, &QTimer::timeout, &window, [e, clock] {
            const eter::Engine::Channel ch[3] = {eter::Engine::Player1, eter::Engine::Player2, eter::Engine::Player3};
            QStringList parts;
            for (int i = 0; i < 3; ++i) {
                auto &s = e->source(ch[i]);
                if (!s.loaded()) continue;
                QString name = s.isStream() ? QStringLiteral("STREAM") : QFileInfo(QString::fromStdString(s.path())).completeBaseName();
                QString line = QStringLiteral("%1%2 %3s").arg(QChar('A' + i)).arg(s.playing() ? "▶" : "·").arg(s.position() / double(eter::kRate), 0, 'f', 1);
                if (s.isStream()) line += QStringLiteral(" [%1 | %2]").arg(QString::fromStdString(s.streamState()), QString::fromStdString(s.streamTitle()));
                parts << QStringLiteral("%1 %2").arg(name, line);
            }
            std::printf("[%6.1f] %s\n", clock->elapsed() / 1000.0, qPrintable(parts.join(QStringLiteral("  |  "))));
            std::fflush(stdout);
        });
        t->start(500);
        if (qEnvironmentVariableIsSet("ETER_STREAM_SHOT"))
            QTimer::singleShot(qEnvironmentVariableIntValue("ETER_STREAM_SHOT_AT") * 1000, &window, [&window] {
                window.grab().save(qEnvironmentVariable("ETER_STREAM_SHOT"));
            });
        QTimer::singleShot(qEnvironmentVariableIntValue("ETER_STREAM_SECS") * 1000, &window, [] { QApplication::exit(0); });
    }
    if (qEnvironmentVariableIsSet("ETER_TPL_TEST")) {
        // Fejlesztéshez: mix sablonok. A Beállítások → Automatikus mix lapon új sablon (ETER_TEMPLATE_NAME), a „Dance”
        // kategóriához rendelve; a beállítások kategóriánként; képek: a beállítás lap és a Kategóriák ablak.
        const QString dir = qEnvironmentVariable("ETER_TPL_TEST");
        const int pop = db.addCategory(QStringLiteral("Pop"), "music", "#5cb0ff");
        const int dance = db.addCategory(QStringLiteral("Dance"), "music", "#f472b6");
        window.resize(1400, 860);
        QTimer::singleShot(500, &window, [&window, &db, dir, pop, dance] {
            auto *dlg = new SettingsDialog(&window);
            dlg->resize(860, 900);
            dlg->show();
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("Automatikus mix")) tabs->setCurrentIndex(i);
            // Az űrlapon hosszabb átfedés és lágyabb intró, majd mentés új sablonként.
            QDoubleSpinBox *maxOv = nullptr;
            for (auto *sp : dlg->findChildren<QDoubleSpinBox *>())
                if (sp->toolTip().startsWith(TR("Semmilyen esetben"))) maxOv = sp;
            if (maxOv) maxOv->setValue(16);
            if (auto *b = dlg->findChild<QPushButton *>("mixTemplateNew")) b->click();
            setCategoryMixTemplate(&db, dance, loadMixTemplates(&db).value(0).id);
            emit db.categoriesChanged();
            // Az alapbeállítás marad (8 mp); vissza az alapra és alkalmazás nélkül.
            MixSettingsCache cache;
            std::printf("sablonok: %d · Pop: átfedés %d ms (sablon %d) · Dance: átfedés %d ms (sablon %d)\n",
                        int(loadMixTemplates(&db).size()), cache.get(&db, pop).smart.maxOverlapMs, cache.get(&db, pop).templateId,
                        cache.get(&db, dance).smart.maxOverlapMs, cache.get(&db, dance).templateId);
            if (auto *sel = dlg->findChild<QComboBox *>("mixTemplate")) {
                sel->setCurrentIndex(1);
                emit sel->activated(1);
                std::printf("szerkesztett: %s\n", qPrintable(sel->currentText()));
            }
            QApplication::processEvents();
            if (auto *area = dlg->findChild<QScrollArea *>()) area->verticalScrollBar()->setValue(0);
            dlg->grab().save(dir + "/settings_template.png");
            dlg->close();
            window.library()->openCategories();
            QApplication::processEvents();
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->windowTitle() == TR("Kategóriák") && w->isVisible()) {
                    w->resize(760, 420);
                    QApplication::processEvents();
                    w->grab().save(dir + "/categories.png");
                }
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_INFO_TEST")) {
        // Fejlesztéshez: az Infó ablak állapotai (MusicBrainz-utánzat: ETER_MB_URL, ETER_WD_URL, ETER_WP_URL).
        const QString dir = qEnvironmentVariable("ETER_INFO_TEST");
        const int music = db.addCategory(QStringLiteral("80-as évek"), "music", "#5cb0ff");
        const QStringList pairs{QStringLiteral("Duran Duran|Girls on Film"), QStringLiteral("Ismeretlen Zenekar|Valami"),
                                QStringLiteral("Adele|Hello")};
        const QStringList fs = parser.positionalArguments();
        QVector<int> ids;
        for (int k = 0; k < pairs.size() && k < fs.size(); ++k) {
            eter::Decoder dec;
            if (!dec.open(fs[k].toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(fs[k]).absoluteFilePath();
            it.artist = pairs[k].section(QLatin1Char('|'), 0, 0);
            it.title = pairs[k].section(QLatin1Char('|'), 1);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.setItemVocal({ids.value(0)}, vocal::Group);
        db.setSetting("adb_enabled", "0");
        db.setSetting("mb_enabled", "1");
        db.setSetting("wiki_enabled", "1");
        db.setSetting("adb_mode", "2");     // kézi mód: az Infó ablak maga kéri az adatokat
        db.notifyItemsChanged();
        if (ArtistInfoService *info = window.artistInfo()) info->reload();
        window.resize(1500, 900);
        auto select = [&window](const QString &title) {
            auto *view = window.library()->findChild<QTableView *>();
            for (int r = 0; r < view->model()->rowCount(); ++r)
                if (view->model()->index(r, 0).data().toString() == title) {
                    view->setCurrentIndex(view->model()->index(r, 0));
                    view->selectRow(r);
                }
            QApplication::processEvents();
        };
        auto grabInfo = [&window, dir](const QString &name) {
            QApplication::processEvents();
            InfoWindow *w = window.infoWindow(false);
            if (!w) return;
            w->grab().save(dir + "/" + name + ".png");
            std::printf("%-10s elem=%d állapot=%s cím=%s\n", qPrintable(name), w->shownItemId(), qPrintable(w->stateName()),
                        qPrintable(w->windowTitle()));
        };
        QTimer::singleShot(400, &window, [&window, ids, select, grabInfo] {
            window.appendItems({ids.value(0)}, 0);
            if (auto *b = window.library()->findChild<QPushButton *>("infoBtn")) b->click();
            InfoWindow *w = window.infoWindow(false);
            std::printf("ablak: %s, gomb aktív: %d\n", w && w->isVisible() ? "nyitva" : "zárva",
                        window.library()->findChild<QPushButton *>("infoBtn")->property("active").toBool());
            w->resize(420, 760);
            select(QStringLiteral("Girls on Film"));
            grabInfo(QStringLiteral("1_loading"));
        });
        QTimer::singleShot(7000, &window, [&window, dir, select, grabInfo] {
            grabInfo(QStringLiteral("2_data"));
            window.grab().save(dir + "/main.png");
            select(QStringLiteral("Valami"));
            QTimer::singleShot(4000, &window, [&window, dir, select, grabInfo] {
                grabInfo(QStringLiteral("3_notfound"));
                InfoWindow *w = window.infoWindow(false);
                if (auto *b = w->findChild<QToolButton *>("infoOnAir")) b->click();
                grabInfo(QStringLiteral("4_onair"));
                if (auto *b = w->findChild<QToolButton *>("infoSelected")) b->click();
                window.artistInfo()->setSettings([&] {
                    ArtistInfoService::Settings s = window.artistInfo()->settings();
                    s.mbEnabled = false;
                    s.enabled = false;
                    return s;
                }());
                select(QStringLiteral("Hello"));
                grabInfo(QStringLiteral("5_off"));
                select(QStringLiteral("Girls on Film"));
                grabInfo(QStringLiteral("6_data_off"));
                if (auto *c = w->findChild<QCheckBox *>("infoOnTop")) c->setChecked(true);
                std::printf("mindig felül: %d, látható: %d\n", int(w->windowFlags().testFlag(Qt::WindowStaysOnTopHint)), int(w->isVisible()));
                w->close();
                std::printf("bezárva: gomb aktív=%d, nyitva mentve=%d\n",
                            window.library()->findChild<QPushButton *>("infoBtn")->property("active").toBool(),
                            QSettings().value("ui/info/open").toBool());
                QApplication::exit(0);
            });
        });
    }
    if (qEnvironmentVariableIsSet("ETER_ADB_TEST")) {
        // Fejlesztéshez: előadói adatok (TheAudioDB-utánzat: ETER_ADB_URL). ETER_ADB_TEST = a képek mappája;
        // ETER_ADB_KEY, ETER_ADB_MODE (0 automatikus, 1 betöltéskor, 2 kézzel), ETER_ADB_WAIT mp.
        const QString dir = qEnvironmentVariable("ETER_ADB_TEST");
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QStringList names{QStringLiteral("Coldplay"), QStringLiteral("Adele"), QStringLiteral("Simon & Garfunkel"),
                          QStringLiteral("David Guetta feat. Sia"), QStringLiteral("Ismeretlen Zenekar")};
        QStringList titles;
        if (qEnvironmentVariableIsSet("ETER_ADB_ITEMS")) {
            // „Előadó|Cím;Előadó|Cím…”
            names.clear();
            for (const QString &pair : qEnvironmentVariable("ETER_ADB_ITEMS").split(QLatin1Char(';'))) {
                names << pair.section(QLatin1Char('|'), 0, 0);
                titles << pair.section(QLatin1Char('|'), 1);
            }
        }
        const QStringList fs = parser.positionalArguments();
        QVector<int> ids;
        for (int k = 0; k < names.size() && !fs.isEmpty(); ++k) {
            eter::Decoder dec;
            if (!dec.open(fs[k % fs.size()].toStdString())) continue;
            Item it;
            it.categoryId = music;
            if (k >= fs.size()) break;
            it.path = QFileInfo(fs[k]).absoluteFilePath();
            it.title = k < titles.size() ? titles[k] : k == 0 ? QStringLiteral("Yellow") : QStringLiteral("Dal %1").arg(k + 1);
            it.artist = names[k];
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.setItemVocal({ids.value(1)}, vocal::Male);   // kézzel beállított ének: nem írhatja felül (Adele „Female” lenne)
        db.setSetting("adb_enabled", qEnvironmentVariable("ETER_ADB_ON", "1"));
        db.setSetting("mb_enabled", qEnvironmentVariable("ETER_MB_ON", "0"));
        db.setSetting("adb_key", qEnvironmentVariable("ETER_ADB_KEY", "123"));
        db.setSetting("adb_mode", qEnvironmentVariable("ETER_ADB_MODE", "0"));
        db.notifyItemsChanged();
        if (ArtistInfoService *info = window.artistInfo()) info->reload();
        window.resize(1500, 900);
        QTimer::singleShot(300, &window, [&window, ids] {
            window.appendItems({ids.value(0)}, 0);
            if (qEnvironmentVariable("ETER_ADB_MODE") == QLatin1String("2"))
                window.artistInfo()->requestItems(ids, true);
        });
        const int wait = qEnvironmentVariableIsSet("ETER_ADB_WAIT") ? qEnvironmentVariableIntValue("ETER_ADB_WAIT") : 14;
        QTimer::singleShot(wait * 1000, &window, [&window, &db, dir] {
            for (const Item &it : db.items()) {
                QSqlQuery q;
                q.prepare("SELECT album, COALESCE(year, 0) FROM items WHERE id = ?");
                q.addBindValue(it.id);
                q.exec();
                q.next();
                const ArtistData d = window.artistInfo()->artist(it.artist);
                std::printf("%-24s ének=%-10s album=%-11s év=%-4d  adat: %s%s%s\n", qPrintable(it.artist), qPrintable(vocal::name(it.vocal)),
                            qPrintable(q.value(0).toString()), q.value(1).toInt(), !d.known ? "nincs lekérve" : d.found ? "megvan" : "nem található",
                            d.found ? qPrintable(QStringLiteral(" · ") + d.facts()) : "", d.imageFile.isEmpty() ? "" : " · fotó");
            }
            std::printf("állapot: %s\n", qPrintable(window.artistInfo()->statusText()));
            window.grab().save(dir + "/main.png");
            QToolButton *b = nullptr;
            for (QToolButton *t : window.findChildren<QToolButton *>("bioButton"))
                if (t->isVisible()) b = t;
            if (b) {
                b->click();
                QApplication::processEvents();
                const auto pops = window.findChildren<QFrame *>("bioPopup");
                std::printf("életrajz ablak: %d\n", int(pops.size()));
                for (QFrame *w : pops) {
                    w->grab().save(dir + "/bio.png");
                    w->close();
                }
            }
            auto *dlg = new SettingsDialog(&window);
            dlg->resize(860, 700);
            dlg->show();
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("Előadói adatok")) tabs->setCurrentIndex(i);
            QApplication::processEvents();
            dlg->grab().save(dir + "/settings.png");
            dlg->close();
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_SQL_TEST")) {
        // Fejlesztéshez: külső SQL szerver. ETER_SQL_TEST = a képek mappája. ETER_SQL_FILL=1: hangtár a fájlokból;
        // ETER_SQL_SETUP="host:port:user:pass:db": a Beállítások SQL oldalán kitölti és feltölti; ETER_SQL_EDIT=szöveg:
        // az első elem címe erre változik; ETER_SQL_WAIT: ennyi mp után kilép.
        const QString dir = qEnvironmentVariable("ETER_SQL_TEST");
        if (qEnvironmentVariableIsSet("ETER_SQL_FILL")) {
            const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
            int n = 0;
            for (const QString &f : parser.positionalArguments()) {
                eter::Decoder dec;
                if (!dec.open(f.toStdString())) continue;
                Item it;
                it.categoryId = music;
                it.path = QFileInfo(f).absoluteFilePath();
                it.title = QStringLiteral("Dal %1").arg(++n);
                it.artist = QStringLiteral("Előadó %1").arg(n);
                it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
                db.addItem(it);
            }
            db.setSetting("teszt_kulcs", QStringLiteral("árvíztűrő tükörfúrógép"));
        }
        auto dump = [&db](const char *when) {
            QStringList t;
            for (const Item &it : db.items()) t << it.title;
            std::printf("[%s] helyi hangtár: %s · teszt_kulcs=%s\n", when, qPrintable(t.join(", ")), qPrintable(db.setting("teszt_kulcs")));
        };
        dump("indulás");
        window.resize(1400, 860);
        QTimer::singleShot(600, &window, [&window, &db, dir, dump] {
            if (qEnvironmentVariableIsSet("ETER_SQL_SETUP")) {
                const QStringList p = qEnvironmentVariable("ETER_SQL_SETUP").split(QLatin1Char(':'));
                auto *dlg = new SettingsDialog(&window);
                dlg->resize(860, 820);
                dlg->show();
                if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == TR("SQL szerver")) tabs->setCurrentIndex(i);
                const char *names[] = {"sqlHost", "sqlPort", "sqlUser", "sqlPassword", "sqlDatabase"};
                for (int i = 0; i < 5 && i < p.size(); ++i) {
                    if (auto *e = dlg->findChild<QLineEdit *>(names[i])) {
                        e->setText(p[i]);
                        emit e->editingFinished();
                    } else if (auto *sp = dlg->findChild<QSpinBox *>(names[i])) {
                        sp->setValue(p[i].toInt());
                        emit sp->editingFinished();
                    }
                }
                if (auto *b = dlg->findChild<QPushButton *>("sqlTest")) b->click();
                if (auto *r = dlg->findChild<QLabel *>("sqlTestResult")) std::printf("teszt: %s\n", qPrintable(r->text()));
                if (auto *b = dlg->findChild<QPushButton *>("sqlUpload")) b->click();
                if (auto *r = dlg->findChild<QLabel *>("sqlTestResult")) std::printf("feltöltés: %s\n", qPrintable(r->text()));
                QTimer::singleShot(1500, dlg, [dlg, dir] {
                    dlg->grab().save(dir + "/settings_sql.png");
                    if (auto *st = dlg->findChild<QLabel *>("sqlState")) std::printf("állapot: %s\n", qPrintable(st->text()));
                    dlg->close();
                });
            }
            if (qEnvironmentVariableIsSet("ETER_SQL_CLICK")) {
                // A Beállítások SQL oldalának egy gombja (sqlDownload, sqlDisconnect, …).
                QTimer::singleShot(1500, &window, [&window] {
                    auto *dlg = new SettingsDialog(&window);
                    dlg->show();
                    if (auto *b = dlg->findChild<QPushButton *>(qEnvironmentVariable("ETER_SQL_CLICK"))) b->click();
                    if (auto *r = dlg->findChild<QLabel *>("sqlTestResult")) std::printf("gomb után: %s\n", qPrintable(r->text()));
                    if (auto *st = dlg->findChild<QLabel *>("sqlState")) std::printf("állapot: %s\n", qPrintable(st->text()));
                    dlg->close();
                });
            }
            if (qEnvironmentVariableIsSet("ETER_SQL_EDIT")) {
                QTimer::singleShot(2500, &window, [&db] {
                    QSqlQuery q;
                    q.prepare("UPDATE items SET title = ? WHERE id = (SELECT MIN(id) FROM items)");
                    q.addBindValue(qEnvironmentVariable("ETER_SQL_EDIT"));
                    q.exec();
                    db.setSetting("teszt_kulcs", qEnvironmentVariable("ETER_SQL_EDIT") + QStringLiteral(" beállítás"));
                    db.notifyItemsChanged();
                    std::printf("módosítva: az első elem címe „%s”\n", qPrintable(qEnvironmentVariable("ETER_SQL_EDIT")));
                });
            }
            const int wait = qEnvironmentVariableIsSet("ETER_SQL_WAIT") ? qEnvironmentVariableIntValue("ETER_SQL_WAIT") : 9;
            QTimer::singleShot(wait * 1000, &window, [&window, dump, dir] {
                dump("vége");
                if (ServerSync *s = window.serverSync()) std::printf("szinkron: %s\n", qPrintable(s->statusText()));
                window.grab().save(dir + "/main.png");
                QApplication::exit(0);
            });
        });
    }
    if (qEnvironmentVariableIsSet("ETER_MIDI_TEST")) {
        // Fejlesztéshez: MIDI-vezérlés egy FIFO-n át (ETER_MIDI_DEVICE = a FIFO útvonala): tanítás és végrehajtás.
        const QString dir = qEnvironmentVariable("ETER_MIDI_TEST");
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        int k = 0;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QStringLiteral("Dal %1").arg(++k);
            it.artist = QStringLiteral("Előadó");
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.resize(1500, 900);
        QTimer::singleShot(500, &window, [&window, ids] { window.appendItems(ids); });
        QTimer::singleShot(1500, &window, [&window, dir] {
            auto *dlg = new SettingsDialog(&window);
            dlg->resize(900, 900);
            dlg->show();
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("MIDI")) tabs->setCurrentIndex(i);
            MidiControl *mc = window.midiControl();
            auto *dev = dlg->findChild<QComboBox *>("midiDevice");
            QStringList names;
            for (int i = 0; i < dev->count(); ++i) names << dev->itemText(i);
            std::printf("eszközök: %s\n", qPrintable(names.join(" | ")));
            dev->setCurrentIndex(dev->count() - 1);
            emit dev->activated(dev->count() - 1);
            QApplication::processEvents();
            std::printf("állapot: %s\n", qPrintable(mc->statusText()));
            auto fifo = std::make_shared<QFile>(qEnvironmentVariable("ETER_MIDI_DEVICE"));
            if (!fifo->open(QIODevice::WriteOnly | QIODevice::Unbuffered)) std::printf("FIFO nem nyitható\n");
            auto send = [fifo](std::initializer_list<int> bytes) {
                QByteArray b;
                for (int x : bytes) b.append(char(x));
                fifo->write(b);
                fifo->flush();
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < 250) QApplication::processEvents(QEventLoop::AllEvents, 20);
            };
            auto learn = [dlg](const char *action) {
                if (auto *b = dlg->findChild<QPushButton *>(QStringLiteral("midiLearn_") + QLatin1String(action))) b->click();
                QApplication::processEvents();
            };
            auto mon = [dlg](const char *what) {
                std::printf("%-28s %s | %s\n", what, qPrintable(dlg->findChild<QLabel *>("mono") ? dlg->findChild<QLabel *>("midiStatus")->text() : QString()),
                            qPrintable(dlg->findChild<QLabel *>("midiLearnNote")->text()));
            };
            learn("play_a");
            mon("tanítás indul");
            dlg->grab().save(dir + "/learning.png");
            send({0x99, 36, 100, 0x89, 36, 0});
            mon("A: hang 36");
            learn("vol_out");
            send({0xB0, 7, 64});
            mon("KI hangerő: CC 7");
            learn("fstart_b");
            send({0xB0, 8, 0});
            mon("B fader-indítás: CC 8");
            learn("vol_b");
            send({0xB0, 8, 0});
            mon("B hangerő: CC 8");
            learn("play_c");
            send({0xB0, 20, 127, 0xB0, 20, 0});
            mon("C: CC-gomb 20");
            learn("next");
            send({0x99, 36, 100});
            mon("Következő: hang 36 (A-tól)");
            learn("play_a");
            send({0x99, 37, 90});
            mon("A: hang 37");
            learn("vol_a");
            send({0x90, 40, 100});
            mon("hangerő hanggal (nem fogadja)");
            learn("vol_a");     // megszakítás
            std::printf("tanítás: '%s'\n", qPrintable(mc->learning()));
            QStringList b;
            for (const MidiControl::Action &a : MidiControl::actions())
                if (!mc->binding(a.id).isEmpty()) b << a.id + "=" + mc->binding(a.id);
            std::printf("hozzárendelések: %s\n", qPrintable(b.join(", ")));
            dlg->grab().save(dir + "/bindings.png");
            // Végrehajtás
            auto st = [&window](const char *what) {
                std::printf("%-30s A=%s B=%s C=%s · KI=%.1f dB B-fader=%.1f dB · monitor: %s\n", what,
                            window.playerWidget(0)->isPlaying() ? "szól" : "áll", window.playerWidget(1)->isPlaying() ? "szól" : "áll",
                            window.playerWidget(2)->isPlaying() ? "szól" : "áll", window.mixer()->db(5), window.mixer()->db(1),
                            "");
            };
            st("kezdet");
            send({0x99, 37, 100});
            st("hang 37 → A indul");
            send({0x89, 37, 0, 0x99, 37, 100});
            st("hang 37 újra → A leáll");
            send({0xB0, 7, 127});
            st("CC 7 = 127 → KI 0 dB");
            send({0xB0, 7, 0});
            st("CC 7 = 0 → KI −∞");
            send({0xB0, 7, 100, 90});       // futó státusz: a második érték 90
            st("CC 7 = 100, 90 (futó státusz)");
            send({0xB0, 8, 100});
            st("CC 8 fel → B indul + hangerő");
            send({0xB0, 8, 1});
            st("CC 8 le → B leáll");
            send({0xB0, 20, 127});
            st("CC 20 → C indul");
            send({0xB0, 20, 0, 0xF8, 0xF0, 1, 2, 3, 0xF7, 0xB0, 20, 127});
            st("SysEx/órajel között CC 20 → C leáll");
            std::printf("monitor: %s\n", qPrintable(dlg->findChild<QLabel *>("mono")->text()));
            fifo->close();
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_MIXPV_SHOT")) {
        // Fejlesztéshez: az automatikus mix előnézete minden példa átmenettel, és egy-két érték változtatása.
        const QString dir = qEnvironmentVariable("ETER_MIXPV_SHOT");
        window.resize(1500, 900);
        QTimer::singleShot(600, &window, [&window, dir] {
            auto *dlg = new SettingsDialog(&window);
            dlg->resize(900, 980);
            dlg->show();
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("Automatikus mix")) tabs->setCurrentIndex(i);
            QApplication::processEvents();
            auto *combo = dlg->findChild<QComboBox *>("mixPreviewScenario");
            auto *pv = dlg->findChild<MixPreview *>("mixPreview");
            for (int i = 0; i < combo->count(); ++i) {
                combo->setCurrentIndex(i);
                QApplication::processEvents();
                pv->grab().save(QStringLiteral("%1/pv%2.png").arg(dir).arg(i));
                std::printf("%d %s\n   %s\n", i, qPrintable(combo->itemText(i)), qPrintable(pv->summary()));
            }
            combo->setCurrentIndex(0);
            QApplication::processEvents();
            dlg->grab().save(dir + "/page.png");
            // Leghosszabb átfedés 2 mp-re: az előnézet azonnal követi.
            for (auto *sp : dlg->findChildren<QDoubleSpinBox *>())
                if (sp->toolTip() == TR("Semmilyen esetben nem szól tovább együtt a két szám")) sp->setValue(2.0);
            QApplication::processEvents();
            pv->grab().save(dir + "/pv_max2.png");
            std::printf("max 2 mp: %s\n", qPrintable(pv->summary()));
            if (auto *c = dlg->findChild<QCheckBox *>("smartMixOn")) c->setChecked(false);
            QApplication::processEvents();
            pv->grab().save(dir + "/pv_simple.png");
            std::printf("egyszerű: %s\n", qPrintable(pv->summary()));
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_THEME_SHOT")) {
        // Fejlesztéshez: a Megjelenés oldal és a főablak saját színekkel (ETER_THEME_ACCENT, ETER_THEME_A).
        const QString dir = qEnvironmentVariable("ETER_THEME_SHOT");
        std::printf("QT_SCALE_FACTOR=%s, méret %d %%\n", qgetenv("QT_SCALE_FACTOR").constData(), theme::uiScalePercent());
        window.resize(1500, 900);
        QTimer::singleShot(700, &window, [&window, dir] {
            if (qEnvironmentVariableIsSet("ETER_THEME_ACCENT")) {
                QSettings().setValue("ui/accent", qEnvironmentVariable("ETER_THEME_ACCENT"));
                QSettings().setValue("ui/player0", qEnvironmentVariable("ETER_THEME_A", "#f472b6"));
                theme::reapply();
            }
            QApplication::processEvents();
            window.grab().save(dir + "/main.png");
            auto *dlg = new SettingsDialog(&window);
            dlg->resize(860, 900);
            dlg->show();
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("Megjelenés")) tabs->setCurrentIndex(i);
            QApplication::processEvents();
            dlg->grab().save(dir + "/settings_look.png");
            std::printf("kiemelő: %s, A: %s\n", qPrintable(theme::amber.name()), qPrintable(theme::playerColor(0).name()));
            if (!qEnvironmentVariableIsSet("ETER_THEME_EDIT")) {
                QApplication::exit(0);
                return;
            }
            // A témaszerkesztő végigjárása: séma választása, egy szín átírása, mentés név szerint, export, törlés, import.
            auto *combo = dlg->findChild<QComboBox *>("colorScheme");
            auto click = [dlg](const char *name) {
                if (auto *b = dlg->findChild<QPushButton *>(name)) {
                    std::printf("  [%s] %s\n", name, b->isEnabled() ? "kattintás" : "tiltva");
                    if (b->isEnabled()) b->click();
                }
                QApplication::processEvents();
            };
            auto state = [combo](const char *what) {
                QStringList items;
                for (int i = 0; i < combo->count(); ++i)
                    if (!combo->itemText(i).isEmpty()) items << combo->itemText(i);
                std::printf("%s: séma=%s (%s), módosítva=%d, háttér=%s, panel=%s | %s\n", what, qPrintable(theme::currentSchemeId()),
                            qPrintable(combo->currentText()), int(theme::currentModified()), qPrintable(theme::bg.name()),
                            qPrintable(theme::panel.name()), qPrintable(items.join(", ")));
            };
            state("kezdet");
            const int mid = combo->findData(QStringLiteral("builtin:midnight"));
            combo->setCurrentIndex(mid);
            emit combo->activated(mid);
            QApplication::processEvents();
            state("Éjkék");
            window.grab().save(dir + "/main_midnight.png");
            qputenv("ETER_PICK_COLOR", "#203050");
            click("color_panel");
            state("panel átírva");
            qputenv("ETER_SCHEME_NAME", "Stúdió éjjel");
            click("schemeSaveAs");
            state("mentve");
            dlg->grab().save(dir + "/settings_saved.png");
            qputenv("ETER_PICK_COLOR", "#e8eaed");
            click("color_bg");
            state("rossz kontraszt");
            if (auto *w = dlg->findChild<QLabel *>("contrastWarn")) std::printf("  figyelmeztetés: %s\n", qPrintable(w->text()));
            dlg->grab().save(dir + "/settings_contrast.png");
            click("schemeRevert");
            state("visszaállítva");
            const QString file = dir + "/studio.json";
            qputenv("ETER_SCHEME_FILE", file.toUtf8());
            click("schemeExport");
            QFile f(file);
            if (f.open(QIODevice::ReadOnly)) std::printf("%s\n", f.readAll().constData());
            qputenv("ETER_AUTOCONFIRM", "1");
            qputenv("ETER_SCHEME_NAME", "Éjszakai");
            click("schemeRename");
            state("átnevezve");
            click("schemeDelete");
            state("törölve");
            click("schemeImport");
            state("importálva");
            click("schemeImport");
            state("újra importálva (nincs másolat)");
            const QString bad = dir + "/bad.json";
            QFile bf(bad);
            if (bf.open(QIODevice::WriteOnly)) bf.write("{\"format\":\"valami\"}");
            bf.close();
            qputenv("ETER_SCHEME_FILE", bad.toUtf8());
            click("schemeImport");
            const int light = combo->findData(QStringLiteral("builtin:light"));
            combo->setCurrentIndex(light);
            emit combo->activated(light);
            QApplication::processEvents();
            state("Világos");
            dlg->grab().save(dir + "/settings_light.png");
            window.grab().save(dir + "/main_light.png");
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_V033_SHOT")) {
        // Fejlesztéshez (0.33): „Jegyzet” és sweeper az órasémában, heti rácsok időszakokkal, előadók szétválasztása.
        const QString dir = qEnvironmentVariable("ETER_V033_SHOT");
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Sweeper"), "jingle", "#f0a040");
        const QStringList fs = parser.positionalArguments();
        const QStringList artistsList{QStringLiteral("Alpha and Beta"), QStringLiteral("Beta"), QStringLiteral("Gamma & Delta"),
                                      QStringLiteral("Delta"), QStringLiteral("Epsilon feat. Alpha"), QStringLiteral("Zeta"),
                                      QStringLiteral("AC/DC"), QStringLiteral("Eta")};
        for (int k = 0; k < artistsList.size() + 2 && !fs.isEmpty(); ++k) {
            const QString f = fs[k % fs.size()];
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            const bool sw = k >= artistsList.size();
            it.categoryId = sw ? jingle : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = sw ? QStringLiteral("Sweeper %1").arg(k - artistsList.size() + 1) : QStringLiteral("Dal %1").arg(k + 1);
            it.artist = sw ? QString() : artistsList[k];
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            db.addItem(it);
        }
        for (const QString &a : artistsList)
            std::printf("előadó „%s” → %s\n", qPrintable(a), qPrintable(artists::split(a, artists::defaultSeparators()).join(" | ")));
        const int clockA = db.addClock(QStringLiteral("Reggel"), "#5cb0ff");
        const int clockB = db.addClock(QStringLiteral("Nyár"), "#f5a524");
        auto slot = [&db](int clock, const QString &type, int cat, int count, const QString &note = {}, int sweeper = 0) {
            ClockSlot s;
            s.clockId = clock;
            s.type = type;
            s.categoryId = cat;
            s.count = count;
            s.note = note;
            s.sweeperCategoryId = sweeper;
            s.sweeperDuckDb = -8;
            db.addSlot(s);
        };
        slot(clockA, "note", 0, 1, QStringLiteral("Időjárás élőben a stúdióból"));
        slot(clockA, "category", music, 3, {}, jingle);
        slot(clockA, "note", 0, 1, QStringLiteral("Nyereményjáték: a 3. telefonáló nyer"));
        slot(clockA, "category", music, 2, {}, jingle);
        slot(clockB, "note", 0, 1, QStringLiteral("Nyári műsor"));
        slot(clockB, "category", music, 2);
        for (int d = 0; d < 7; ++d)
            for (int h = 0; h < 24; ++h) db.setGrid(1, d, h, clockA);
        const int summer = db.addWeekGrid(QStringLiteral("Nyári"), 1);
        for (int d = 0; d < 7; ++d)
            for (int h = 0; h < 24; ++h) db.setGrid(summer, d, h, clockB);
        const QDate today = QDate::currentDate();
        std::printf("rácsok: %d; ma érvényes: %d; séma most: %d\n", int(db.weekGrids().size()), db.gridFor(today),
                    db.clockAt(today, 10));
        WeekGridPeriod p;
        p.gridId = summer;
        p.from = today.addDays(3);
        p.to = today.addDays(10);
        const int pid = db.addWeekGridPeriod(p);
        std::printf("időszak (+3…+10 nap): ma %d, +5 napon %d, +11 napon %d\n", db.gridFor(today), db.gridFor(today.addDays(5)),
                    db.gridFor(today.addDays(11)));
        WeekGridPeriod y;
        y.gridId = summer;
        y.from = QDate(2000, 12, 20);
        y.to = QDate(2000, 1, 6);
        y.yearly = true;
        std::printf("évente dec. 20 – jan. 6: dec. 25 %d, jan. 3 %d, márc. 1 %d\n", y.contains(QDate(2027, 12, 25)),
                    y.contains(QDate(2028, 1, 3)), y.contains(QDate(2028, 3, 1)));
        db.setActiveWeekGrid(summer);
        std::printf("kézi váltás után: ma %d, +5 napon %d\n", db.gridFor(today), db.gridFor(today.addDays(5)));
        db.setActiveWeekGrid(1);
        db.deleteWeekGridPeriod(pid);
        WeekGridPeriod later = p;
        later.from = today.addDays(30);
        later.to = today.addDays(60);
        db.addWeekGridPeriod(later);
        db.setSetting("generate_ahead_hours", "2");
        db.setSetting("artist_separation_min", "0");
        db.notifyItemsChanged();
        window.schedulerPage()->refreshAll();
        window.resize(1500, 900);
        QTimer::singleShot(800, &window, [&window, &db, dir] {
            window.schedulerPage()->generateNow();
            std::printf("generálás: %s\n", qPrintable(window.schedulerPage()->generateInfo()));
            for (const Entry &e : db.pendingEntries())
                std::printf("  %s%-12s %-18s%s%s\n", e.overlay ? "  ↝ " : "", qPrintable(e.item.title), qPrintable(e.item.artist),
                            e.overlay ? qPrintable(QStringLiteral(" (ráfektetve, %1 dB)").arg(e.overlayDuckDb)) : "",
                            e.note.isEmpty() ? "" : qPrintable(QStringLiteral("   [jegyzet: %1]").arg(e.note)));
            if (qEnvironmentVariableIsSet("ETER_GENDLG_SHOT")) {
                // A generálás párbeszéde: holnap 6–9 óra (a párbeszéd maga tölti ki és fogadja el).
                for (auto *b : window.schedulerPage()->findChildren<QPushButton *>())
                    if (b->text() == TR("Playlistek generálása")) { b->click(); break; }
                std::printf("párbeszéd után: %s\n", qPrintable(window.schedulerPage()->generateInfo()));
                QMap<int, int> perHour;
                for (const Entry &e : db.pendingEntries()) ++perHour[e.playlistKey];
                for (auto it = perHour.begin(); it != perHour.end(); ++it) std::printf("  óra %d: %d sor\n", it.key(), it.value());
            }
        });
        QTimer::singleShot(1600, &window, [&window, dir] {
            window.grab().save(dir + "/playlist_notes.png");
            if (qEnvironmentVariableIsSet("ETER_GRAB_TABS")) {
                // Fejlesztéshez: minden fül képe (a színsémák ellenőrzéséhez).
                if (auto *row = window.findChild<QWidget *>("tabRow")) {
                    int k = 0;
                    for (auto *b : row->findChildren<QToolButton *>()) {
                        if (!b->isVisible() || !b->isCheckable()) continue;
                        b->click();
                        QApplication::processEvents();
                        QWidget *top = &window;
                        for (QWidget *t : QApplication::topLevelWidgets())
                            if (t != &window && t->isVisible() && t->isWindow() && t->width() > 400) top = t;
                        top->grab().save(QStringLiteral("%1/tab%2.png").arg(dir).arg(++k));
                        if (top != &window) top->hide();
                    }
                    for (auto *b : row->findChildren<QToolButton *>())
                        if (b->isCheckable() && b->isVisible()) { b->click(); break; }
                    QApplication::processEvents();
                }
            }
            for (auto *b : window.findChildren<QToolButton *>())
                if (b->text() == TR("Ütemező") && b->isVisible()) { b->click(); break; }
            QApplication::processEvents();
            auto *sched = window.schedulerPage();
            for (auto *c : sched->findChildren<QComboBox *>())
                if (const int i = c->findText(QStringLiteral("Reggel")); i >= 0 && c->objectName() != QLatin1String("weekGridSelect")) {
                    c->setCurrentIndex(i);
                    emit c->activated(i);
                    break;
                }
            QApplication::processEvents();
            QTableWidget *slotsT = nullptr;
            for (auto *t : sched->findChildren<QTableWidget *>())
                if (t->columnCount() == 5) slotsT = t;
            if (slotsT) {
                slotsT->selectRow(1);
                QApplication::processEvents();
            }
            sched->window()->grab().save(dir + "/scheduler_sweeper.png");
            if (slotsT) {
                slotsT->selectRow(0);
                QApplication::processEvents();
            }
            sched->window()->grab().save(dir + "/scheduler_note.png");
            if (auto *sel = sched->findChild<QComboBox *>("weekGridSelect")) {
                QStringList names;
                for (int i = 0; i < sel->count(); ++i) names << sel->itemText(i);
                std::printf("rácsválasztó: %s\n", qPrintable(names.join(" | ")));
            }
            qputenv("ETER_PERIODS_SHOT", (dir + "/periods.png").toUtf8());
            if (auto *b = sched->findChild<QPushButton *>("weekGridPeriods")) b->click();
            QTimer::singleShot(300, qApp, [] { QApplication::exit(0); });
        });
    }
    if (qEnvironmentVariableIsSet("ETER_UI32_SHOT")) {
        // Fejlesztéshez (0.32): a jobb oldali nézetváltó (hangtár / lejátszott / cartwall), a Lapok beállítás, az oszlopok
        // szélessége (ETER_UI32_PHASE=2: a mentett szélességek visszatöltése) és az órasema sorainak húzása.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Jingle"), "jingle", "#f0a040");
        int n = 0;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = n % 3 == 2 ? jingle : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QStringLiteral("Dal %1").arg(++n);
            it.artist = QStringLiteral("Előadó %1").arg(n);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            db.addItem(it);
        }
        const int clock = db.addClock(QStringLiteral("Reggel"), "#5cb0ff");
        for (int k = 0; k < 4; ++k) {
            ClockSlot sl;
            sl.clockId = clock;
            sl.categoryId = k == 1 ? jingle : music;
            sl.count = k + 1;
            db.addSlot(sl);
        }
        db.notifyItemsChanged();
        window.resize(1500, 900);
        QTimer::singleShot(900, &window, [&window, &db, clock] {
            const QString dir = qEnvironmentVariable("ETER_UI32_SHOT");
            const bool phase2 = qEnvironmentVariable("ETER_UI32_PHASE") == QLatin1String("2");
            auto *lib = window.library();
            auto *view = lib->findChild<QTableView *>();
            auto widths = [view] {
                QStringList w;
                for (int c = 0; c < view->horizontalHeader()->count(); ++c)
                    w << (view->isColumnHidden(c) ? QStringLiteral("-") : QString::number(view->columnWidth(c)));
                return w.join(' ');
            };
            std::printf("hangtár oszlopok: %s (nézet %d px)\n", qPrintable(widths()), view->viewport()->width());
            if (phase2) {
                std::printf("mentve: %s\n", qPrintable(QSettings().value("ui/columns/library").toStringList().join(' ')));
                QApplication::exit(0);
                return;
            }
            window.grab().save(dir + "/main_library.png");
            if (auto *mode = lib->findChild<QComboBox *>("libraryView")) {
                QStringList items;
                for (int i = 0; i < mode->count(); ++i) items << mode->itemText(i);
                std::printf("nézetváltó: %s\n", qPrintable(items.join(" | ")));
                mode->setCurrentIndex(2);
                emit mode->activated(2);
            }
            QApplication::processEvents();
            window.grab().save(dir + "/main_cartwall.png");
            std::printf("right_view = %s\n", qPrintable(db.setting("right_view")));
            if (auto *cv = window.findChild<QComboBox *>("cartView")) {
                cv->setCurrentIndex(0);
                emit cv->activated(0);
            }
            QApplication::processEvents();
            // Oszlop húzása: a Kategória (2) szélesebb lesz, a Cím és az Előadó osztozik a maradékon.
            view->horizontalHeader()->resizeSection(2, 260);
            QApplication::processEvents();
            std::printf("húzás után:  %s\n", qPrintable(widths()));
            window.resize(1200, 900);
            QApplication::processEvents();
            std::printf("kisebb ablak: %s (nézet %d px)\n", qPrintable(widths()), view->viewport()->width());
            window.grab().save(dir + "/main_columns.png");
            // Lapok elrejtése: Mix és Mixdown.
            MainWindow::setHiddenTabs({QStringLiteral("mix"), QStringLiteral("mixdown")});
            window.applyTabVisibility();
            QApplication::processEvents();
            window.grab().save(dir + "/main_hidden_tabs.png");
            auto *dlg = new SettingsDialog(&window);
            dlg->resize(820, 760);
            dlg->show();
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("Lapok")) tabs->setCurrentIndex(i);
            QApplication::processEvents();
            dlg->grab().save(dir + "/settings_tabs.png");
            if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == TR("Automatikus mix")) tabs->setCurrentIndex(i);
            QApplication::processEvents();
            if (auto *cue = dlg->findChild<QDoubleSpinBox *>("autoCueThreshold")) {
                std::printf("kezdőpont küszöbe: %.0f dB\n", cue->value());
                for (QWidget *w = cue->parentWidget(); w; w = w->parentWidget())
                    if (auto *sa = qobject_cast<QScrollArea *>(w)) { sa->ensureWidgetVisible(cue, 50, 200); break; }
            }
            QApplication::processEvents();
            dlg->grab().save(dir + "/settings_cue.png");
            dlg->close();
            // Az óraséma: a 4. sor húzása az 1. elé.
            MainWindow::setHiddenTabs({});
            window.applyTabVisibility();
            for (auto *b : window.findChildren<QToolButton *>())
                if (b->text() == TR("Ütemező") && b->isVisible()) { b->click(); break; }
            QApplication::processEvents();
            auto *sched = window.schedulerPage();
            sched->refreshAll();
            QApplication::processEvents();
            auto *slotsT = sched->findChildren<QTableWidget *>().value(1);
            for (auto *t : sched->findChildren<QTableWidget *>())
                if (t->columnCount() == 5) slotsT = t;
            auto order = [&db, clock] {
                QStringList o;
                for (const ClockSlot &s : db.clockSlots(clock)) o << QStringLiteral("%1×%2").arg(s.count).arg(s.label);
                return o.join(", ");
            };
            std::printf("sorrend előtte: %s (táblában %d sor)\n", qPrintable(order()), slotsT->rowCount());
            slotsT->selectRow(3);
            QMimeData mime;
            mime.setData(QStringLiteral("application/x-qabstractitemmodeldatalist"), QByteArray("x"));
            const QRect r0 = slotsT->visualRect(slotsT->model()->index(0, 1));
            const QPointF at(r0.center().x(), r0.top() + 3);
            QDragEnterEvent enter(at.toPoint(), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(slotsT->viewport(), &enter);
            std::printf("húzás belépés elfogadva: %d\n", enter.isAccepted());
            QDropEvent drop(at, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(slotsT->viewport(), &drop);
            QApplication::processEvents();
            QTimer::singleShot(100, &window, [order, slotsT, dir] {
                std::printf("sorrend utána:  %s (kijelölt sor %d)\n", qPrintable(order()), slotsT->currentRow());
                slotsT->window()->grab().save(dir + "/scheduler_drag.png");
                QTimer::singleShot(700, qApp, [] { QApplication::exit(0); });
            });
        });
    }
    if (qEnvironmentVariableIsSet("ETER_LIBLOG_SHOT")) {
        // Fejlesztéshez: a hangtár (a ma adásba ment dalok piros háttérrel) és a „Lejátszott elemek” lista képe.
        // A fájlok zeneként kerülnek a hangtárba; az 1., 3. és 4. „szólt” (adásnapló-bejegyzés), a 2. tegnap.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        int n = 0;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QStringLiteral("Dal %1").arg(++n);
            it.artist = QStringLiteral("Előadó %1").arg(n);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        for (int k : {0, 2, 3}) {
            if (k >= ids.size()) continue;
            const int log = db.logStart(db.item(ids[k]), 0, 1, "auto");
            if (k != 3) db.logEnd(log, "mixed");
        }
        if (ids.size() > 1) {
            QSqlQuery q;
            q.exec(QStringLiteral("UPDATE items SET last_played_at = %1 WHERE id = %2")
                       .arg(QDateTime::currentSecsSinceEpoch() - 86400 - 3600).arg(ids[1]));
        }
        db.notifyItemsChanged();
        QTimer::singleShot(800, &window, [&window] {
            const QString dir = qEnvironmentVariable("ETER_LIBLOG_SHOT");
            LibraryPanel *lib = window.library();
            lib->grab().save(dir + "/library.png");
            if (auto *mode = lib->findChild<QComboBox *>("libraryView")) {
                mode->setCurrentIndex(1);
                emit mode->activated(1);
            }
            QApplication::processEvents();
            lib->grab().save(dir + "/playlog.png");
            if (auto *t = lib->findChild<QTableWidget *>("playLog"))
                for (int r = 0; r < t->rowCount(); ++r)
                    std::printf("napló %d: %s | %s | %s\n", r, qPrintable(t->item(r, 0)->text()), qPrintable(t->item(r, 1)->text()),
                                qPrintable(t->item(r, 4)->text()));
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_CART_SHOT")) {
        // Fejlesztéshez: képek a cartwallról (lejátszás közben a körbefutó mutató) és a mini mixerről (az adásban lévő
        // lejátszó betűjele). Az első fájl szól a playlistben; a többi a cartwall 1., 2., … gombjára kerül, és 0,6 mp-es
        // eltolással indulnak. ETER_CART_SHOT = a képek mappája.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const QStringList fs = parser.positionalArguments();
        QVector<int> ids;
        for (const QString &f : fs.mid(0, 1)) {
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
        window.appendItems(ids);
        CartwallPanel *cw = window.cartwall();
        for (int i = 1; i < fs.size(); ++i) cw->assignPaths(i - 1, {QFileInfo(fs[i]).absoluteFilePath()});
        if (auto *stack = qobject_cast<QStackedWidget *>(cw->parentWidget())) stack->setCurrentWidget(cw);
        QTimer::singleShot(1000, &window, [&window] {
            window.setMode(MainWindow::Auto);
            window.next();
        });
        for (int i = 1; i < fs.size(); ++i)
            QTimer::singleShot(1200 + (i - 1) * 600, &window, [cw, i] { cw->trigger(i - 1); });
        QTimer::singleShot(1200 + (fs.size() - 1) * 600 + 400, &window, [&window, cw] {
            const QString dir = qEnvironmentVariable("ETER_CART_SHOT");
            cw->grab().save(dir + "/cartwall.png");
            window.mixer()->grab().save(dir + "/mixer.png");
            window.grab().save(dir + "/main.png");
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_MIXER_TEST")) {
        // Fejlesztéshez: a mini mixer. Az első fájl szól; a szóló lejátszó csúcsszintje 0 dB, −12 dB és némítva,
        // majd a fő kimenet −6 dB-en; a végén kép a főablakról (ETER_MIXER_TEST = a kép útvonala).
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        for (const QString &f : parser.positionalArguments()) {
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
        window.appendItems(ids);
        ChannelMixer *mx = window.mixer();
        mx->confirmMute = nullptr;   // a tesztben nincs kérdés
        mx->resetAll();
        QTimer::singleShot(1500, &window, [&window] {
            window.setMode(MainWindow::Auto);
            window.next();
        });
        auto steps = std::make_shared<int>(0);
        auto *t = new QTimer(&window);
        QObject::connect(t, &QTimer::timeout, &window, [&window, mx, steps, t] {
            auto &src = window.engine()->source(eter::Engine::Player1);
            float l = 0, r = 0;
            src.takePeaks(l, r);   // (a mixer is ebből olvas; a teszt a saját mintáját nézi)
            static float acc = 0;
            static int n = 0;
            acc = std::max(acc, std::max(l, r));
            if (++n < 30) return;
            const char *what[] = {"0 dB", "A −12 dB", "A némítva", "KI −6 dB"};
            if (*steps < 4)
                std::printf("%-10s csúcs %6.1f dBFS · %s\n", what[*steps], acc > 1e-5f ? 20 * std::log10(acc) : -100.0,
                            qPrintable(mx->debugText()));
            acc = 0;
            n = 0;
            ++*steps;
            if (*steps == 1) mx->setDb(ChannelMixer::A, -12);
            if (*steps == 2) mx->setMuted(ChannelMixer::A, true);
            if (*steps == 3) { mx->setMuted(ChannelMixer::A, false); mx->setDb(ChannelMixer::A, 0); mx->setDb(ChannelMixer::Out, -6); }
            if (*steps == 4) {
                mx->setDb(ChannelMixer::C, -3);
                window.grab().save(qEnvironmentVariable("ETER_MIXER_TEST"));
            }
            if (*steps == 5) {
                t->stop();
                QApplication::exit(0);
            }
            std::fflush(stdout);
        });
        QTimer::singleShot(2600, &window, [t] { t->start(30); });
    }
    if (qEnvironmentVariableIsSet("ETER_WEB_TEST")) {
        // Fejlesztéshez: kapcsolat a WordPress-oldallal (ETER_WEB_URL, ETER_WEB_KEY). A fájlok zenék az „Előadó N”
        // előadóval; az első négy az ütemező soraként a playlistben, és szól. 5 mp-enként az állapot; a végén
        // (ETER_WEB_SECS) a kérések táblája, és kép a főablakról és a Kérések ablakról (ETER_WEB_TEST = a képek mappája).
        // ETER_WEB_APPROVE=1: jóváhagyásos mód; ETER_WEB_APPROVE_AT: ennyi mp-nél az első várakozó jóváhagyása.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingles = db.addCategory(QStringLiteral("Jingle"), "jingle", "#b99cff");
        QVector<int> ids;
        int n = 0;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            const bool jingle = QFileInfo(f).completeBaseName().startsWith(QLatin1String("jingle"));
            it.categoryId = jingle ? jingles : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = jingle ? QStringLiteral("Állomásazonosító") : QStringLiteral("Dal %1").arg(++n);
            it.artist = jingle ? QString() : QStringLiteral("Előadó %1").arg(n);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids.mid(0, 4), -1, 0, {}, QStringLiteral("scheduler"));
        WebSync::Config c;
        c.enabled = true;
        c.url = qEnvironmentVariable("ETER_WEB_URL");
        c.key = qEnvironmentVariable("ETER_WEB_KEY");
        c.requests = true;
        c.approve = qEnvironmentVariable("ETER_WEB_APPROVE") == QLatin1String("1");
        WebSync::saveConfig(c);
        window.configureWeb();
        QTimer::singleShot(1000, &window, [&window] {
            window.setMode(MainWindow::Auto);
            window.next();
        });
        auto *t = new QTimer(&window);
        QObject::connect(t, &QTimer::timeout, &window, [&window] {
            std::printf("[%s] web: %s\n  sorok: %s\n", qPrintable(QTime::currentTime().toString("HH:mm:ss")),
                        qPrintable(window.webSync()->status()), qPrintable(window.debugState()));
            std::fflush(stdout);
        });
        t->start(5000);
        const int approveAt = qEnvironmentVariableIntValue("ETER_WEB_APPROVE_AT");
        if (approveAt > 0)
            QTimer::singleShot(approveAt * 1000, &window, [&window, &db] {
                window.openRequestsDialog();
                const auto pending = db.webRequests({QStringLiteral("pending")});
                std::printf("jóváhagyásra vár: %d\n", int(pending.size()));
                if (!pending.isEmpty()) window.approveRequest(pending.first().id);
            });
        const int secs = qEnvironmentVariableIsSet("ETER_WEB_SECS") ? qEnvironmentVariableIntValue("ETER_WEB_SECS") : 40;
        QTimer::singleShot(secs * 1000, &window, [&window, &db] {
            for (const WebRequest &r : db.webRequests())
                std::printf("kérés #%d (távoli %d): %s – %s · %s · sor %d\n", r.id, r.remoteId, qPrintable(r.artist),
                            qPrintable(r.title), qPrintable(r.status), r.entryId);
            const QString dir = qEnvironmentVariable("ETER_WEB_TEST");
            window.grab().save(dir + "/web-main.png");
            window.openRequestsDialog();
            QTimer::singleShot(500, &window, [dir, &window] {
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (w->isVisible() && w->windowTitle() == TR("Számkérések")) w->grab().save(dir + "/web-requests.png");
                auto *dlg = new SettingsDialog(&window);
                if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == TR("Weboldal")) tabs->setCurrentIndex(i);
                dlg->resize(820, 820);
                dlg->show();
                QApplication::processEvents();
                dlg->grab().save(dir + "/web-settings.png");
                QApplication::exit(0);
            });
        });
    }
    if (qEnvironmentVariableIsSet("ETER_SMARTMIX_TEST")) {
        // Fejlesztéshez: okos automatikus mix adásban. A megadott fájlok zeneként, sorban; minden szám 26 mp-re ugrik,
        // és kiírja, a kimenő szám hol tart, amikor a következő elindul, és hol áll meg.
        // A „jingle”, „ad”, „news” kezdetű fájlok ilyen (nem zenei) kategóriába kerülnek. ETER_SMARTMIX_NOJUMP=1: nincs
        // ugrás 26 mp-re. ETER_SMARTMIX_SET="kulcs=érték,…": beállítások (például smart_hidden_track=0).
        db.setSetting("mix_mode", "auto");
        for (const QString &kv : qEnvironmentVariable("ETER_SMARTMIX_SET").split(QLatin1Char(','), Qt::SkipEmptyParts))
            db.setSetting(kv.section(QLatin1Char('='), 0, 0), kv.section(QLatin1Char('='), 1));
        window.reloadMixSettings();
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Jingle"), "jingle", "#b99cff");
        const int ad = db.addCategory(QStringLiteral("Reklám"), "ad", "#ff7a7a");
        const int news = db.addCategory(QStringLiteral("Hírek"), "news", "#7ad1ff");
        QVector<int> ids;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            const QString base = QFileInfo(f).completeBaseName();
            it.categoryId = base.startsWith(QLatin1String("jingle")) ? jingle : base.startsWith(QLatin1String("ad")) ? ad
                          : base.startsWith(QLatin1String("news")) ? news : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.setAutoMix(true);
        window.appendItems(ids);
        if (qEnvironmentVariableIsSet("ETER_SMARTMIX_SHOT")) {
            // Képek: a Mix editor sávja az okos mix tervével, és a Beállítások „Automatikus mix” lapja.
            QTimer::singleShot(1200, &window, [&window, &db] {
                const QVector<Entry> en = db.pendingEntries();
                window.mixPage()->window()->show();   // (a „Mix editor” feliratú gomb a mód-kapcsoló is)
                window.mixPage()->selectEntries(en[0].id, en[1].id);
            });
            QTimer::singleShot(1800, &window, [&window] {
                const QString dir = qEnvironmentVariable("ETER_SMARTMIX_SHOT");
                window.mixPage()->window()->grab().save(dir + "/smart-mixeditor.png");
                std::printf("Mix editor: %s\n", qPrintable(window.mixPage()->smartPlanText()));
                auto *dlg = new SettingsDialog(&window);
                dlg->resize(860, 900);
                dlg->show();
                if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                    for (int i = 0; i < tabs->count(); ++i)
                        if (tabs->tabText(i) == TR("Automatikus mix")) tabs->setCurrentIndex(i);
                QTimer::singleShot(600, dlg, [dlg, dir] {
                    dlg->grab().save(dir + "/smart-settings.png");
                    dlg->close();
                    QApplication::exit(0);
                });
            });
            return app.exec();
        }
        QTimer::singleShot(2000, &window, [&window] {   // a hullámformák beolvasása után
            window.setMode(MainWindow::Auto);
            window.next();
        });
        auto *e = window.engine();
        struct St { bool playing = false; QString path; bool jumped = false; };
        auto st = std::make_shared<std::array<St, 3>>();
        auto *sampler = new QTimer(&window);
        QObject::connect(sampler, &QTimer::timeout, &window, [e, st, &window] {
            const eter::Engine::Channel ch[3] = {eter::Engine::Player1, eter::Engine::Player2, eter::Engine::Player3};
            for (int i = 0; i < 3; ++i) {
                auto &src = e->source(ch[i]);
                St &s = (*st)[i];
                const bool now = src.playing();
                const QString path = QFileInfo(QString::fromStdString(src.path())).completeBaseName();
                if (now && !s.playing) {
                    QString others;
                    for (int j = 0; j < 3; ++j)
                        if (j != i && e->source(ch[j]).playing())
                            others += QStringLiteral(" · %1 itt tart: %2 mp").arg(QFileInfo(QString::fromStdString(e->source(ch[j]).path())).completeBaseName())
                                          .arg(e->source(ch[j]).position() / double(eter::kRate), 0, 'f', 2);
                    static QElapsedTimer clock;
                    if (!clock.isValid()) clock.start();
                    std::printf("[%7.2f] indul: %s (%.2f mp-től)%s\n", clock.elapsed() / 1000.0, qPrintable(path),
                                src.position() / double(eter::kRate), qPrintable(others));
                    s.jumped = false;
                }
                if (!now && s.playing) std::printf("megállt: %s (%.2f mp-nél)\n", qPrintable(s.path), src.position() / double(eter::kRate));
                if (now && !s.jumped && src.position() > eter::kRate && !qEnvironmentVariableIsSet("ETER_SMARTMIX_NOJUMP")) {
                    s.jumped = true;
                    src.seek(26LL * eter::kRate);
                }
                if (now) s.path = path;
                s.playing = now;
            }
            std::fflush(stdout);
        });
        sampler->start(20);
        QTimer::singleShot(qEnvironmentVariableIntValue("ETER_SMARTMIX_SECS") * 1000 + 2500, &window, [] { QApplication::exit(0); });
    }
    if (qEnvironmentVariableIsSet("ETER_FLOW_SHOT")) {
        // Fejlesztéshez: a megadott fájlok zeneként, véletlen tempóval (110–132) és hangnemmel; séma: 3 zene + kitöltés,
        // tempó-szabály 8 %, hangnem-szabály ETER_FLOW_KEY (alap: 1). Generálás, összegzés, képek a playlistről és az ütemezőről.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QRandomGenerator rng(11);
        QVector<int> ids;
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QStringLiteral("Szám %1").arg(ids.size() + 1);
            it.artist = QStringLiteral("Előadó %1").arg(ids.size() + 1);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            it.bpm = 110 + rng.bounded(23);
            it.musicKey = 1 + rng.bounded(24);
            it.vocal = ids.size() % 6 == 5 ? 0 : 1 + rng.bounded(5);
            ids << db.addItem(it);
            if (it.vocal) db.setItemVocal({ids.last()}, it.vocal);
        }
        const int clock = db.addClock(QStringLiteral("Folyam"), "#f5a524");
        ClockSlot s;
        s.clockId = clock;
        s.type = "fill";
        s.categoryId = music;
        db.addSlot(s);
        for (int d = 0; d < 7; ++d)
            for (int h = 0; h < 24; ++h) db.setGrid(d, h, clock);
        db.setSetting("artist_separation_min", "0");
        db.setSetting("title_separation_min", "60");
        db.setSetting("rule_bpm_jump_pct", "8");
        db.setSetting("rule_key", qEnvironmentVariable("ETER_FLOW_KEY", "1"));
        db.setSetting("rule_vocal_max", qEnvironmentVariable("ETER_FLOW_VOCAL", "2"));
        db.setSetting("generate_ahead_hours", "2");
        db.notifyItemsChanged();
        window.schedulerPage()->refreshAll();
        QTimer::singleShot(800, &window, [&window, &db] {
            window.schedulerPage()->generateNow();
            std::printf("generálás: %s\n", qPrintable(window.schedulerPage()->generateInfo()));
            // Egy szándékosan rossz átmenet: egy messzi tempójú és hangnemű szám a 3. helyre.
            Item odd = db.items().first();
            db.setItemFlow({odd.id}, -1, 175, -1);
            db.setItemKey({odd.id}, mkey::make(6, false));
            window.appendItems({odd.id}, 3);
            // És egy túl hosszú női sorozat (3 egymás után, a szabály 2).
            const QVector<Item> all = db.items();
            QVector<int> fem;
            for (int k = 1; k < all.size() && fem.size() < 3; ++k) fem << all[k].id;
            db.setItemVocal(fem, vocal::Female);
            window.appendItems(fem, 5);
            std::fflush(stdout);
        });
        QTimer::singleShot(2500, &window, [&window] {
            const QString dir = qEnvironmentVariable("ETER_FLOW_SHOT");
            window.grab().save(dir + "/flow-playlist.png");
        });
        QTimer::singleShot(3500, &window, [&window] {
            // Az Ütemező fülre kattintva külön ablakban nyílik meg.
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == TR("Ütemező")) b->click();
            QWidget *page = window.schedulerPage()->window();
            std::printf("ütemező ablak: %s, külön: %s, cím: %s\n", page->isVisible() ? "látszik" : "rejtve",
                        page != &window ? "igen" : "nem", qPrintable(page->windowTitle()));
            page->grab().save(qEnvironmentVariable("ETER_FLOW_SHOT") + "/flow-scheduler.png");
            if (auto *t = window.schedulerPage()->findChild<QToolButton *>("rulesToggle")) {
                QWidget *body = window.schedulerPage()->findChild<QWidget *>("rulesBody");
                std::printf("szabályok: %s\n", body && body->isVisible() ? "nyitva" : "csukva");
                t->click();
                QApplication::processEvents();
                std::printf("kattintás után: %s\n", body && body->isVisible() ? "nyitva" : "csukva");
                page->grab().save(qEnvironmentVariable("ETER_FLOW_SHOT") + "/flow-scheduler-open.png");
            }
            QApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("ETER_ANALYZE_WATCH")) {
        // Fejlesztéshez: a háttérelemzés állapota másodpercenként; ha kész, kiírja az eredményt és kilép.
        // A megadott fájlokat (címkékkel együtt) zeneként veszi fel.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        for (const QString &f : parser.positionalArguments()) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            it.categoryId = music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            it.musicKey = mkey::parse(QString::fromStdString(dec.keyTag()));
            it.bpm = mkey::parseBpm(QString::fromStdString(dec.bpmTag()));
            db.addItem(it);
        }
        db.notifyItemsChanged();
        auto *t = new QTimer(&window);
        QObject::connect(t, &QTimer::timeout, &window, [&window, &db] {
            LibraryAnalyzer *an = window.analyzer();
            std::printf("elemzés: %s\n", qPrintable(an->status()));
            std::fflush(stdout);
            if (an->total() > 0 && an->pending() == 0 && !an->working()) {
                for (const Item &it : db.items())
                    std::printf("  %-28s  BPM %6.1f  hangnem %-4s %-4s  (ver %d)\n", qPrintable(QFileInfo(it.path).fileName()), it.bpm,
                                qPrintable(mkey::camelot(it.musicKey)), qPrintable(mkey::traditional(it.musicKey)), it.analysisVer);
                const QString shots = qEnvironmentVariable("ETER_ANALYZE_SHOT");
                if (shots.isEmpty()) {
                    QApplication::exit(0);
                    return;
                }
                static bool once = false;
                if (once) return;
                once = true;
                db.notifyItemsChanged();
                QTimer::singleShot(800, &window, [&window, &db, shots] {
                    window.grab().save(shots + "/library.png");
                    auto *dlg = new SettingsDialog(&window);
                    dlg->resize(820, 760);
                    dlg->show();
                    if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                        for (int i = 0; i < tabs->count(); ++i)
                            if (tabs->tabText(i) == TR("Elemzés")) tabs->setCurrentIndex(i);
                    QTimer::singleShot(800, dlg, [&window, &db, dlg, shots] {
                        dlg->grab().save(shots + "/settings.png");
                        dlg->close();
                        auto *ed = new ItemEditorDialog(&db, window.engine(), db.items().first().id, &window);
                        ed->resize(1000, 640);
                        ed->show();
                        QTimer::singleShot(1500, ed, [ed, shots] {
                            ed->grab().save(shots + "/editor.png");
                            QApplication::exit(0);
                        });
                    });
                });
            }
        });
        t->start(1000);
    }
    if (qEnvironmentVariableIsSet("ETER_WATCH_STATE")) {
        // Fejlesztéshez: az állapot másodpercenként, kilépés a megadott idő után.
        auto *t = new QTimer(&window);
        QObject::connect(t, &QTimer::timeout, &window, [&window] {
            std::printf("fő: %s\n", qPrintable(window.debugState()));
            std::fflush(stdout);
        });
        t->start(1000);
        QTimer::singleShot(qEnvironmentVariableIntValue("ETER_WATCH_STATE") * 1000, [] { QApplication::exit(0); });
    }
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty() && !parser.isSet(uiTestOpt) && !parser.isSet(schedTestOpt) && !parser.isSet(mixTestOpt) && !parser.isSet(recTestOpt) && !parser.isSet(plTestOpt) && !parser.isSet(cartTestOpt) && !parser.isSet(editTestOpt) && !parser.isSet(eventTestOpt) && !parser.isSet(lookTestOpt) && !parser.isSet(nextTestOpt) && !parser.isSet(overlayTestOpt) && !parser.isSet(serveTestOpt) && !parser.isSet(countTestOpt) && !parser.isSet(clickTestOpt) && !parser.isSet(fileTestOpt) && !parser.isSet(fixTestOpt) && !parser.isSet(loggerTestOpt) && !parser.isSet(threeTestOpt) && !parser.isSet(dbTestOpt) && !parser.isSet(envTestOpt) && !parser.isSet(freshTestOpt) && !parser.isSet(fillTestOpt) && !qEnvironmentVariableIsSet("ETER_ANALYZE_WATCH") && !qEnvironmentVariableIsSet("ETER_FLOW_SHOT") && !qEnvironmentVariableIsSet("ETER_SMARTMIX_TEST") && !qEnvironmentVariableIsSet("ETER_MIXER_TEST") && !qEnvironmentVariableIsSet("ETER_MIXDOWN_TEST") && !qEnvironmentVariableIsSet("ETER_WEB_TEST") && !qEnvironmentVariableIsSet("ETER_CART_SHOT") && !qEnvironmentVariableIsSet("ETER_LIBLOG_SHOT") && !qEnvironmentVariableIsSet("ETER_UI32_SHOT") && !qEnvironmentVariableIsSet("ETER_V033_SHOT") && !qEnvironmentVariableIsSet("ETER_THEME_SHOT") && !qEnvironmentVariableIsSet("ETER_SQL_TEST") && !qEnvironmentVariableIsSet("ETER_ADB_TEST") && !qEnvironmentVariableIsSet("ETER_INFO_TEST") && !qEnvironmentVariableIsSet("ETER_MIXPV_SHOT") && !qEnvironmentVariableIsSet("ETER_MIDI_TEST") && !qEnvironmentVariableIsSet("ETER_TPL_TEST") && !qEnvironmentVariableIsSet("ETER_STREAM_TEST") && !qEnvironmentVariableIsSet("ETER_STREAMDLG_SHOT")) window.library()->importPaths(files);

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
        window.showRight(2);
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
            const bool p3 = e->source(eter::Engine::Player3).playing();
            const bool o1 = e->source(eter::Engine::Ovl1).playing(), o2 = e->source(eter::Engine::Ovl2).playing();
            const int n = int(p1) + int(p2) + int(p3) + int(o1) + int(o2);
            st->maxTogether = std::max(st->maxTogether, n);
            if ((o1 || o2) && st->ovlStart < 0) st->ovlStart = t;
            if (st->ovlStart >= 0 && !o1 && !o2 && st->ovlEnd < 0) st->ovlEnd = t;
            if ((p2 || p3) && st->cStart < 0) st->cStart = t;
            static int k = 0;
            if (++k % 10 == 0)
                std::printf("  %5.2f s  P1 %d  P2 %d  P3 %d  RÁF %d%d  (%d szól)  %s\n", t, p1, p2, p3, o1, o2, n, qPrintable(window.debugState()));
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
    if (parser.isSet(flowTestOpt)) {
        // 150 kitalált zeneszám (a fájlok nem kellenek a generáláshoz), séma: 3 szám 4–5 energiával, majd kitöltés.
        // ETER_FLOW_RULES=0: szabályok nélkül (összehasonlításhoz).
        const bool rules = qEnvironmentVariable("ETER_FLOW_RULES", "1") != QLatin1String("0");
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QRandomGenerator rng(7);
        for (int i = 0; i < 150; ++i) {
            Item it;
            it.categoryId = music;
            it.path = QStringLiteral("/nincs/szam%1.mp3").arg(i);
            it.title = QStringLiteral("Szám %1").arg(i);
            it.artist = QStringLiteral("Előadó %1").arg(i % 45);
            it.durationMs = 170000 + rng.bounded(80000);
            const int id = db.addItem(it);
            const int energy = i % 10 == 0 ? 0 : 1 + rng.bounded(5);
            const int parts = i % 6 == 0 ? (1 << 4) : 0;              // minden hatodik csak este szólhat
            db.setItemFlow({id}, energy, 80 + rng.bounded(80), parts);
            if (i % 12 != 0) db.setItemKey({id}, 1 + rng.bounded(24));   // minden 12. hangneme ismeretlen
            // Ének: a hangtár fele női (mint egy vokális house-válogatásban), a többi vegyesen; minden 10. ismeretlen.
            if (i % 10 != 0) {
                const int r = rng.bounded(10);
                db.setItemVocal({id}, r < 5 ? vocal::Female : r < 7 ? vocal::Male : r < 8 ? vocal::Group : r < 9 ? vocal::Mixed : vocal::Instrumental);
            }
        }
        const int clock = db.addClock(QStringLiteral("Folyam"), "#f5a524");
        ClockSlot s;
        s.clockId = clock;
        s.type = "category";
        s.categoryId = music;
        s.count = 3;
        s.energyMin = 4;
        s.energyMax = 5;
        db.addSlot(s);
        s = ClockSlot();
        s.clockId = clock;
        s.type = "fill";
        s.categoryId = music;
        db.addSlot(s);
        for (int d = 0; d < 7; ++d)
            for (int h = 0; h < 24; ++h) db.setGrid(d, h, clock);
        db.setSetting("artist_separation_min", "20");
        db.setSetting("title_separation_min", "180");
        db.setSetting("rule_slow_max", rules ? "1" : "0");
        db.setSetting("rule_energy_jump", rules ? "2" : "0");
        db.setSetting("rule_bpm_jump_pct", rules ? "25" : "0");
        db.setSetting("rule_key", rules ? qEnvironmentVariable("ETER_FLOW_KEY", "2") : QStringLiteral("0"));
        db.setSetting("rule_vocal_max", rules ? qEnvironmentVariable("ETER_FLOW_VOCAL", "2") : QStringLiteral("0"));
        Scheduler sched(&db);
        const QDateTime from(eter::now().date(), QTime(16, 0));
        const GenerateResult r = sched.generate(from, 6, true);           // 16–22: a 19 órás napszakhatáron át
        const QVector<int> starts = Scheduler::daypartStarts(&db);
        int slowPairs = 0, bigJumps = 0, bpmJumps = 0, wrongPart = 0, wrongBand = 0, n = 0, keyMiss = 0, keyKnown = 0;
        int prevKey = 0, inHour = 0;
        Item prev;
        for (const Entry &e : db.pendingEntries()) {
            const Item &it = e.item;
            if (e.playlistKey != prevKey) { prevKey = e.playlistKey; inHour = 0; }
            const int part = Scheduler::daypartOf(e.playlistKey % 100, starts);
            if (it.dayparts && !(it.dayparts & (1 << part))) ++wrongPart;
            if (inHour < 3 && it.energy > 0 && it.energy < 4) ++wrongBand;
            if (prev.id) {
                if (prev.energy > 0 && prev.energy <= 2 && it.energy > 0 && it.energy <= 2) ++slowPairs;
                if (prev.energy > 0 && it.energy > 0 && std::abs(prev.energy - it.energy) > 2) ++bigJumps;
                double ratio = it.bpm / prev.bpm;
                if (ratio < 0.75) ratio *= 2;
                if (ratio > 1.5) ratio /= 2;
                if (std::abs(ratio - 1) > 0.25) ++bpmJumps;
                if (mkey::valid(prev.musicKey) && mkey::valid(it.musicKey)) {
                    ++keyKnown;
                    if (!mkey::compatible(prev.musicKey, it.musicKey)) ++keyMiss;
                }
            }
            prev = it;
            ++inHour;
            ++n;
        }
        std::printf("szabályok %s: %d óra, %d elem, lazítás %d | két lassú egymás után: %d, 2-nél nagyobb energiaugrás: %d, "
                    "25%%-nál nagyobb tempóváltás: %d, rossz napszak: %d, a 4–5-ös helyen kisebb energia: %d\n",
                    rules ? "be" : "ki", r.hours, n, r.relaxed, slowPairs, bigJumps, bpmJumps, wrongPart, wrongBand);
        std::printf("hangnem-szabály %s: nem illő átmenet %d / %d (ismert hangnemű pár)\n",
                    qPrintable(db.setting("rule_key")), keyMiss, keyKnown);
        const QVector<Entry> entries = db.pendingEntries();
        const FlowStats fs = Scheduler::flowStats(entries, 0, 0, 2);
        int longest = 0, vt = 0, vr = 0;
        for (const Entry &e : entries)
            if (e.item.kind == QLatin1String("music") && vocal::counts(e.item.vocal)) {
                vr = e.item.vocal == vt ? vr + 1 : 1;
                vt = e.item.vocal;
                longest = std::max(longest, vr);
            }
        std::printf("ének-szabály %s: 2-nél hosszabb azonos sorozatban %d / %d szám · leghosszabb sorozat %d · lazítás: folyam %d\n",
                    qPrintable(db.setting("rule_vocal_max")), fs.vocalKnown - fs.vocalOk, fs.vocalKnown, longest, r.relaxedBy[1]);
        return 0;
    }
    if (parser.isSet(loggerTestOpt)) {
        // ETER_LOG_DIR mappába rögzít ETER_LOG_SECS másodpercig. ETER_LOG_PFL: csak előhallgatás szól (a felvétel néma kell legyen).
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
        {
            QSettings st;
            st.setValue("logger/enabled", true);
            st.setValue("logger/folder", qEnvironmentVariable("ETER_LOG_DIR"));
            st.setValue("logger/kbps", 128);
            st.setValue("logger/days", 30);
        }
        window.configureLogger();
        const bool pflOnly = qEnvironmentVariableIsSet("ETER_LOG_PFL");
        window.setMode(MainWindow::Auto);
        QTimer::singleShot(200, &window, [&window, pflOnly, files] {
            if (pflOnly) {
                auto &pfl = window.engine()->source(eter::Engine::Pfl);
                pfl.load(QFileInfo(files.first()).absoluteFilePath().toStdString());
                pfl.play();
            } else {
                window.next();
            }
        });
        auto *t = new QTimer(&window);
        QObject::connect(t, &QTimer::timeout, &window, [&window] {
            std::printf("  %s\n", qPrintable(window.loggerStatus()));
            std::fflush(stdout);
        });
        t->start(2000);
        QTimer::singleShot(qEnvironmentVariableIntValue("ETER_LOG_SECS") * 1000, [] {
            QSettings().remove("logger");
            QApplication::exit(0);
        });
    }
    if (parser.isSet(fixTestOpt)) {
        // Fájlok: zene (a hírek.wav kivételével); a playlist: az első ETER_FIX_N zene, utána a hírek fix időpontra
        // (ETER_FIX_AT mp múlva). ETER_FIX_ADJUST: manual / fill / both. ETER_FIX_SHOT: a párbeszédablak képe.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int news = db.addCategory(QStringLiteral("Hírek"), "news", "#e5484d");
        QVector<int> songs;
        int newsId = 0;
        for (const QString &f : files) {
            eter::Decoder dec;
            if (!dec.open(f.toStdString())) continue;
            Item it;
            const bool isNews = QFileInfo(f).completeBaseName() == QLatin1String("hirek");
            it.categoryId = isNews ? news : music;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = isNews ? QStringLiteral("Hírek") : QFileInfo(f).completeBaseName();
            it.artist = isNews ? QString() : QStringLiteral("Előadó %1").arg(songs.size() + 1);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            const int id = db.addItem(it);
            if (isNews) newsId = id; else songs << id;
        }
        db.notifyItemsChanged();
        const int n = std::clamp(qEnvironmentVariableIntValue("ETER_FIX_N"), 1, int(songs.size()));
        // ETER_FIX_SCHED: mintha az ütemező tette volna be őket (így kivehetők, cserélhetők).
        window.appendItems(songs.mid(0, n), -1, 0, {}, qEnvironmentVariableIsSet("ETER_FIX_SCHED") ? QStringLiteral("scheduler") : QStringLiteral("manual"));
        const QDateTime now = eter::now();
        const QDateTime at = now.addSecs(qEnvironmentVariableIntValue("ETER_FIX_AT"));
        int key = 0;
        const int pl = db.playlistFor(at.date(), at.time().hour(), &key);
        db.addEntry(pl, newsId, "manual", at.time().minute() * 60 + at.time().second());
        emit window.schedulerPage()->generated({});
        // ETER_FIX_TAIL: minden szám még egyszer a fix pont után (mintha 24 órára előre le lenne generálva a lista).
        if (qEnvironmentVariableIsSet("ETER_FIX_TAIL")) window.appendItems(songs, -1, 0, {}, QStringLiteral("scheduler"));
        db.setSetting("fix_adjust", qEnvironmentVariable("ETER_FIX_ADJUST", "manual"));
        db.setSetting("fix_gap_fill", qEnvironmentVariable("ETER_FIX_GAP", "0"));
        if (qEnvironmentVariableIsSet("ETER_FIX_CLOCKFILL")) {
            // Külön „12 Inch” kategória (a számok másolata) és séma a fix pont előtti órára; a séma saját kitöltési
            // beállítása ETER_FIX_CLOCKFILL („-1”: a séma kitöltő kategóriája, „cat”: a 12 Inch kategória, „”: általános).
            const int twelve = db.addCategory(QStringLiteral("12 Inch"), "music", "#f5a524");
            for (int k = 0; k < songs.size(); ++k) {
                Item it = db.item(songs[k]);
                it.id = 0;
                it.categoryId = twelve;
                static QTemporaryDir copies;   // a fájlútvonal egyedi a hangtárban
                const QString to = copies.filePath(QStringLiteral("12in-%1.%2").arg(k + 1).arg(QFileInfo(it.path).suffix()));
                QFile::link(it.path, to);
                it.path = to;
                it.title = QStringLiteral("12in-%1").arg(k + 1);
                it.artist = QStringLiteral("Twelve %1").arg(k + 1);
                db.addItem(it);
            }
            db.notifyItemsChanged();
            const int clock = db.addClock(QStringLiteral("12 Inch"), "#f5a524");
            ClockSlot slot;
            slot.clockId = clock;
            slot.type = QStringLiteral("fill");
            slot.categoryId = twelve;
            db.addSlot(slot);
            const QDateTime before = at.addSecs(-1);
            db.setGrid(before.date().dayOfWeek() - 1, before.time().hour(), clock);
            QString v = qEnvironmentVariable("ETER_FIX_CLOCKFILL");
            if (v == QLatin1String("cat")) v = QString::number(twelve);
            db.setSetting(QStringLiteral("fix_fill_category_clock_%1").arg(clock), v);
            db.setSetting("fix_fill_category", QString::number(music));
        }
        window.setMode(MainWindow::Auto);
        auto report = [&window](const char *when) {
            std::printf("%-22s %s\n  sorok: %s\n", when, qPrintable(window.fixDebug()), qPrintable(window.debugState()));
            std::fflush(stdout);
        };
        QTimer::singleShot(1500, &window, [report] { report("kezdetben"); });
        QTimer::singleShot(2500, &window, [&window] {
            if (qEnvironmentVariableIsSet("ETER_FIX_SHOT")) {
                window.grab().save(qEnvironmentVariable("ETER_FIX_SHOT") + "-main.png");
                window.openFixDialog();
            }
        });
        QTimer::singleShot(3500, &window, [&window, report] {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->isVisible() && w->windowTitle().contains(QStringLiteral("fix időponthoz"))) {
                    w->grab().save(qEnvironmentVariable("ETER_FIX_SHOT") + "-dialog.png");
                    for (QPushButton *b : w->findChildren<QPushButton *>())
                        if (b->property("accent").toBool() && qEnvironmentVariableIsSet("ETER_FIX_APPLY")) {
                            std::printf("párbeszédablak: %s\n", qPrintable(b->text()));
                            b->click();
                        }
                }
            report("3,5 mp után");
        });
        const int deletes = qEnvironmentVariableIntValue("ETER_FIX_DELETE");
        if (deletes > 0) {
            // A fix pont előtti utolsó elem törlése 8 mp-enként (mintha kézzel törölnék), és az állapot utána.
            auto *t = new QTimer(&window);
            auto count = std::make_shared<int>(0);
            QObject::connect(t, &QTimer::timeout, &window, [&window, count, deletes, t] {
                std::printf("%d. törlés előtt: %s\n", *count + 1, qPrintable(window.fixDebug()));
                if (*count >= deletes) {
                    t->stop();
                    QApplication::exit(0);
                    return;
                }
                std::printf("   törölve: %s\n", qPrintable(window.deleteBeforeFixForTest()));
                ++*count;
                std::fflush(stdout);
            });
            t->start(8000);
        } else {
            QTimer::singleShot(5000, [] { QApplication::exit(0); });
        }
    }
    if (parser.isSet(fileTestOpt)) {
        // A fájlok másolata egy ideiglenes mappában; a 3. fájlt eltüntetjük, majd visszatesszük.
        static QTemporaryDir dir;
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        QStringList copies;
        for (int n = 0; n < files.size(); ++n) {
            const QString to = dir.filePath(QStringLiteral("%1_%2").arg(n).arg(QFileInfo(files[n]).fileName()));
            QFile::copy(files[n], to);
            copies << to;
            eter::Decoder dec;
            if (!dec.open(to.toStdString())) continue;
            Item it;
            it.categoryId = cat;
            it.path = to;
            it.title = QFileInfo(files[n]).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        const QString victim = copies.value(2), parked = victim + QStringLiteral(".el");
        auto show = [&window](const char *when) {
            std::printf("%-34s hiányzik: [%s]\n", when, qPrintable(window.missingTitles().join(", ")));
            std::fflush(stdout);
        };
        QTimer::singleShot(2500, &window, [&window, show, victim, parked] {
            show("indulás után");
            QFile::rename(victim, parked);
            window.checkFilesNow();
        });
        QTimer::singleShot(4000, &window, [&window, show] {
            show("a 3. fájl eltávolítva");
            window.showMissingFiles();
        });
        QTimer::singleShot(5000, &window, [&window] {
            window.grab().save("missing-main.png");
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->isVisible() && w->windowTitle().contains(QStringLiteral("Hiányzó"))) w->grab().save("missing-dialog.png");
            window.scanLibraryFiles();
        });
        QTimer::singleShot(6500, &window, [&window, show, victim, parked] {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->isVisible() && w->windowTitle().contains(QStringLiteral("hangtárban"))) {
                    w->grab().save("missing-library.png");
                    if (auto *l = w->findChild<QLabel *>()) std::printf("hangtár: %s\n", qPrintable(l->text()));
                }
            QFile::rename(parked, victim);
            window.checkFilesNow();
        });
        QTimer::singleShot(8000, &window, [show] {
            show("visszatéve");
            QApplication::exit(0);
        });
    }
    if (parser.isSet(clickTestOpt)) {
        // 1. fájl zene, 2. fájl szignál (nem zene), 3. fájl zene. Mix editor: a szignál alapból nem halkul;
        // kattintás a sávokban előhallgatást indít onnan, húzás nem. ETER_CLICK_AIR: utána adás automata módban.
        const int music = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Szignál"), "jingle", "#ffb020");
        QVector<int> ids;
        for (int n = 0; n < files.size(); ++n) {
            eter::Decoder dec;
            if (!dec.open(files[n].toStdString())) continue;
            Item it;
            it.categoryId = n == 1 ? jingle : music;
            it.path = QFileInfo(files[n]).absoluteFilePath();
            it.title = QFileInfo(files[n]).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        auto &e = *window.engine();
        const eter::Engine::Channel chs[3] = {eter::Engine::Pfl, eter::Engine::Pfl2, eter::Engine::Pfl4};
        auto sources = [&e, chs] {
            QString s;
            for (int k = 0; k < 3; ++k) {
                auto &src = e.source(chs[k]);
                s += QStringLiteral(" %1:%2@%3").arg(k + 1).arg(src.playing() ? "szól" : "áll").arg(src.position() / 48000.0, 0, 'f', 1);
            }
            return s;
        };
        if (!qEnvironmentVariableIsSet("ETER_CLICK_AIR")) QTimer::singleShot(400, &window, [&window, sources] {
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == QStringLiteral("Mix editor")) b->click();
            MixEditorPage *page = window.mixPage();
            for (QPushButton *b : page->findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("3 elem") && !b->isChecked()) b->click();
            page->selectPair(0);
            QApplication::processEvents();
            MixTimeline *tl = page->timeline();
            for (int j = 0; j < tl->transitions(); ++j) {
                const MixParams &p = tl->params(j);
                std::printf("%d→%d: mix %.1f, kiúsztatás %.1f, a kimenő vége %.1f (hossza %.1f)\n", j + 1, j + 2, p.mixOutMs / 1000.0,
                            p.fadeOutMs / 1000.0, tl->outEndTrack(j) / 1000.0, tl->clipEnd(j) / 1000.0 - tl->clipStart(j) / 1000.0);
            }
            auto mouse = [tl](QPointF from, QPointF to) {
                QMouseEvent press(QEvent::MouseButtonPress, from, tl->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(tl, &press);
                if (to != from) {
                    QMouseEvent move(QEvent::MouseMove, to, tl->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(tl, &move);
                }
                QMouseEvent release(QEvent::MouseButtonRelease, to, tl->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(tl, &release);
            };
            auto steps = std::make_shared<QVector<std::function<void()>>>();
            // a) kattintás a 2. sávban (a szignál), a közepén
            steps->append([tl, mouse, sources] {
                const qint64 t = (tl->clipStart(1) + tl->clipEnd(1)) / 2;
                const QPointF at(tl->xAt(t), tl->laneRect(1).center().y());
                std::printf("kattintás a 2. sávban, %.1f mp-nél:", t / 1000.0);
                mouse(at, at);
                std::printf("%s\n", qPrintable(sources()));
            });
            steps->append([sources, tl] { std::printf("  0,6 mp múlva:%s  lejátszófej: %.1f mp\n", qPrintable(sources()), tl->playhead() / 1000.0); });
            // b) kattintás az 1. sáv elején (üres hely is számít)
            steps->append([tl, mouse, sources] {
                const qint64 t = tl->clipStart(0) + 1000;
                const QPointF at(tl->xAt(t), tl->laneRect(0).top() + 8);
                std::printf("kattintás az 1. sávban, %.1f mp-nél:", t / 1000.0);
                mouse(at, at);
                std::printf("%s\n", qPrintable(sources()));
            });
            steps->append([sources, tl] { std::printf("  0,6 mp múlva:%s  lejátszófej: %.1f mp\n", qPrintable(sources()), tl->playhead() / 1000.0); });
            // c) a gomb továbbra is működik: leállít, majd indít
            steps->append([page, sources] {
                for (QPushButton *b : page->findChildren<QPushButton *>())
                    if (b->text() == QStringLiteral("Leállítás")) b->click();
                std::printf("gomb (leállítás):%s\n", qPrintable(sources()));
            });
            steps->append([page, sources] {
                for (QPushButton *b : page->findChildren<QPushButton *>())
                    if (b->text() == QStringLiteral("Előhallgatás")) b->click();
                std::printf("gomb (előhallgatás):%s\n", qPrintable(sources()));
            });
            steps->append([page, sources] {
                std::printf("  0,6 mp múlva:%s\n", qPrintable(sources()));
                for (QPushButton *b : page->findChildren<QPushButton *>())
                    if (b->text() == QStringLiteral("Leállítás")) b->click();
            });
            // d) húzás: a mix pont változik, előhallgatás nem indul
            steps->append([tl, mouse, sources] {
                const double before = tl->params(0).mixOutMs / 1000.0;
                const QPointF from = tl->handlePos(MixTimeline::Block, 0);
                mouse(from, from + QPointF(-40, 0));
                std::printf("húzás: mix %.1f → %.1f mp;%s\n", before, tl->params(0).mixOutMs / 1000.0, qPrintable(sources()));
            });
            // e) 2 képpontos remegés még kattintás
            steps->append([tl, mouse, sources] {
                const double before = tl->params(0).mixOutMs / 1000.0;
                const QPointF from = tl->handlePos(MixTimeline::Block, 0);
                mouse(from, from + QPointF(2, 1));
                std::printf("remegő kattintás az elemen: mix %.1f → %.1f mp;%s\n", before, tl->params(0).mixOutMs / 1000.0, qPrintable(sources()));
            });
            steps->append([sources] { std::printf("  0,6 mp múlva:%s\n", qPrintable(sources())); QApplication::exit(0); });
            auto *run = new QTimer(tl);
            auto idx = std::make_shared<int>(0);
            QObject::connect(run, &QTimer::timeout, tl, [steps, idx, run] {
                if (*idx < steps->size()) (*steps)[(*idx)++]();
                else run->stop();
            });
            run->start(600);
        });
        if (qEnvironmentVariableIsSet("ETER_CLICK_AIR")) {
            // Adás: a szignál a mix pontnál nem halkul, a 3. elem alatta indul.
            window.setMode(MainWindow::Auto);
            QTimer::singleShot(300, &window, [&window] { window.next(); });
            auto *sampler = new QTimer(&window);
            auto t0 = std::make_shared<qint64>(QDateTime::currentMSecsSinceEpoch());
            QObject::connect(sampler, &QTimer::timeout, &window, [&window, t0] {
                auto &e = *window.engine();
                std::printf("  %5.1f s  %s  halkul: P1 %d, P2 %d\n", (QDateTime::currentMSecsSinceEpoch() - *t0) / 1000.0, qPrintable(window.debugState()),
                            e.source(eter::Engine::Player1).fading(), e.source(eter::Engine::Player2).fading());
                std::fflush(stdout);
            });
            sampler->start(1000);
            QTimer::singleShot(qEnvironmentVariableIntValue("ETER_CLICK_AIR") * 1000, [] { QApplication::exit(0); });
        }
    }
    if (parser.isSet(countTestOpt)) {
        // Visszaszámláló: az első elem (12 mp) utolsó 10 másodperce. ETER_COUNT_AUTO: automata mód, mix ponttal 8 mp-nél.
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
        const bool autoMode = !qEnvironmentVariable("ETER_COUNT_AUTO").isEmpty();
        if (autoMode) {
            const QVector<Entry> en = db.pendingEntries();
            Transition t;
            t.fromEntryId = en[0].id;
            t.toEntryId = en[1].id;
            t.mixOutMs = 8000;
            t.outEndMs = 10000;
            t.fadeOutMs = 2000;
            t.fadeOutCurve = "scurve";
            db.saveTransition(t);
            emit window.mixPage()->mixSaved({});
        }
        window.setMode(autoMode ? MainWindow::Auto : MainWindow::Assist);
        auto t0 = std::make_shared<qint64>(0);
        QTimer::singleShot(300, &window, [&window, t0] {
            *t0 = QDateTime::currentMSecsSinceEpoch();
            window.next();
        });
        auto last = std::make_shared<QString>(QStringLiteral("-"));
        auto *sampler = new QTimer(&window);
        QObject::connect(sampler, &QTimer::timeout, &window, [&window, t0, last] {
            if (!*t0) return;
            const QString now = window.countdownText();
            if (now != *last) {
                std::printf("  %5.2f s  %s\n", (QDateTime::currentMSecsSinceEpoch() - *t0) / 1000.0,
                            now.isEmpty() ? "(nincs visszaszámláló)" : qPrintable(now));
                std::fflush(stdout);
                *last = now;
            }
        });
        sampler->start(20);
        QTimer::singleShot(300 + 5000, &window, [&window] {
            window.grab().save("countdown.png");
            // A visszaszámláló helyén kattintva az alatta lévő elem kapja meg (nem kattintható).
            for (QWidget *w : window.findChildren<QWidget *>())
                if (w->accessibleName() == QLatin1String("countdown")) {
                    const QPoint g = w->mapToGlobal(w->rect().center());
                    QWidget *under = QApplication::widgetAt(g);
                    std::printf("  a visszaszámláló közepén lévő elem: %s (%s)\n", under ? under->metaObject()->className() : "semmi",
                                under == w ? "a visszaszámláló – HIBA" : "nem a visszaszámláló");
                    std::printf("  egéresemények átengedve: %d, fókuszálható: %d\n", w->testAttribute(Qt::WA_TransparentForMouseEvents),
                                w->focusPolicy() != Qt::NoFocus);
                }
        });
        QTimer::singleShot(300 + 14000, [] { QApplication::exit(0); });
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
        // ETER_SERVE_USERS: „név:jelszó:jog,jog;…”; ETER_SERVE_UPLOADDIR: a feltöltések mappája.
        {
            QVector<remote::User> users;
            for (const QString &spec : qEnvironmentVariable("ETER_SERVE_USERS").split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const QStringList p = spec.split(QLatin1Char(':'));
                if (p.size() < 2) continue;
                remote::User u;
                u.name = p[0];
                u.salt = remote::newSalt();
                u.key = remote::deriveKey(u.salt, p[1]);
                if (p.size() > 2) u.perms = p[2].split(QLatin1Char(','), Qt::SkipEmptyParts);
                users << u;
            }
            remote::saveUsers(users);
            if (qEnvironmentVariableIsSet("ETER_SERVE_UPLOADDIR")) st.setValue("remote/uploadDir", qEnvironmentVariable("ETER_SERVE_UPLOADDIR"));
            else st.remove("remote/uploadDir");
        }
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
            QSqlQuery u("SELECT i.id, i.title, c.kind, i.file_path, i.duration_ms FROM items i JOIN categories c ON c.id = i.category_id "
                        "WHERE i.file_path LIKE '%Eter-feltoltesek%' OR i.file_path LIKE '%feltolt%'");
            while (u.next()) std::printf("playout: feltöltött elem %d: %s (%s) %s, %d ms\n", u.value(0).toInt(), qPrintable(u.value(1).toString()),
                                         qPrintable(u.value(2).toString()), qPrintable(u.value(3).toString()), u.value(4).toInt());
            Q_UNUSED(db);
            std::fflush(stdout);
            QApplication::exit(0);
        });
    }
    if (parser.isSet(fillTestOpt)) {
        // Hosszú (12 inches, 5–8,5 perces) számok a felhasználói óraséma szerint: :00 Station ID, zene, :30 Station ID,
        // zene. ETER_FILL_MODE: count (5 darab), fit (legfeljebb 5, amennyi elfér), fill (kitöltés a fix pontig).
        // Kiírja, mennyivel tér el a :30 és a következő :00 a tervezett időtől (24 óra).
        const QString mode = qEnvironmentVariable("ETER_FILL_MODE", "fill");
        const int music = db.addCategory(QStringLiteral("12 Inches"), "music", "#5cb0ff");
        const int jingle = db.addCategory(QStringLiteral("Station ID"), "jingle", "#ff9fb0");
        db.beginBatch();
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < 400; ++i) {
            Item it;
            it.categoryId = music;
            it.path = QStringLiteral("/zene/12inch-%1.mp3").arg(i);
            it.title = QStringLiteral("Mix %1").arg(i);
            it.artist = QStringLiteral("Előadó %1").arg(i % 120);
            it.durationMs = 300000 + rng->bounded(210000);
            db.addItem(it);
        }
        for (int i = 0; i < 4; ++i) {
            Item it;
            it.categoryId = jingle;
            it.path = QStringLiteral("/zene/id-%1.mp3").arg(i);
            it.title = QStringLiteral("Station ID %1").arg(i);
            it.durationMs = 60000 + i * 2000;
            db.addItem(it);
        }
        db.endBatch();
        db.notifyItemsChanged();
        const int clock = db.addClock(QStringLiteral("12Inches"), "#5cb0ff");
        for (int half = 0; half < 2; ++half) {
            ClockSlot s;
            s.clockId = clock;
            s.type = "category";
            s.categoryId = jingle;
            s.fixedOffsetS = half * 1800;
            db.addSlot(s);
            s = ClockSlot();
            s.clockId = clock;
            s.categoryId = music;
            if (mode == QLatin1String("fill")) s.type = "fill";
            else { s.type = "category"; s.count = 5; s.upToFit = mode == QLatin1String("fit"); }
            db.addSlot(s);
        }
        for (int d = 0; d < 7; ++d)
            for (int h = 0; h < 24; ++h) db.setGrid(d, h, clock);
        if (qEnvironmentVariableIsSet("ETER_FILL_TOL")) db.setSetting("fill_tolerance_s", qEnvironmentVariable("ETER_FILL_TOL"));
        if (qEnvironmentVariableIsSet("ETER_FILL_SHOT")) {
            // Az óraséma-szerkesztő: túlcsordulás-figyelmeztetés, majd a javítás gomb.
            window.resize(1440, 1000);
            window.schedulerPage()->refreshAll();
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == TR("Ütemező")) b->click();
            QTimer::singleShot(500, &window, [&window, &db, clock] {
                window.grab().save("fill_before.png");
                for (QPushButton *b : window.schedulerPage()->findChildren<QPushButton *>())
                    if (b->text() == TR("Javítás: kitöltés a fix pontig") && b->isVisible()) {
                        std::printf("javítás gomb látszik, megnyomva\n");
                        b->click();
                    }
                QStringList types;
                for (const ClockSlot &s : db.clockSlots(clock)) types << s.type + (s.upToFit ? "/fit" : "");
                std::printf("sorok javítás után: %s\n", qPrintable(types.join(", ")));
                QTimer::singleShot(400, &window, [&window] {
                    window.grab().save("fill_after.png");
                    QApplication::exit(0);
                });
            });
            return app.exec();
        }
        Scheduler sched(&db);
        const QDateTime from(eter::now().date().addDays(1), QTime(0, 0));
        const GenerateResult res = sched.generate(from, 24, true);
        // Eltérések: a fix pontig tervezett idő és a fix pont között (pozitív: túlcsordul, negatív: üres idő marad).
        QMap<int, QVector<const Entry *>> byHour;
        const QVector<Entry> en = db.pendingEntries();
        for (const Entry &e : en) byHour[e.playlistKey] << &e;
        QVector<qint64> diffs;
        int songs = 0;
        for (auto it = byHour.begin(); it != byHour.end(); ++it) {
            qint64 t = 0;
            for (const Entry *e : it.value()) {
                if (e->fixedStartS > 0) diffs << t - qint64(e->fixedStartS) * 1000;
                if (e->fixedStartS >= 0) t = std::max(t, qint64(e->fixedStartS) * 1000);
                t += e->item.airMs();
                if (e->item.kind == QLatin1String("music")) ++songs;
            }
            diffs << t - 3600000;                          // az óra vége (a következő :00)
        }
        std::sort(diffs.begin(), diffs.end(), [](qint64 a, qint64 b) { return std::llabs(a) < std::llabs(b); });
        qint64 within15 = 0, within60 = 0, sumAbs = 0;
        for (qint64 d : diffs) {
            within15 += std::llabs(d) <= 15000;
            within60 += std::llabs(d) <= 60000;
            sumAbs += std::llabs(d);
        }
        std::printf("mód: %s · %d óra, %d zene · fix pontok: %d\n", qPrintable(mode), res.hours, songs, int(diffs.size()));
        std::printf("  ±15 mp-en belül: %lld · ±60 mp-en belül: %lld · átlagos eltérés: %.1f mp · legrosszabb: %+.1f mp\n",
                    (long long)within15, (long long)within60, diffs.isEmpty() ? 0.0 : sumAbs / 1000.0 / diffs.size(),
                    diffs.isEmpty() ? 0.0 : diffs.last() / 1000.0);
        QStringList sample;
        for (int i = 0; i < std::min<int>(8, diffs.size()); ++i) sample << QString::number(diffs[diffs.size() - 1 - i] / 1000.0, 'f', 1);
        std::printf("  a legnagyobb eltérések (mp): %s\n", qPrintable(sample.join(", ")));
        std::fflush(stdout);
        QTimer::singleShot(0, &window, [] { QApplication::exit(0); });
    }
    if (parser.isSet(freshTestOpt)) {
        // Frissen importált fájl kézzel a playlistbe, adás közben (hibakereséshez). Fájlok: az első zene (előre a
        // hangtárban, ez szól), a többit az importáló ablakon át a „Hírek” kategóriába hozza be, és a szóló elem után
        // szúrja be (ETER_IMP_HOW: append = a playlist végére, player = a C lejátszóra ejtve, alapból a szóló után).
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int news = db.addCategory(QStringLiteral("Hírek"), "news", "#ff8f85");
        Item first;
        first.categoryId = cat;
        first.path = QFileInfo(files[0]).absoluteFilePath();
        first.title = QStringLiteral("Zene");
        {
            eter::Decoder dec;
            if (dec.open(files[0].toStdString())) first.durationMs = dec.durationFrames() * 1000 / eter::kRate;
        }
        QVector<int> pre{db.addItem(first)};
        // ETER_IMP_PRE=n: az első n fájl előre a hangtárban és a playlistben (zene), a többi importálva.
        const int npre = std::max(1, qEnvironmentVariableIntValue("ETER_IMP_PRE"));
        for (int i = 1; i < npre && i < files.size(); ++i) {
            Item it = first;
            it.path = QFileInfo(files[i]).absoluteFilePath();
            it.title = QFileInfo(files[i]).completeBaseName();
            eter::Decoder dec;
            if (dec.open(files[i].toStdString())) it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            pre << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(pre);
        window.setMode(qEnvironmentVariable("ETER_IMP_MODE") == QLatin1String("assist") ? MainWindow::Assist : MainWindow::Auto);
        if (!qEnvironmentVariableIsSet("ETER_IMP_IDLE")) QTimer::singleShot(300, &window, [&window] { window.next(); });
        const QStringList rest = files.mid(std::max(1, qEnvironmentVariableIntValue("ETER_IMP_PRE")));
        const QString how = qEnvironmentVariable("ETER_IMP_HOW");
        QTimer::singleShot(1500, &window, [&window, rest, news, how] {
            if (qEnvironmentVariableIsSet("ETER_IMP_TAB"))
                for (QToolButton *b : window.findChildren<QToolButton *>())
                    if (b->text() == qEnvironmentVariable("ETER_IMP_TAB")) b->click();
            window.library()->importPaths(rest, [&window, how](const QVector<int> &ids) {
                std::printf("importálva: %d elem · %s\n", int(ids.size()), qPrintable(window.debugState()));
                std::fflush(stdout);
                if (how == QLatin1String("none")) return;     // kézzel (például xdotool-lal) húzza be
                if (how == QLatin1String("append")) window.appendItems(ids);
                else if (how == QLatin1String("player")) {
                    window.appendItems(ids);
                } else window.appendItems(ids, qEnvironmentVariableIsSet("ETER_IMP_AT") ? qEnvironmentVariableIntValue("ETER_IMP_AT") : 1);
                std::printf("beszúrva: %s\n", qPrintable(window.debugState()));
                std::fflush(stdout);
            });
            QTimer::singleShot(300, &window, [news] {
                for (QWidget *w : QApplication::topLevelWidgets()) {
                    auto *dlg = qobject_cast<ImportDialog *>(w);
                    if (!dlg) continue;
                    auto *combo = dlg->findChild<QComboBox *>();
                    combo->setCurrentIndex(combo->findData(news));
                    for (QPushButton *b : dlg->findChildren<QPushButton *>())
                        if (b->isEnabled() && b->text() == TR("Importálás")) b->click();
                }
            });
        });
        auto *sampler = new QTimer(&window);
        QObject::connect(sampler, &QTimer::timeout, &window, [&window] {
            static int k = 0;
            std::printf("  %4.1f s  %s\n", ++k * 0.5, qPrintable(window.debugState()));
            std::fflush(stdout);
        });
        sampler->start(500);
        QTimer::singleShot(qEnvironmentVariableIntValue("ETER_IMP_SECS") > 0 ? qEnvironmentVariableIntValue("ETER_IMP_SECS") * 1000 : 30000,
                           &window, [] { QApplication::exit(0); });
    }
    if (parser.isSet(envTestOpt)) {
        // Hangerőgörbe: (1) a Mix editorban egérrel két pont a hírek szignálon (eleje 0 dB, a többi −12 dB), mentés;
        // (2) adásban a lejátszó csúcsszintje követi-e; (3) az elemszerkesztőben egy pont törlése és mentése.
        // Fájlok: zene szignál zene.
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jcat = db.addCategory(QStringLiteral("Hírek"), "news", "#ff8f85");
        QVector<int> ids;
        for (int i = 0; i < files.size(); ++i) {
            eter::Decoder dec;
            if (!dec.open(files[i].toStdString())) continue;
            Item it;
            it.categoryId = i == 1 ? jcat : cat;
            it.path = QFileInfo(files[i]).absoluteFilePath();
            it.title = i == 1 ? QStringLiteral("Hírek szignál") : QFileInfo(files[i]).completeBaseName();
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        const QVector<Entry> en = db.pendingEntries();
        QTimer::singleShot(400, &window, [&window, &db, en, ids] {
            // A Mix editor: a szignál → zene átmenet; a szignál az első sáv.
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == TR("Mix editor")) b->click();
            window.mixPage()->selectEntries(en[1].id, en[2].id);
            QApplication::processEvents();
            MixTimeline *t = window.mixPage()->timeline();
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == TR("Hangerőgörbe")) b->setChecked(true);
            QApplication::processEvents();
            auto mouse = [t](QEvent::Type type, QPointF pt, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent ev(type, pt, t->mapToGlobal(pt), b, bs, Qt::NoModifier);
                QApplication::sendEvent(t, &ev);
            };
            const QRectF lane = t->laneRect(0);
            const double y0 = lane.top() + 30;                     // a 0 dB-es vonal
            const double x1 = t->xAt(t->clipStart(0) + 1500 - t->cue(0)), x2 = t->xAt(t->clipStart(0) + 2500 - t->cue(0));
            mouse(QEvent::MouseButtonPress, {x1, y0 + 20}, Qt::LeftButton, Qt::LeftButton);   // 1. pont: a görbén marad (0 dB)
            mouse(QEvent::MouseButtonRelease, {x1, y0 + 20}, Qt::LeftButton, Qt::NoButton);
            mouse(QEvent::MouseButtonPress, {x2, y0}, Qt::LeftButton, Qt::LeftButton);         // 2. pont, lehúzva −12 dB-re
            const double yTarget = y0 + 12.0 / 40.0 * ((lane.bottom() - 8) - y0);
            mouse(QEvent::MouseMove, {x2, y0 + 10}, Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseMove, {x2, yTarget}, Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {x2, yTarget}, Qt::LeftButton, Qt::NoButton);
            // Egy harmadik pont, majd törlése jobb gombbal
            const double x3 = t->xAt(t->clipStart(0) + 3500 - t->cue(0));
            mouse(QEvent::MouseButtonPress, {x3, y0}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {x3, y0}, Qt::LeftButton, Qt::NoButton);
            const int before = t->envelope(0).size();
            const double y3 = y0 + 12.0 / 40.0 * ((lane.bottom() - 8) - y0);
            mouse(QEvent::MouseButtonPress, {x3, y3}, Qt::RightButton, Qt::RightButton);
            std::printf("Mix editor görbe: %s (törlés előtt %d pont)\n", qPrintable(env::toString(t->envelope(0))), before);
            window.grab().save("env_mix.png");
            for (QPushButton *b : window.mixPage()->findChildren<QPushButton *>())
                if (b->text() == TR("Mentés")) b->click();
            std::printf("mentve az elemhez: „%s”\n", qPrintable(db.item(ids[1]).volumeEnv));
            QSqlQuery q("SELECT COUNT(*) FROM transitions");
            q.next();
            std::printf("mentett átmenetek (a görbe nem tesz egyedi mixet): %d\n", q.value(0).toInt());
            for (QToolButton *b : window.findChildren<QToolButton *>())
                if (b->text() == TR("Adás")) b->click();
            window.setMode(MainWindow::Auto);
            window.next();
            QTimer::singleShot(300, &window, [&window] { window.next(); });   // rögtön a szignál
        });
        // Adásban: a szignál csúcsszintje 0,5 mp-enként (a szinusz −27 dB-es).
        auto *e = window.engine();
        auto st = std::make_shared<QStringList>();
        auto *sampler = new QTimer(&window);
        QObject::connect(sampler, &QTimer::timeout, &window, [e, st, &window] {
            const QString s = window.debugState();
            for (auto ch : {eter::Engine::Player1, eter::Engine::Player2, eter::Engine::Player3}) {
                auto &src = e->source(ch);
                float l, r;
                src.takePeaks(l, r);
                if (!src.playing()) continue;
                const QString path = QString::fromStdString(src.path());
                if (!path.contains("jingle")) continue;
                const double pos = src.position() / double(eter::kRate);
                st->append(QStringLiteral("%1 s: %2 dB").arg(pos, 0, 'f', 1).arg(l > 0 ? 20 * std::log10(l) : -99, 0, 'f', 1));
            }
        });
        sampler->start(500);
        QTimer::singleShot(9000, &window, [st, &window, &db, ids] {
            std::printf("a szignál szintje adásban:\n  %s\n", qPrintable(st->join("\n  ")));
            // Elemszerkesztő: a görbe ott is látszik; a második pont törlése dupla kattintással, mentés.
            ItemEditorDialog *dlg = window.openItemEditor(ids[1]);
            QTimer::singleShot(1500, dlg, [dlg, &db, ids] {
                dlg->setEnvelopeEditing(true);
                QApplication::processEvents();
                MarkerWave *w = dlg->wave();
                std::printf("elemszerkesztő görbe: %s\n", qPrintable(env::toString(w->envelope())));
                dlg->grab().save("env_item.png");
                const QPointF pt = w->envPointPos(1);
                QMouseEvent dc(QEvent::MouseButtonDblClick, pt, w->mapToGlobal(pt), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(w, &dc);
                dlg->save();
                std::printf("az elemszerkesztőben törölve, mentve: „%s”\n", qPrintable(db.item(ids[1]).volumeEnv));
                QApplication::exit(0);
            });
        });
    }
    if (parser.isSet(dbTestOpt)) {
        // Adatbázis export és import: export → módosítás → hibás fájlok elutasítása → import előkészítése →
        // újraindulás (ETER_DBTEST_RELAUNCH: az új példány argumentumai). Az eredményt a hívó ellenőrzi.
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        QVector<int> ids;
        for (const QString &f : files) {
            Item it;
            it.categoryId = cat;
            it.path = QFileInfo(f).absoluteFilePath();
            it.title = QFileInfo(f).completeBaseName();
            ids << db.addItem(it);
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        auto count = [] {
            QSqlQuery q("SELECT COUNT(*) FROM items");
            return q.next() ? q.value(0).toInt() : -1;
        };
        const QString dir = qEnvironmentVariable("ETER_DBTEST_DIR", QDir::tempPath());
        const QString exp = dir + QStringLiteral("/export 'próba'.sqlite");
        QString err;
        const bool ok = window.exportDatabaseTo(exp, &err);
        std::printf("export: %s · %lld bájt · elemek: %d %s\n", ok ? "ok" : "HIBA", (long long)QFileInfo(exp).size(), count(), qPrintable(err));
        std::printf("összegzés: %s\n", qPrintable(window.databaseSummary().replace('\n', " | ")));
        Item extra;
        extra.categoryId = cat;
        extra.path = QStringLiteral("/nincs/ilyen.mp3");
        extra.title = QStringLiteral("csak a régiben");
        db.addItem(extra);
        std::printf("módosítás után elemek: %d\n", count());
        // Hibás fájlok
        QFile bad(dir + QStringLiteral("/nem-adatbazis.sqlite"));
        bad.open(QIODevice::WriteOnly);
        bad.write("ez csak szöveg");
        bad.close();
        QString e1, e2;
        const bool r1 = window.stageDatabaseImport(bad.fileName(), &e1);
        std::printf("szövegfájl importja: %s (%s)\n", r1 ? "ELFOGADVA" : "elutasítva", qPrintable(e1.replace('\n', ' ')));
        const QString newer = dir + QStringLiteral("/ujabb.sqlite");
        QFile::copy(exp, newer);
        {
            QSqlDatabase n = QSqlDatabase::addDatabase("QSQLITE", "newer");
            n.setDatabaseName(newer);
            n.open();
            QSqlQuery(n).exec(QStringLiteral("PRAGMA user_version = 99"));
            n.close();
        }
        QSqlDatabase::removeDatabase("newer");
        const bool r2 = window.stageDatabaseImport(newer, &e2);
        std::printf("újabb séma importja: %s (%s)\n", r2 ? "ELFOGADVA" : "elutasítva", qPrintable(e2.replace('\n', ' ')));
        const QString older = qEnvironmentVariable("ETER_DBTEST_OLD");
        if (!older.isEmpty()) {
            QString e4;
            const bool r4 = window.stageDatabaseImport(older, &e4);
            std::printf("régi (0.18-as) adatbázis importja: %s %s\n", r4 ? "elfogadva" : "ELUTASÍTVA", qPrintable(e4));
            QFile::remove(db.path() + ".import");
        }
        QString backup, e3;
        const bool staged = window.stageDatabaseImport(exp, &e3, &backup);
        std::printf("import előkészítve: %s · biztonsági mentés: %s (%lld bájt) %s\n", staged ? "ok" : "HIBA",
                    qPrintable(QFileInfo(backup).fileName()), (long long)QFileInfo(backup).size(), qPrintable(e3));
        std::printf("várakozó fájl: %s\n", QFile::exists(db.path() + ".import") ? "van" : "NINCS");
        std::fflush(stdout);
        const QString again = qEnvironmentVariable("ETER_DBTEST_RELAUNCH");
        QTimer::singleShot(500, &window, [&window, again] {
            if (again.isEmpty()) QApplication::exit(0);
            else window.relaunch(QProcess::splitCommand(again));
        });
        if (qEnvironmentVariableIsSet("ETER_DBTEST_MODAL"))
            QTimer::singleShot(0, &window, [&window] {
                // Mint a valóságban: az újraindítás a nyitott (modális) Beállítások ablakból indul.
                SettingsDialog dlg(&window);
                dlg.exec();
                std::printf("a Beállítások ablak bezárult\n");
            });
    }
    if (parser.isSet(threeTestOpt)) {
        // Fájlok: zene1 jingle zene2 zene3 [vt]. Zene1 → jingle: a zene1 a mix pont után még 3 mp-ig kiúszik; a jingle
        // 2 mp után átad a zene2-nek (nem zene: teljes hangerővel szól tovább). Így egy ideig mindhárom lejátszó szól.
        // Zene2 → zene3: mentett mix 2 mp-nél és voice track; automatikus mixnél (ETER_MIX=auto) a mentett időzítés nem
        // érvényes, a voice track igen. ETER_THREE_MODE=assist: kézi indítás (Következő) a mix pontoknál.
        const bool assist = qEnvironmentVariable("ETER_THREE_MODE") == QLatin1String("assist");
        const int cat = db.addCategory(QStringLiteral("Zene"), "music", "#5cb0ff");
        const int jcat = db.addCategory(QStringLiteral("Szignál"), "jingle", "#f2c94c");
        const int vcat = db.addCategory(QStringLiteral("Felvezetés"), "voicetrack", "#f472b6");
        QVector<int> ids;
        int vtItem = 0;
        for (int i = 0; i < files.size(); ++i) {
            eter::Decoder dec;
            if (!dec.open(files[i].toStdString())) continue;
            Item it;
            it.categoryId = i == 1 ? jcat : i == 4 ? vcat : cat;
            it.path = QFileInfo(files[i]).absoluteFilePath();
            it.title = QFileInfo(files[i]).completeBaseName();
            it.artist = i == 1 ? QString() : QStringLiteral("Előadó %1").arg(i + 1);
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            const int id = db.addItem(it);
            if (i == 4) vtItem = id; else ids << id;
        }
        db.notifyItemsChanged();
        window.appendItems(ids);
        const QVector<Entry> en = db.pendingEntries();
        if (en.size() < 4) { std::printf("kevés fájl\n"); return 1; }
        Transition t1;
        t1.fromEntryId = en[0].id;
        t1.toEntryId = en[1].id;
        t1.mixOutMs = 5000;
        t1.outEndMs = 8000;
        t1.fadeOutMs = 3000;
        db.saveTransition(t1);
        Transition t2;
        t2.fromEntryId = en[1].id;
        t2.toEntryId = en[2].id;
        t2.mixOutMs = 2000;
        t2.fadeOutMs = 0;                                 // a jingle a saját végéig szól
        db.saveTransition(t2);
        Transition t3;
        t3.fromEntryId = en[2].id;
        t3.toEntryId = en[3].id;
        t3.mixOutMs = 2000;
        t3.fadeOutMs = 1000;
        t3.vtItemId = vtItem;
        t3.vtOffsetMs = 0;
        t3.vtDuckDb = -9;
        db.saveTransition(t3);
        emit window.mixPage()->mixSaved({});
        std::printf("átmenetek: %s · mód: %s\n", window.autoMix() ? "automatikus mix" : "Mix editor", assist ? "asszisztált" : "automata");
        if (qEnvironmentVariableIsSet("ETER_NOW_FILE")) {
            // „Most szól” kiírás: minden változásnál a fájl tartalma (és ha van, az Icecast-próbaszerver).
            NowPlaying::Config nc;
            nc.fileEnabled = true;
            nc.path = qEnvironmentVariable("ETER_NOW_FILE");
            nc.format = qEnvironmentVariable("ETER_NOW_FORMAT", QStringLiteral("%artist% - %title%"));
            const QString nm = qEnvironmentVariable("ETER_NOW_NONMUSIC");
            nc.nonMusic = nm == QLatin1String("own") ? NowPlaying::OwnTitle : nm == QLatin1String("text") ? NowPlaying::FixedText : NowPlaying::KeepLast;
            nc.nonMusicText = QStringLiteral("Éter Rádió");
            nc.idleText = QStringLiteral("Éter Rádió – szünet");
            nc.icecastEnabled = qEnvironmentVariableIsSet("ETER_NOW_ICECAST");
            nc.icecastUrl = qEnvironmentVariable("ETER_NOW_ICECAST");
            nc.icecastMount = QStringLiteral("/élő+adás");
            nc.icecastUser = QStringLiteral("source");
            nc.icecastPassword = QStringLiteral("hackme");
            NowPlaying::saveConfig(nc);
            window.configureNowPlaying();
            if (qEnvironmentVariableIsSet("ETER_NOW_SHOT"))
                QTimer::singleShot(4000, &window, [&window] {
                    auto *dlg = new SettingsDialog(&window);
                    dlg->resize(820, 860);
                    dlg->show();
                    if (auto *tabs = dlg->findChild<QTabWidget *>("settingsTabs"))
                        for (int i = 0; i < tabs->count(); ++i)
                            if (tabs->tabText(i) == qEnvironmentVariable("ETER_SHOT_TAB", TR("Most szól"))) tabs->setCurrentIndex(i);
                    QTimer::singleShot(1500, dlg, [dlg] {
                        dlg->grab().save(qEnvironmentVariable("ETER_NOW_SHOT"));
                        dlg->close();
                    });
                });
            QObject::connect(window.nowPlaying(), &NowPlaying::changed, &window, [nc](const QString &text) {
                QFile f(nc.path);
                f.open(QIODevice::ReadOnly);
                std::printf("  MOST SZÓL: „%s” (fájlban: „%s”)\n", qPrintable(text), f.readAll().constData());
            });
        }
        window.setMode(assist ? MainWindow::Assist : MainWindow::Auto);
        auto *e = window.engine();
        struct Stat { int maxPlayers = 0; double threeAt = -1, vtAt = -1; qint64 t0 = 0; bool shot = false; QString threeView; };
        auto st = std::make_shared<Stat>();
        QTimer::singleShot(250, &window, [&window] {
            std::printf("indulás előtt   %s\n   %s\n   sorok: %s\n", qPrintable(window.debugState()), qPrintable(window.playerViewDebug()),
                        qPrintable(window.rowStatusDebug()));
            window.grab().save("three_pending.png");
        });
        QTimer::singleShot(300, &window, [&window, st] {
            st->t0 = QDateTime::currentMSecsSinceEpoch();
            window.next();
        });
        QTimer::singleShot(300 + 2000, &window, [&window] {
            window.grab().save("three_playing.png");
            std::printf("  2 mp: %s\n", qPrintable(window.playerViewDebug()));
        });
        if (assist) {
            QTimer::singleShot(300 + 5000, &window, [&window] { std::printf("  kézi Következő (jingle)\n"); window.next(); });
            QTimer::singleShot(300 + 7000, &window, [&window] { std::printf("  kézi Következő (zene2)\n"); window.next(); });
        }
        auto *sampler = new QTimer(&window);
        QObject::connect(sampler, &QTimer::timeout, &window, [e, st, &window] {
            if (!st->t0) return;
            const double t = (QDateTime::currentMSecsSinceEpoch() - st->t0) / 1000.0;
            int n = 0;
            for (auto ch : {eter::Engine::Player1, eter::Engine::Player2, eter::Engine::Player3}) n += e->source(ch).playing();
            st->maxPlayers = std::max(st->maxPlayers, n);
            if (n == 3 && st->threeAt < 0) {
                st->threeAt = t;
                st->threeView = window.playerViewDebug();
            }
            if (n == 3 && !st->shot && t > st->threeAt + 0.3) {
                st->shot = true;
                window.grab().save("three_live.png");
                std::printf("  három egyszerre: %s\n   %s\n   sorok: %s\n", qPrintable(window.debugState()), qPrintable(window.playerViewDebug()),
                            qPrintable(window.rowStatusDebug()));
            }
            if (e->source(eter::Engine::Vt).playing() && st->vtAt < 0) {
                st->vtAt = t;
                QTimer::singleShot(400, &window, [&window] {
                    window.grab().save("three_vt.png");
                    std::printf("  voice track szól: %s\n   %s\n", qPrintable(window.debugState()), qPrintable(window.playerViewDebug()));
                });
            }
            static int k = 0;
            if (++k % 10 == 0) std::printf("  %5.2f s  (%d lejátszó szól)  %s\n", t, n, qPrintable(window.debugState()));
        });
        sampler->start(50);
        const int endMs = qEnvironmentVariableIntValue("ETER_THREE_SECS") > 0 ? qEnvironmentVariableIntValue("ETER_THREE_SECS") * 1000 : 24000;
        QTimer::singleShot(300 + endMs, &window, [st, &db] {
            std::printf("legtöbb egyszerre szóló lejátszó: %d · először: %.2f s (%s)\n", st->maxPlayers, st->threeAt, qPrintable(st->threeView));
            std::printf("voice track indul: %.2f s\n", st->vtAt);
            QSqlQuery q("SELECT title, player, end_reason FROM play_log ORDER BY id");
            while (q.next())
                std::printf("  napló: %-8s lejátszó %d · %s\n", qPrintable(q.value(0).toString()), q.value(1).toInt(), qPrintable(q.value(2).toString()));
            Q_UNUSED(db);
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
                if (w->isVisible() && w != &window && qobject_cast<QDialog *>(w)) {
                    w->grab().save(QString(out).replace(".png", QStringLiteral("-%1.png").arg(++n)));
                    if (auto *tabs = w->findChild<QTabWidget *>("settingsTabs"))       // a Beállítások minden lapja
                        for (int t = 1; t < tabs->count(); ++t) {
                            tabs->setCurrentIndex(t);
                            w->grab().save(QString(out).replace(".png", QStringLiteral("-%1-%2.png").arg(n).arg(t)));
                        }
                }
            QApplication::exit(0);
        });
    }
    return QCoreApplication::exec();
}
