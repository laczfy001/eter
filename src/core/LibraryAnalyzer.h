#pragma once
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class Database;
class QTimer;

// A hangtár háttérelemzése: a zenék hiányzó tempóját (BPM) és hangnemét becsüli, egyenként, a legkisebb
// prioritású szálon, két elem között szünettel, hogy az adást ne zavarja. Csak az üres mezőt tölti ki; ami
// kézzel vagy a fájl címkéjéből jött, azt soha nem írja felül. A beállítás: Beállítások → Elemzés.
class LibraryAnalyzer : public QObject {
    Q_OBJECT
public:
    // Ha az elemzés módszere változik, ezt kell növelni: a régebbi verzióval már megnézett, de üresen maradt
    // mezőket akkor újra megpróbálja.
    static constexpr int kVersion = 1;

    struct Config {
        bool enabled = true;
        bool useWindow = false;   // csak egy napi időablakban dolgozik
        int fromHour = 1, toHour = 6;
        bool camelot = true;      // a hangnem jelölése: Camelot (8A) vagy hagyományos (Am)
    };
    static Config loadConfig();
    static void saveConfig(const Config &c);

    explicit LibraryAnalyzer(Database *db, QObject *parent = nullptr);
    ~LibraryAnalyzer() override;

    void configure(const Config &c);
    const Config &config() const { return cfg_; }
    void setPaused(bool on);      // pl. a tartalék playout, vagy amíg a felhasználó maga becsül
    QString status() const;
    int pending() const { return pending_; }
    int total() const { return total_; }
    bool working() const { return busy_; }
    void restart();               // a korábban üresen maradt mezőket is újra megpróbálja

    // Az időablak: [from, to) óra, átnyúlhat éjfélen (pl. 22 → 6). from == to: egész nap.
    static bool inWindow(int hour, int fromHour, int toHour);

signals:
    void statusChanged();

private:
    void tick();
    void worker();
    void finished(int id, double bpm, int key, qint64 ms);
    void refreshCounts();

    Database *db_;
    Config cfg_;
    QTimer *timer_;
    bool paused_ = false, busy_ = false;
    int pending_ = 0, total_ = 0, doneSession_ = 0;
    qint64 spentMs_ = 0;          // az ebben a futásban elemzett elemek összes ideje (a hátralévő becsléséhez)
    QSet<int> skip_;              // ebben a futásban nem elérhető fájlok (pl. leválasztott hálózati meghajtó)
    QString lastError_;

    struct Job {
        int id;
        std::string path;
        bool bpm, key;
    };
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    bool quit_ = false;
    std::atomic<bool> cancel_{false};
};
