// Adásrögzítés (logger): a kimenő adás folyamatos felvétele óránkénti MP3 fájlokba, a régi fájlok törlésével.
// A felület szála adja át a hangot (feed); a kódolás és az írás külön szálon fut, hogy a lemez ne akassza az adást.
#pragma once
#include <QDateTime>
#include <QString>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class ProgramLogger {
public:
    struct Config {
        bool enabled = false;
        QString folder;
        int kbps = 128;
        int keepDays = 30;
    };
    ProgramLogger();
    ~ProgramLogger();
    void configure(const Config &c);
    const Config &config() const { return cfg_; }
    void feed(const float *stereo, int frames);        // 48 kHz sztereó, a felület szálán
    bool running() const;
    QString currentFile() const;
    QString lastError() const;
    qint64 writtenBytes() const;
    static QString defaultFolder();
    static QString fileNameFor(const QDateTime &hour); // „Eter-2026-10-16-1400.mp3”

private:
    struct Worker;
    std::shared_ptr<Worker> w_;
    Config cfg_;
};
