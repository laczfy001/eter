// Cartwall: színes gombfalak azonnal indítható hangokkal. Az Adás oldalon a playlist helyén nyílik meg.
#pragma once
#include "audio/Engine.h"
#include "db/Database.h"

#include <QFrame>
#include <QPointer>
#include <QVector>

class QDialog;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;

// Egy cart gombja.
class CartButton : public QWidget {
    Q_OBJECT
public:
    explicit CartButton(int slot, QWidget *parent = nullptr);
    void setCart(const Cart &cart, const QColor &wallColor);   // cart.slot < 0: üres
    void setPlaying(bool playing, qint64 posMs);
    bool playing() const { return playing_; }
    int slot() const { return slot_; }

signals:
    void triggered(int slot);
    void menuRequested(int slot, const QPoint &globalPos);
    void itemsDropped(int slot, const QVector<int> &itemIds);
    void pathsDropped(int slot, const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    int slot_;
    Cart cart_;
    QColor color_;
    bool playing_ = false, hover_ = false, pressed_ = false, dropping_ = false;
    qint64 posMs_ = 0;
};

class CartwallPanel : public QFrame {
    Q_OBJECT
public:
    static constexpr int kColumns = 4, kRows = 4, kSlots = kColumns * kRows;

    CartwallPanel(Database *db, eter::Engine *engine, QWidget *parent = nullptr);
    void setHeaderWidget(QWidget *w);          // a Playlist / Cartwall váltó helye
    void tick(float *peakL, float *peakR);     // a főablak 30 ms-os időzítője hívja
    void stopAll(int fadeMs = 150);
    bool anyPlaying() const;

    // A főablaknak és fejlesztői ellenőrzéshez
    int currentWall() const { return wallId_; }
    void selectWall(int wallId);
    int addWall(const QString &name, const QString &color);
    void setWallColor(int wallId, const QString &color);
    void assignItems(int slot, const QVector<int> &itemIds);
    void assignPaths(int slot, const QStringList &paths);
    void trigger(int slot);
    QString debugState() const;

signals:
    void message(const QString &text);

private:
    struct Voice { int wallId = -1; int slot = -1; };

    void reloadWalls();
    void reloadCarts();
    void assign(int slot, const QVector<Cart> &carts);
    void cartMenu(int slot, const QPoint &globalPos);
    void wallMenu(int wallId, const QPoint &globalPos);
    void pickFromLibrary(int slot);
    void browse(int slot);
    void renameWall(int wallId);
    void newWall();
    int voiceOf(int wallId, int slot) const;
    eter::Source &source(int voice) { return engine_->source(static_cast<eter::Engine::Channel>(eter::Engine::Cart1 + voice)); }

    Database *db_;
    eter::Engine *engine_;
    QHBoxLayout *header_, *tabs_;
    QVector<CartButton *> buttons_;
    QVector<QToolButton *> wallButtons_;
    QVector<Cartwall> walls_;
    QVector<Cart> carts_;                      // kSlots elem; slot < 0: üres
    QLabel *info_;
    QPushButton *stop_;
    int wallId_ = 0;
    Voice voices_[eter::Engine::kCartVoices];
    QPointer<QDialog> dialog_;
};
