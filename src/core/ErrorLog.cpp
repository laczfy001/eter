#include "core/ErrorLog.h"

#include "core/CrashReport.h"
#include "i18n.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QRecursiveMutex>
#include <QRegularExpression>
#include <QOperatingSystemVersion>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextDocument>
#include <QTimer>

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace elog {
namespace {

QRecursiveMutex g_mutex;   // rekurzív: a naplózás közbeni Qt-figyelmeztetés is ide jön
QString g_dir, g_role, g_marker;
QDate g_day;
QFile *g_file = nullptr;
bool g_active = false;
bool g_shutdown = false;
QtMessageHandler g_prevHandler = nullptr;
qint64 g_started = 0;

struct Seen {
    qint64 lastMs = 0;
    int suppressed = 0;
};
QHash<QString, Seen> g_seen;

QString levelName(Level l)
{
    // Egyforma szélességű jelölés (a nyelv szerint), hogy az oszlopok egymás alá essenek.
    QString n;
    switch (l) {
    case Info: n = TR("INFO"); break;
    case Warning: n = TR("FIGYELMEZTETÉS"); break;
    case Error: n = TR("HIBA"); break;
    }
    return n.leftJustified(14);
}

bool writable(const QString &dir)
{
    if (!QDir().mkpath(dir)) return false;
    QFile probe(QDir(dir).filePath(QStringLiteral(".eter-write-test")));
    if (!probe.open(QIODevice::WriteOnly)) return false;
    probe.close();
    probe.remove();
    return true;
}

QString resolveDir()
{
    const QString env = qEnvironmentVariable("ETER_LOG_DIR");
    if (!env.isEmpty() && writable(env)) return QDir(env).absolutePath();
    QString dir;
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) dir = QFileInfo(appImage).absolutePath() + QStringLiteral("/eter-logs");
    else dir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    if (writable(dir)) return QDir(dir).absolutePath();
    // A program mappája nem írható (pl. Program Files): a felhasználó saját adatmappája.
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (fallback.isEmpty()) fallback = QDir::homePath() + QStringLiteral("/.eter");
    fallback += QStringLiteral("/logs");
    QDir().mkpath(fallback);
    return fallback;
}

QString fileFor(const QDate &d) { return QDir(g_dir).filePath(QStringLiteral("eter-%1.txt").arg(d.toString(Qt::ISODate))); }

// A mai fájl (éjfélkor új nap, új fájl; az összeomlás-jelentés is oda megy).
void ensureFile()
{
    const QDate today = QDate::currentDate();
    if (g_file && g_day == today) return;
    delete g_file;
    g_day = today;
    g_file = new QFile(fileFor(today));
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_file;
        g_file = nullptr;
        return;
    }
    crash::install(g_file->fileName());
}

void writeLine(Level level, const QString &area, const QString &text)
{
    ensureFile();
    if (!g_file) return;
    QString body = text.trimmed();
    body.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    body.replace(QLatin1Char('\n'), QLatin1String("\n                                            "));
    // A terület neve a program nyelvén (a hívók magyarul adják meg; „Felület · Cím”: csak az első rész fordul).
    QString a = area;
    const int dot = a.indexOf(QStringLiteral(" · "));
    a = dot > 0 ? i18n::text(a.left(dot).toUtf8().constData()) + a.mid(dot) : i18n::text(a.toUtf8().constData());
    const QString line = QStringLiteral("%1 %2 [%3] %4\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), levelName(level), a, body);
    g_file->write(line.toUtf8());
    g_file->flush();
}

bool processAlive(qint64 pid)
{
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0;
#endif
}

void writeMarker()
{
    QFile f(g_marker);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    const QString text = QStringLiteral("pid=%1\nstarted=%2\nheartbeat=%3\nversion=%4\n")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QDateTime::fromSecsSinceEpoch(g_started).toString(Qt::ISODate),
                                  QDateTime::currentDateTime().toString(Qt::ISODate), QStringLiteral(ETER_VERSION));
    f.write(text.toUtf8());
}

// Az előző futások életjelei: ha a folyamat már nem él, de a jel megmaradt, a program váratlanul állt le.
void checkPreviousRuns()
{
    const QDir d(g_dir);
    const QStringList markers = d.entryList({QStringLiteral("eter-%1-*.running").arg(g_role)}, QDir::Files);
    for (const QString &m : markers) {
        QFile f(d.filePath(m));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QHash<QString, QString> kv;
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString l = QString::fromUtf8(raw).trimmed();
            const int eq = l.indexOf(QLatin1Char('='));
            if (eq > 0) kv.insert(l.left(eq), l.mid(eq + 1));
        }
        f.close();
        const qint64 pid = kv.value(QStringLiteral("pid")).toLongLong();
        if (pid == QCoreApplication::applicationPid()) continue;
        if (processAlive(pid)) continue;      // egy másik, még futó példány
        const QDateTime started = QDateTime::fromString(kv.value(QStringLiteral("started")), Qt::ISODate);
        const QDateTime beat = QDateTime::fromString(kv.value(QStringLiteral("heartbeat")), Qt::ISODate);
        writeLine(Error, QStringLiteral("Váratlan kilépés"),
                  TR("Az előző futás (Éter %1, folyamat %2, indítva %3) nem rendesen ért véget: összeomlott, "
                     "a rendszer leállította, vagy elment az áram. Utolsó életjel: %4 (fél percenként frissül). "
                     "Ha összeomlás volt, a hívási lánc az akkori napi naplóban, „Éter összeomlott” címmel található.")
                      .arg(kv.value(QStringLiteral("version"), QStringLiteral("?")))
                      .arg(pid)
                      .arg(started.isValid() ? started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("?"),
                           beat.isValid() ? beat.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("?")));
        QFile::remove(d.filePath(m));
    }
}

void removeOldLogs()
{
    const QDir d(g_dir);
    const QDate limit = QDate::currentDate().addDays(-30);
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("eter-*.txt")}, QDir::Files)) {
        const QDate day = QDate::fromString(fi.completeBaseName().mid(5), Qt::ISODate);
        if (day.isValid() && day < limit) QFile::remove(fi.absoluteFilePath());
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
    else {
        const QByteArray m = msg.toLocal8Bit();
        std::fprintf(stderr, "%s\n", m.constData());
    }
    Level level;
    switch (type) {
    case QtWarningMsg: level = Warning; break;
    case QtCriticalMsg:
    case QtFatalMsg: level = Error; break;
    default: return;      // debug, info: nem hiba
    }
    // Ismert, ártalmatlan Qt-üzenet: a szerver által lezárt titkosított kapcsolatból még olvasna (a kérés ettől
    // függetlenül rendben lezajlik vagy újraindul).
    if (msg.startsWith(QLatin1String("QIODevice::read (QSslSocket): device not open"))) return;
    QString area = QStringLiteral("Qt");
    const QString cat = QString::fromLatin1(ctx.category ? ctx.category : "");
    if (!cat.isEmpty() && cat != QLatin1String("default")) area += QStringLiteral(" · ") + cat;
    // A program saját SQL-hibái „SQL hiba:” előtaggal jönnek: az adatbázishoz.
    if (msg.startsWith(QLatin1String("SQL hiba")) || msg.contains(QLatin1String("tükrözése nem sikerült"))) area = QStringLiteral("Adatbázis");
    write(level, area, msg);
}

// A figyelmeztető és hiba párbeszédablakok szövege is a naplóba kerül (felületi hibák).
class DialogWatcher : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Show) {
            if (auto *box = qobject_cast<QMessageBox *>(o)) {
                const QMessageBox::Icon icon = box->icon();
                if (icon == QMessageBox::Warning || icon == QMessageBox::Critical) {
                    QString text = box->text();
                    if (!box->informativeText().isEmpty()) text += QLatin1Char('\n') + box->informativeText();
                    if (Qt::mightBeRichText(text)) {
                        text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>")), QStringLiteral("\n"));
                        text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
                    }
                    write(icon == QMessageBox::Critical ? Error : Warning, QStringLiteral("Felület · %1").arg(box->windowTitle()), text);
                }
            }
        }
        return false;
    }
};

} // namespace

void init(const QString &role)
{
    {
        QMutexLocker<QRecursiveMutex> lock(&g_mutex);
        if (g_active) return;
        g_role = role.isEmpty() ? QStringLiteral("playout") : role;
        g_dir = resolveDir();
        g_started = QDateTime::currentSecsSinceEpoch();
        g_marker = QDir(g_dir).filePath(QStringLiteral("eter-%1-%2.running").arg(g_role).arg(QCoreApplication::applicationPid()));
        g_active = true;
        ensureFile();
        removeOldLogs();
        checkPreviousRuns();
        writeLine(Info, QStringLiteral("Indítás"),
                  TR("Éter %1 (%2) · %3 · %4 · Qt %5 · napló: %6")
                      .arg(QStringLiteral(ETER_VERSION), g_role, QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                           QString::fromLatin1(qVersion()), QDir::toNativeSeparators(g_file ? g_file->fileName() : g_dir)));
        writeMarker();
    }
    g_prevHandler = qInstallMessageHandler(messageHandler);
    // Életjel fél percenként (ebből derül ki a következő indításkor, ha a program váratlanul állt le).
    auto *beat = new QTimer(QCoreApplication::instance());
    beat->setInterval(30000);
    QObject::connect(beat, &QTimer::timeout, [] {
        QMutexLocker<QRecursiveMutex> lock(&g_mutex);
        if (g_active && !g_shutdown) writeMarker();
    });
    beat->start();
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] { shutdown(); });
    std::atexit([] { shutdown(); });
    if (qobject_cast<QApplication *>(QCoreApplication::instance()))
        QCoreApplication::instance()->installEventFilter(new DialogWatcher(QCoreApplication::instance()));
}

void shutdown()
{
    QMutexLocker<QRecursiveMutex> lock(&g_mutex);
    if (!g_active || g_shutdown) return;
    g_shutdown = true;
    const qint64 secs = QDateTime::currentSecsSinceEpoch() - g_started;
    writeLine(Info, QStringLiteral("Kilépés"),
              TR("rendben (futott %1 óra %2 perc)").arg(secs / 3600).arg((secs / 60) % 60));
    QFile::remove(g_marker);
}

void write(Level level, const QString &area, const QString &text)
{
    QMutexLocker<QRecursiveMutex> lock(&g_mutex);
    if (!g_active) return;
    // Ugyanaz az üzenet egy percen belül csak egyszer (a többit megszámolja).
    const QString key = QString::number(level) + area + QLatin1Char('\x1f') + text;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (g_seen.size() > 4000) g_seen.clear();
    Seen &s = g_seen[key];
    if (s.lastMs && now - s.lastMs < 60000) {
        ++s.suppressed;
        return;
    }
    QString t = text;
    if (s.suppressed > 0) t += TR(" (az előző bejegyzés óta még %1 alkalommal)").arg(s.suppressed);
    s.lastMs = now;
    s.suppressed = 0;
    writeLine(level, area, t);
}

void writeKeyed(Level level, const QString &area, const QString &text, const QString &key, int windowMs)
{
    QMutexLocker<QRecursiveMutex> lock(&g_mutex);
    if (!g_active) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Seen &s = g_seen[QStringLiteral("key\x1f") + key];
    if (s.lastMs && now - s.lastMs < windowMs) {
        ++s.suppressed;
        return;
    }
    QString t = text;
    if (s.suppressed > 0) t += TR(" (az előző bejegyzés óta még %1 alkalommal)").arg(s.suppressed);
    s.lastMs = now;
    s.suppressed = 0;
    writeLine(level, area, t);
}

QString directory()
{
    QMutexLocker<QRecursiveMutex> lock(&g_mutex);
    return g_dir;
}

QString currentFile()
{
    QMutexLocker<QRecursiveMutex> lock(&g_mutex);
    return g_file ? g_file->fileName() : QString();
}

bool active() { return g_active; }

} // namespace elog
