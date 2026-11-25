// Infó ablak: a hangtárban kijelölt (vagy az adásban lévő) elem adatai külön, nem modális ablakban: a szám albuma és
// éve, az előadó adatai, fotója és rövid életrajza (MusicBrainz, Wikipédia, TheAudioDB; Beállítások → Előadói adatok).
// Az adatok az adatbázisban tárolódnak; az ablak csak megjeleníti őket, és ha hiányoznak, lekéri az elemhez.
#pragma once

#include "db/Database.h"

#include <QWidget>
#include <functional>

class ArtistInfoService;
class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QToolButton;
class QVBoxLayout;

class InfoWindow : public QWidget {
    Q_OBJECT
public:
    enum Source { Selected = 0, OnAir = 1 };
    using Provider = std::function<Item()>;

    InfoWindow(Database *db, ArtistInfoService *info, QWidget *parent);
    ~InfoWindow() override;

    void setProviders(Provider selected, Provider onAir);
    void setSource(Source s);
    Source source() const { return source_; }
    void selectionChanged();            // a hangtár kijelölése változott
    void openWindow();                  // megnyitás (a felhasználó kérésére): előtérbe hozza
    int shownItemId() const { return itemId_; }
    QString stateName() const { return state_; }   // fejlesztéshez: data, loading, notfound, off, empty

signals:
    void visibilityChanged(bool visible);
    void settingsRequested();           // Beállítások → Előadói adatok

protected:
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void poll();
    void rebuild();
    void setOnTop(bool on);
    void saveGeometryNow();
    QWidget *buildContent(const Item &it);

    Database *db_;
    ArtistInfoService *info_;
    Provider selected_, onAir_;
    Source source_ = Selected;
    int itemId_ = 0;
    QString artistKey_, state_ = QStringLiteral("empty");
    QCheckBox *onTop_;
    QToolButton *srcSel_, *srcAir_;
    QScrollArea *scroll_;
    QLabel *footer_;
    QPushButton *refresh_;
    QTimer *timer_;
    bool requested_ = false;            // ehhez az elemhez már kértünk adatot (egyszer elég)
};
