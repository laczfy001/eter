#include "FileCheck.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

struct FileCheck::Shared {
    std::mutex m;
    std::condition_variable cv;
    QStringList queue;
    bool pending = false, quit = false;
    std::atomic<bool> working{false};
    QPointer<FileCheck> owner;                 // csak a felület szálán olvasható
};

FileCheck::FileCheck(QObject *parent) : QObject(parent), s_(std::make_shared<Shared>())
{
    s_->owner = this;
    std::thread([sh = s_] {
        for (;;) {
            QStringList paths;
            {
                std::unique_lock<std::mutex> lock(sh->m);
                sh->cv.wait(lock, [&] { return sh->pending || sh->quit; });
                if (sh->quit) return;
                paths = sh->queue;
                sh->pending = false;
                sh->working = true;
            }
            QSet<QString> missing;
            QStringList done;
            for (int i = 0; i < paths.size(); ++i) {
                {
                    std::lock_guard<std::mutex> lock(sh->m);
                    if (sh->quit) return;
                    if (sh->pending) break;                 // újabb kérés jött: azzal folytatja
                }
                if (!reachable(paths[i])) missing.insert(paths[i]);
                done << paths[i];
                if ((i + 1) % 50 == 0 || i + 1 == paths.size()) {
                    const int n = i + 1, total = int(paths.size());
                    QMetaObject::invokeMethod(qApp, [sh, n, total] { if (sh->owner) emit sh->owner->progress(n, total); }, Qt::QueuedConnection);
                }
            }
            sh->working = false;
            std::lock_guard<std::mutex> lock(sh->m);
            if (sh->quit) return;
            QMetaObject::invokeMethod(qApp, [sh, done, missing] { if (sh->owner) emit sh->owner->finished(done, missing); },
                                      Qt::QueuedConnection);
        }
    }).detach();
}

FileCheck::~FileCheck()
{
    std::lock_guard<std::mutex> lock(s_->m);
    s_->quit = true;
    s_->cv.notify_all();
}

void FileCheck::check(const QStringList &paths)
{
    std::lock_guard<std::mutex> lock(s_->m);
    s_->queue = paths;
    s_->pending = true;
    s_->cv.notify_all();
}

bool FileCheck::busy() const
{
    std::lock_guard<std::mutex> lock(s_->m);
    return s_->pending || s_->working;
}

bool FileCheck::reachable(const QString &path)
{
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile() && fi.isReadable() && fi.size() > 0;
}
