#include "ProgramLogger.h"
#include "audio/Decoder.h"
#include "db/Database.h"
#include "i18n.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <lame/lame.h>
#include <atomic>
#include <cstdio>

struct ProgramLogger::Worker {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::vector<float>> queue;
    size_t queuedFrames = 0;
    bool quit = false;
    Config cfg;
    bool reopen = false;                    // új beállítás: a mostani fájl lezárul
    std::thread thread;

    // csak a munkaszál használja
    lame_t lame = nullptr;
    FILE *file = nullptr;
    int64_t framesLeft = 0;                 // az óra végéig hátralévő képkockák
    QDateTime hour;
    std::vector<unsigned char> mp3;

    // állapot a felületnek (m alatt)
    QString current, error;
    std::atomic<qint64> written{0};

    void closeFile()
    {
        if (file) {
            if (lame) {
                const int n = lame_encode_flush(lame, mp3.data(), int(mp3.size()));
                if (n > 0) std::fwrite(mp3.data(), 1, size_t(n), file);
            }
            std::fclose(file);
            file = nullptr;
        }
        if (lame) {
            lame_close(lame);
            lame = nullptr;
        }
        std::lock_guard<std::mutex> lock(m);
        current.clear();
    }

    void setError(const QString &e)
    {
        std::lock_guard<std::mutex> lock(m);
        error = e;
    }

    bool openFile(const Config &c)
    {
        const QDateTime now = eter::now();
        hour = QDateTime(now.date(), QTime(now.time().hour(), 0));
        framesLeft = std::max<qint64>(1, now.msecsTo(hour.addSecs(3600))) * eter::kRate / 1000;
        QDir dir(c.folder);
        if (!dir.mkpath(QStringLiteral("."))) {
            setError(TR("A mappa nem hozható létre: %1").arg(c.folder));
            return false;
        }
        const QString path = dir.filePath(fileNameFor(hour));
        // Ugyanarra az órára (például újraindítás után) hozzáfűz: az MP3 keretek egymás után is lejátszhatók.
#ifdef Q_OS_WIN
        file = _wfopen(reinterpret_cast<const wchar_t *>(path.utf16()), L"ab");
#else
        file = std::fopen(QFile::encodeName(path).constData(), "ab");
#endif
        if (!file) {
            setError(TR("A fájl nem írható: %1").arg(path));
            return false;
        }
        lame = lame_init();
        lame_set_in_samplerate(lame, eter::kRate);
        lame_set_out_samplerate(lame, eter::kRate);
        lame_set_num_channels(lame, 2);
        lame_set_mode(lame, JOINT_STEREO);
        lame_set_brate(lame, c.kbps);
        lame_set_quality(lame, 5);
        lame_set_bWriteVbrTag(lame, 0);
        if (lame_init_params(lame) < 0) {
            setError(TR("Az MP3 kódoló nem indítható."));
            closeFile();
            return false;
        }
        std::lock_guard<std::mutex> lock(m);
        current = path;
        error.clear();
        return true;
    }

    void cleanup(const Config &c)
    {
        // A megőrzési időnél régebbi saját fájlok törlése (csak a logger nevű fájlok, más nem).
        static const QRegularExpression re(QStringLiteral("^Eter-(\\d{4}-\\d{2}-\\d{2})-(\\d{2})00\\.mp3$"));
        const QDate limit = eter::now().date().addDays(-std::max(1, c.keepDays));
        const QDir dir(c.folder);
        for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("Eter-*.mp3")}, QDir::Files)) {
            const auto mt = re.match(fi.fileName());
            if (!mt.hasMatch()) continue;
            const QDate d = QDate::fromString(mt.captured(1), Qt::ISODate);
            if (d.isValid() && d < limit) QFile::remove(fi.absoluteFilePath());
        }
    }

    void write(const float *pcm, int frames)
    {
        const size_t need = size_t(1.25 * frames + 7200);
        if (mp3.size() < need) mp3.resize(need);
        const int n = lame_encode_buffer_interleaved_ieee_float(lame, pcm, frames, mp3.data(), int(mp3.size()));
        if (n > 0) {
            if (std::fwrite(mp3.data(), 1, size_t(n), file) != size_t(n)) setError(TR("Írási hiba (megtelt a lemez?)"));
            else written += n;
        }
    }

    void run()
    {
        Config c;
        bool haveFile = false;
        for (;;) {
            std::vector<float> chunk;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&] { return quit || reopen || !queue.empty(); });
                if (quit) break;
                if (reopen) {
                    reopen = false;
                    c = cfg;
                    lock.unlock();
                    closeFile();
                    haveFile = false;
                    if (c.enabled) cleanup(c);
                    continue;
                }
                chunk = std::move(queue.front());
                queue.pop_front();
                queuedFrames -= chunk.size() / 2;
            }
            if (!c.enabled) continue;
            int frames = int(chunk.size() / 2);
            const float *p = chunk.data();
            while (frames > 0) {
                if (!haveFile) {
                    haveFile = openFile(c);
                    if (!haveFile) break;                      // a hibát a felület mutatja; a következő darabnál újra próbálja
                }
                // Az óra határán pontosan vált: az egyik fájl vége és a következő eleje hézag nélkül folytatódik.
                const int take = int(std::min<int64_t>(frames, framesLeft));
                write(p, take);
                p += take * 2;
                frames -= take;
                framesLeft -= take;
                if (framesLeft <= 0) {
                    closeFile();
                    haveFile = false;
                    cleanup(c);
                }
            }
        }
        closeFile();
    }
};

ProgramLogger::ProgramLogger() : w_(std::make_shared<Worker>())
{
    w_->thread = std::thread([w = w_] { w->run(); });
}

ProgramLogger::~ProgramLogger()
{
    {
        std::lock_guard<std::mutex> lock(w_->m);
        w_->quit = true;
    }
    w_->cv.notify_all();
    // A lezárás (az utolsó keretek kiírása) megvárandó, különben a fájl vége elveszne.
    if (w_->thread.joinable()) w_->thread.join();
}

void ProgramLogger::configure(const Config &c)
{
    cfg_ = c;
    cfg_.kbps = std::clamp(c.kbps, 32, 320);
    std::lock_guard<std::mutex> lock(w_->m);
    w_->cfg = cfg_;
    w_->reopen = true;
    if (!c.enabled) {
        w_->queue.clear();
        w_->queuedFrames = 0;
        w_->error.clear();
    }
    w_->cv.notify_all();
}

void ProgramLogger::feed(const float *stereo, int frames)
{
    if (!cfg_.enabled || frames <= 0) return;
    std::lock_guard<std::mutex> lock(w_->m);
    if (w_->queuedFrames > size_t(eter::kRate) * 120) return;     // a lemez nagyon lassú: inkább kimarad, mint hogy elfogyjon a memória
    w_->queue.emplace_back(stereo, stereo + size_t(frames) * 2);
    w_->queuedFrames += size_t(frames);
    w_->cv.notify_all();
}

bool ProgramLogger::running() const
{
    std::lock_guard<std::mutex> lock(w_->m);
    return cfg_.enabled && !w_->current.isEmpty();
}

QString ProgramLogger::currentFile() const
{
    std::lock_guard<std::mutex> lock(w_->m);
    return w_->current;
}

QString ProgramLogger::lastError() const
{
    std::lock_guard<std::mutex> lock(w_->m);
    return w_->error;
}

qint64 ProgramLogger::writtenBytes() const { return w_->written.load(); }

QString ProgramLogger::defaultFolder()
{
    QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (music.isEmpty()) music = QDir::homePath();
    return QDir(music).filePath(QStringLiteral("Eter-logger"));
}

QString ProgramLogger::fileNameFor(const QDateTime &hour)
{
    return QStringLiteral("Eter-") + hour.date().toString(Qt::ISODate) + QLatin1Char('-')
           + QStringLiteral("%1").arg(hour.time().hour(), 2, 10, QLatin1Char('0')) + QStringLiteral("00.mp3");
}
