#include "i18n.h"
#include "LibraryAnalyzer.h"

#include "audio/Analysis.h"
#include "core/MusicKey.h"
#include "db/Database.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

static void lowerThreadPriority()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // a lemezhez is háttérprioritással fér
#else
#if defined(__linux__)
    // Linuxon a szál saját „nice” értéke és ütemezési osztálya: csak akkor kap processzort, ha más nem kéri.
    const pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
    setpriority(PRIO_PROCESS, static_cast<id_t>(tid), 19);
#ifdef SCHED_IDLE
    sched_param p{};
    p.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_IDLE, &p);
#endif
#ifdef SYS_ioprio_set
    // Lemezhozzáférés: „idle” osztály (IOPRIO_CLASS_IDLE = 3, IOPRIO_WHO_PROCESS = 1 a szálra).
    syscall(SYS_ioprio_set, 1, static_cast<int>(tid), 3 << 13);
#endif
#endif
#endif
}

LibraryAnalyzer::Config LibraryAnalyzer::loadConfig()
{
    QSettings s;
    Config c;
    c.enabled = s.value("analysis/enabled", true).toBool();
    c.useWindow = s.value("analysis/useWindow", false).toBool();
    c.fromHour = std::clamp(s.value("analysis/from", 1).toInt(), 0, 23);
    c.toHour = std::clamp(s.value("analysis/to", 6).toInt(), 0, 23);
    c.camelot = s.value("analysis/notation", "camelot").toString() != QLatin1String("traditional");
    return c;
}

void LibraryAnalyzer::saveConfig(const Config &c)
{
    QSettings s;
    s.setValue("analysis/enabled", c.enabled);
    s.setValue("analysis/useWindow", c.useWindow);
    s.setValue("analysis/from", c.fromHour);
    s.setValue("analysis/to", c.toHour);
    s.setValue("analysis/notation", c.camelot ? "camelot" : "traditional");
}

bool LibraryAnalyzer::inWindow(int hour, int fromHour, int toHour)
{
    if (fromHour == toHour) return true;
    if (fromHour < toHour) return hour >= fromHour && hour < toHour;
    return hour >= fromHour || hour < toHour;   // éjfélen átnyúló ablak
}

LibraryAnalyzer::LibraryAnalyzer(Database *db, QObject *parent) : QObject(parent), db_(db), timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &LibraryAnalyzer::tick);
    thread_ = std::thread([this] { worker(); });
    configure(loadConfig());
}

LibraryAnalyzer::~LibraryAnalyzer()
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        quit_ = true;
        queue_.clear();
    }
    cancel_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LibraryAnalyzer::configure(const Config &c)
{
    cfg_ = c;
    mkey::camelotNotation() = c.camelot;
    refreshCounts();
    timer_->start(busy_ ? 3000 : 1500);
    emit statusChanged();
}

void LibraryAnalyzer::setPaused(bool on)
{
    if (paused_ == on) return;
    paused_ = on;
    if (on && busy_) cancel_.store(true);
    timer_->start(1000);
    emit statusChanged();
}

void LibraryAnalyzer::restart()
{
    db_->resetAnalysis();
    skip_.clear();
    lastError_.clear();
    refreshCounts();
    timer_->start(500);
    emit statusChanged();
}

void LibraryAnalyzer::refreshCounts()
{
    int p = 0, t = 0;
    db_->analysisCounts(kVersion, &p, &t);
    pending_ = p;
    total_ = t;
}

void LibraryAnalyzer::tick()
{
    const bool allowed = cfg_.enabled && !paused_
                         && (!cfg_.useWindow || inWindow(QDateTime::currentDateTime().time().hour(), cfg_.fromHour, cfg_.toHour));
    if (!allowed) {
        if (busy_) cancel_.store(true);   // az időablak lejárt vagy kikapcsolták: a folyamatban lévő is álljon le
        timer_->start(30000);
        emit statusChanged();
        return;
    }
    if (busy_) {
        timer_->start(3000);
        return;
    }
    const QVector<AnalysisJob> jobs = db_->analysisJobs(kVersion, 64 + skip_.size());
    for (const AnalysisJob &j : jobs) {
        if (skip_.contains(j.id)) continue;
        if (!QFileInfo::exists(j.path)) {
            skip_.insert(j.id);   // most nem érhető el; a következő indításkor (vagy újraelemzéskor) újra megpróbálja
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cancel_.store(false);
            queue_.push_back({j.id, j.path.toStdString(), j.needBpm, j.needKey});
        }
        busy_ = true;
        cv_.notify_one();
        emit statusChanged();
        timer_->start(3000);
        return;
    }
    // Nincs több teendő: ritkán nézünk rá (az új importokat így is felveszi).
    refreshCounts();
    timer_->start(20000);
    emit statusChanged();
}

void LibraryAnalyzer::worker()
{
    lowerThreadPriority();
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            job = queue_.front();
            queue_.pop_front();
        }
        QElapsedTimer clock;
        clock.start();
        double bpm = 0;
        int key = 0;
        if (job.bpm && !cancel_.load()) bpm = eter::estimateBpm(job.path, cancel_);
        if (job.key && !cancel_.load()) key = eter::estimateKey(job.path, cancel_);
        const qint64 ms = clock.elapsed();
        QMetaObject::invokeMethod(this, [this, id = job.id, bpm, key, ms] { finished(id, bpm, key, ms); }, Qt::QueuedConnection);
    }
}

void LibraryAnalyzer::finished(int id, double bpm, int key, qint64 ms)
{
    busy_ = false;
    if (cancel_.exchange(false)) {
        // Megszakítva: nem jelöljük elemzettnek, legközelebb újra sorra kerül.
        timer_->start(1000);
        emit statusChanged();
        return;
    }
    db_->setItemAnalysis(id, kVersion, bpm, key);
    ++doneSession_;
    spentMs_ += ms;
    refreshCounts();
    // A hangtár nézete nem frissül minden egyes elem után (nagy hangtárnál az is terhelés lenne).
    static qint64 lastNotify = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pending_ == 0 || now - lastNotify > 15000) {
        lastNotify = now;
        db_->notifyItemsChanged();
    }
    timer_->start(2000);   // szünet két elem között
    emit statusChanged();
}

QString LibraryAnalyzer::status() const
{
    if (!cfg_.enabled) return TR("Kikapcsolva · elemzésre vár: %1 / %2 zene").arg(pending_).arg(total_);
    if (total_ == 0) return TR("A hangtárban még nincs zene.");
    if (pending_ == 0) return TR("Kész: mind a(z) %1 zene elemezve.").arg(total_);
    QString left = TR("hátra: %1 zene").arg(pending_);
    if (doneSession_ >= 3) {
        const qint64 perItem = spentMs_ / doneSession_ + 2000;
        const qint64 mins = perItem * pending_ / 60000;
        left += mins >= 120 ? TR(", kb. %1 óra").arg((mins + 30) / 60) : TR(", kb. %1 perc").arg(std::max<qint64>(1, mins));
    }
    if (paused_) return TR("Szünetel · %1").arg(left);
    if (cfg_.useWindow && !inWindow(QDateTime::currentDateTime().time().hour(), cfg_.fromHour, cfg_.toHour))
        return TR("Az időablakra vár (%1:00 – %2:00) · %3").arg(cfg_.fromHour).arg(cfg_.toHour).arg(left);
    QString s = TR("Elemez · %1").arg(left);
    if (!skip_.isEmpty()) s += TR(" · %1 fájl most nem érhető el").arg(skip_.size());
    return s;
}
