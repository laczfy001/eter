// Dokkolható panelek (Ablakok menü): az Adás munkaterület minden része – lejátszó, A/B/C sáv, adásvezérlés, óra,
// keverő, playlist, hangtár, cartwall, számkérések – önálló panel. A panelek a fejlécüknél fogva áthúzhatók,
// egymás mellé / alá dokkolhatók, fülcsoportba tehetők, a köztük lévő elválasztóval átméretezhetők, és kiemelhetők
// önálló ablakká (akár a második monitorra). Az elrendezés megmarad; név szerint menthető és visszatölthető, az
// alaphelyzet a megszokott főablak. Zárolva (élő adás alatt) a panelek nem mozdíthatók el véletlenül.
#pragma once

#include <QDockWidget>
#include <QWidget>

class QLabel;
class QToolButton;

// A panel fejléce: fogantyú, cím, kiegészítő felirat, gombok (kiemelés / visszadokkolás, teljes méret, bezárás).
class DockTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit DockTitleBar(QDockWidget *dock);
    void setExtra(const QString &text, const QString &color = QString());   // pl. „2 ÚJ”
    void setLocked(bool locked);
    void setMaximized(bool on);
    void setFixed(bool on);   // fix (nem dokkolható) elrendezés: a fejléc eltűnik
    void refresh();

signals:
    void maximizeRequested();

protected:
    void paintEvent(QPaintEvent *e) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QDockWidget *dock_;
    QLabel *title_ = nullptr, *extra_ = nullptr;
    QWidget *grip_ = nullptr;
    QToolButton *float_ = nullptr, *max_ = nullptr, *close_ = nullptr;
    bool locked_ = false;
    bool fixed_ = false;
};

// Egy panel létrehozása a megadott tartalommal; objectName: "dock_<kulcs>" (az elrendezés mentéséhez).
QDockWidget *makeDock(const QString &key, const QString &title, QWidget *content, QWidget *parent);
DockTitleBar *dockTitle(QDockWidget *d);
