// Stream enkóder: a program hangját (vagy egy bemenetet) MP3-ba vagy Ogg Vorbisba kódolja, és forrásként elküldi egy
// Icecast vagy Shoutcast szervernek. Minden enkóder a saját szálán kódol és küld, így a hálózat nem akaszthatja meg
// az adást. Megszakadáskor újracsatlakozik; az adásban lévő címet a szerver metaadatában frissíti (MP3: a szerver
// admin felületén; Ogg: a folyamba írt új fejléccel).
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "stream/Processor.h"

#include <functional>

#include <memory>

class QThread;

namespace stream {

enum class Server { Icecast, Shoutcast1, Shoutcast2 };
enum class Format { Mp3, Vorbis };
enum class State { Off, Connecting, OnAir, Retrying, Error };

struct Config {
    QString id;
    QString name;
    bool autoStart = false;
    Server server = Server::Icecast;
    QString host;
    int port = 8000;
    QString mount = QStringLiteral("/stream");
    QString user = QStringLiteral("source");
    QString password;
    bool tls = false;
    int sid = 1;                   // Shoutcast v2: a stream azonosítója
    Format format = Format::Mp3;
    int kbps = 128;
    bool abr = false;              // MP3: átlagos bitráta (ABR) az állandó (CBR) helyett
    int sampleRate = 44100;
    bool mono = false;
    QString source;                // "" = fő kimenet (KI); "in:<eszköz neve>" = hangkártya-bemenet ("in:" = alapértelmezett)
    double gainDb = 0;
    ProcConfig proc;               // hangfeldolgozás: AGC, kompresszor, limiter
    bool sendTitle = true;
    bool titleLikeNowPlaying = true;
    QString titleFormat = QStringLiteral("%artist% - %title%");
    QString stationName, description, genre, url;
    bool isPublic = false;
    bool reconnect = true;
    int retrySec = 5;

    QJsonObject toJson() const;
    static Config fromJson(const QJsonObject &o);
    bool connectionDiffers(const Config &o) const;   // ez a különbség csak újrakapcsolódással érvényesül
    QString contentType() const;
    QString endpoint() const;      // „Icecast · host:8000/radio.mp3”
    QString formatText() const;    // „MP3” / „Ogg Vorbis”
    QString audioText() const;     // „44,1 kHz · sztereó”
    bool inputSource() const { return source.startsWith(QLatin1String("in:")); }
    QString inputDevice() const { return source.mid(3); }
};

struct Status {
    State state = State::Off;
    QString message;
    qint64 bytes = 0;
    qint64 since = 0;              // ms (epoch): mióta ad
    int listeners = -1;            // -1: nem ismert
    int attempt = 0;
    qint64 retryAt = 0;            // ms (epoch): a következő próbálkozás
    float peakL = 0, peakR = 0;    // a legutóbbi lekérdezés óta (a szint és a korlátozó után)
    ProcMeters proc;               // a hangfeldolgozás mérői (a legutóbbi lekérdezés óta)
};

class Worker;
struct Shared;

class Encoder : public QObject {
    Q_OBJECT
public:
    explicit Encoder(const Config &c, QObject *parent = nullptr);
    ~Encoder() override;

    const Config &config() const { return cfg_; }
    void setConfig(const Config &c);          // a szint, a korlátozó és a cím azonnal; a többi a következő kapcsolódáskor
    void start();
    void stop();
    void test();                              // csak kapcsolódás és bejelentkezés, utána bont
    bool active() const { return active_; }   // be van kapcsolva (ad, kapcsolódik vagy újrapróbál)
    // Fut, és azóta olyan beállítás változott, amely csak újrakapcsolódással érvényesül.
    bool needsRestart() const { return active_ && cfg_.connectionDiffers(started_); }
    void feed(const float *stereo48k, int frames);   // a felület szálán
    void setTitle(const QString &text);
    // Belehallgatás: a feldolgozott (48 kHz-es, sztereó) jel ide is megy, az enkóder szálán hívva. Üres: ki.
    void setMonitor(std::function<void(const float *, int)> sink);
    Status status() const;                    // a csúcsszintet nullázza (a szintmérőnek)
    State state() const;
    QStringList events() const;               // a legutóbbi események (legfeljebb 200)

signals:
    void event(const QString &line);
    void stateChanged();

private:
    Config cfg_, started_;
    QThread *thread_ = nullptr;
    Worker *w_ = nullptr;
    std::shared_ptr<Shared> sh_;
    bool active_ = false;
};

QString stateLabel(State s);

} // namespace stream
