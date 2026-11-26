#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// „Most szól”: az adásban lévő elem címét kiírja egy szövegfájlba (stream kódolók, például butt, Liquidsoap,
// Mixxx, OBS ebből olvassák), és ha be van állítva, közvetlenül az Icecast szerver metaadatát is frissíti.
class NowPlaying : public QObject {
    Q_OBJECT
public:
    struct Track {
        int itemId = 0;
        QString title, artist, category;
        qint64 durationMs = 0;
        bool music = true;
        bool valid() const { return itemId > 0 || !title.isEmpty(); }
        bool operator==(const Track &o) const
        {
            return itemId == o.itemId && title == o.title && artist == o.artist && category == o.category && music == o.music;
        }
    };
    enum NonMusic { KeepLast, OwnTitle, FixedText };
    struct Config {
        bool fileEnabled = false;
        QString path;
        QString format = QStringLiteral("%artist% - %title%");
        bool bom = false;                 // UTF-8 BOM (néhány Windowsos program kéri)
        NonMusic nonMusic = KeepLast;
        QString nonMusicText;             // FixedText esetén
        QString idleText;                 // ha semmi sem szól (üres: üres fájl)
        bool icecastEnabled = false;
        QString icecastUrl = QStringLiteral("http://localhost:8000");
        QString icecastMount = QStringLiteral("/stream");
        QString icecastUser = QStringLiteral("admin");
        QString icecastPassword;
    };

    explicit NowPlaying(QObject *parent = nullptr);
    void configure(const Config &c);
    const Config &config() const { return cfg_; }
    static Config loadConfig();           // QSettings „nowplaying/…”
    static void saveConfig(const Config &c);
    static QString defaultPath();

    // Az adásban lévő és a következő elem; üres onAir: semmi sem szól.
    void setTracks(const Track &onAir, const Track &next);

    // A sablon kitöltése (az előnézethez is). Üres előadónál a fölösleges elválasztó eltűnik.
    static QString render(const QString &format, const Track &t, const Track &next);
    // A kiírandó szöveg a beállítások szerint (szünet, nem zenei elem). false: nincs mit küldeni, marad az előző.
    static bool compose(const Config &c, const Track &onAir, const Track &next, const Track &lastMusic, bool everPlayed,
                        QString *out);
    QString currentText() const { return text_; }
    QString status() const;               // emberi nyelvű állapot a beállításokhoz
    void writeIdle();                     // kilépéskor: a szünet szövege

signals:
    void changed(const QString &text);

private:
    void publish(bool force = false);
    bool writeFile(const QString &text);
    void sendIcecast(const QString &text);

    Config cfg_;
    Track onAir_, next_, lastMusic_;
    bool everPlayed_ = false;
    QString text_;
    bool hasText_ = false;
    QString fileError_, iceError_;
    QDateTime fileAt_, iceAt_;
    QNetworkAccessManager *net_ = nullptr;
    int iceSerial_ = 0;
};
