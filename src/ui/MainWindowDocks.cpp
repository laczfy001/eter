// A főablak dokkolható panelei: létrehozás, alaphelyzet, teljes méret, zárolás, mentett elrendezések, Ablakok menü.
#include "MainWindow.h"

#include "CartwallPanel.h"
#include "DockPanels.h"
#include "Library.h"
#include "PlayerStrip.h"
#include "i18n.h"

#include <QAction>
#include <QDockWidget>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QPainter>
#include "Theme.h"

static constexpr int kDockVersion = 1;

// Az Elrendezés gomb ikonja: négy panel (zárolva: lakat, borostyánszínnel).
static QIcon layoutIcon(bool locked)
{
    QPixmap pm(QSize(16, 16) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(locked ? theme::amber : theme::c("#c9ced6"), 1.4));
    if (locked) {
        p.drawRoundedRect(QRectF(3, 7.5, 10, 6.5), 1.2, 1.2);
        p.drawArc(QRectF(5, 2.5, 6, 8), 0, 180 * 16);
    } else {
        p.drawRoundedRect(QRectF(2.5, 2.5, 5, 5), 1, 1);
        p.drawRoundedRect(QRectF(8.5, 2.5, 5, 5), 1, 1);
        p.drawRoundedRect(QRectF(2.5, 8.5, 11, 5), 1, 1);
    }
    return QIcon(pm);
}

void MainWindow::setupDocks()
{
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    struct Def {
        const char *key;
        QString title;
        QWidget *content;
    };
    const Def defs[] = {
        {"player", TR("Lejátszó"), playerStack_},
        {"strip", TR("A, B, C lejátszó"), strip_},
        {"control", TR("Adásvezérlés"), ctlPanel_},
        {"clock", TR("Óra"), clockPanel_},
        {"mixer", TR("Keverő"), mixerPanel_},
        {"playlist", TR("Playlist"), playlistPanel_},
        {"library", TR("Hangtár"), library_},
        {"cart", TR("Cartwall"), cartPanel_},
        {"requests", TR("Számkérések"), nullptr},
    };
    for (const Def &d : defs) {
        QWidget *content = d.content;
        if (!content) {
            // A számkérések panel tartalma (korábban külön ablak volt).
            openRequestsDialog();
            content = requestsDlg_;
        }
        QDockWidget *dock = makeDock(QString::fromLatin1(d.key), d.title, content, this);
        docks_.insert(QString::fromLatin1(d.key), dock);
        dockOrder_ << QString::fromLatin1(d.key);
        if (DockTitleBar *t = dockTitle(dock)) connect(t, &DockTitleBar::maximizeRequested, this, [this, dock] { toggleMaximize(dock); });
    }
    // Az A/B/C sáv megjelenése a régi kapcsolóval (Beállítások → Megjelenés) is egyezzen.
    connect(docks_["strip"], &QDockWidget::visibilityChanged, this, [this](bool) {
        if (!docksReady_) return;
        const bool on = !docks_["strip"]->isHidden();
        if (on != playerStripVisible()) QSettings().setValue(QStringLiteral("ui/playerStrip"), on);
    });

    docking_ = dockingSetting();
    applyDefaultLayout();
    const QByteArray saved = QSettings().value(QStringLiteral("ui/dock/state")).toByteArray();
    if (docking_ && !saved.isEmpty()) {
        if (!restoreState(saved, kDockVersion)) applyDefaultLayout();
        else if (QSettings().contains(QStringLiteral("ui/dock/visible")))
            applyVisibleDocks(QSettings().value(QStringLiteral("ui/dock/visible")).toStringList());
    }
    setLayoutLocked(QSettings().value(QStringLiteral("ui/dock/locked"), false).toBool());
    docksReady_ = true;
    fillWindowsMenu();
}

// Az alaphelyzet: a megszokott főablak. Fölül a lejátszó (alatta az A/B/C sáv), az adásvezérlés, az óra és a keverő;
// alatta a playlist és mellette a hangtár, fülként a cartwall és a számkérések.
void MainWindow::applyDefaultLayout()
{
    for (QDockWidget *d : std::as_const(docks_)) {
        d->setFloating(false);
        removeDockWidget(d);
    }
    QDockWidget *player = docks_["player"], *strip = docks_["strip"], *ctl = docks_["control"], *clock = docks_["clock"],
                *mixer = docks_["mixer"], *pl = docks_["playlist"], *lib = docks_["library"], *cart = docks_["cart"],
                *req = docks_["requests"];
    addDockWidget(Qt::TopDockWidgetArea, player);
    splitDockWidget(player, ctl, Qt::Horizontal);
    splitDockWidget(ctl, clock, Qt::Horizontal);
    splitDockWidget(player, strip, Qt::Vertical);
    splitDockWidget(clock, mixer, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, pl);
    splitDockWidget(pl, lib, Qt::Horizontal);
    tabifyDockWidget(lib, cart);
    tabifyDockWidget(cart, req);
    for (QDockWidget *d : std::as_const(docks_)) d->show();
    strip->setVisible(playerStripVisible());
    req->hide();
    if (requestsBtn_ && requestsBtn_->isVisible()) req->show();
    lib->raise();
    // A méretek a megszokott főablakéi: a felső sor alacsony, a playlist és a hangtár kapja a helyet. (Az ablak
    // megjelenése után is, mert addig a panelek még nem tudják a végleges méretüket.)
    auto sizes = [this, player, strip, ctl, clock, mixer, pl, lib] {
        resizeDocks({player, ctl, clock}, {1000, 230, 300}, Qt::Horizontal);
        resizeDocks({clock, mixer}, {80, 170}, Qt::Vertical);
        resizeDocks({pl, lib}, {740, 680}, Qt::Horizontal);
        resizeDocks({player, strip, pl}, {200, 60, 800}, Qt::Vertical);
    };
    sizes();
    QTimer::singleShot(0, this, sizes);
    maxDock_ = nullptr;
    maxState_.clear();
}

void MainWindow::resetLayout()
{
    applyDefaultLayout();
    if (stack_ && stack_->currentIndex() != 0) {
        liveHidden_.clear();
        showPage(stack_->currentIndex());
    }
    say(TR("A panelek az alap elrendezésben."), 5000);
}

QStringList MainWindow::liveVisibleDocks()
{
    if (maxDock_) return maxVisible_;
    QStringList out;
    for (const QString &k : std::as_const(dockOrder_))
        if (QDockWidget *d = docks_.value(k); d && (!d->isHidden() || liveHidden_.contains(d))) out << k;
    return out;
}

// A visszatöltött elrendezés után a panelek láthatósága (a restoreState a bezárt paneleket nem hozza vissza).
void MainWindow::applyVisibleDocks(const QStringList &keys)
{
    for (const QString &k : std::as_const(dockOrder_))
        if (QDockWidget *d = docks_.value(k)) d->setVisible(keys.contains(k));
}

QByteArray MainWindow::liveLayoutState()
{
    if (maxDock_ && !maxState_.isEmpty()) return maxState_;
    if (stack_ && stack_->currentIndex() != 0 && !liveState_.isEmpty()) return liveState_;
    return saveState(kDockVersion);
}

// Teljes méret: a többi dokkolt panel elrejtve, újra kattintva vissza az előző elrendezés.
void MainWindow::toggleMaximize(QDockWidget *d)
{
    if (maxDock_) {
        QDockWidget *was = maxDock_;
        maxDock_ = nullptr;
        if (!maxState_.isEmpty()) {
            restoreState(maxState_, kDockVersion);
            applyVisibleDocks(maxVisible_);
        }
        maxState_.clear();
        if (DockTitleBar *t = dockTitle(was)) t->setMaximized(false);
        if (was == d) return;
    }
    maxVisible_ = liveVisibleDocks();
    maxState_ = saveState(kDockVersion);
    maxDock_ = d;
    for (QDockWidget *o : std::as_const(docks_))
        if (o != d && !o->isFloating() && o->isVisible()) o->hide();
    if (DockTitleBar *t = dockTitle(d)) t->setMaximized(true);
}

void MainWindow::setLayoutLocked(bool on)
{
    layoutLocked_ = on;
    QSettings().setValue(QStringLiteral("ui/dock/locked"), on);
    applyDockFeatures();
}

// A panelek tulajdonságai a két kapcsoló szerint: fix elrendezésben fejléc nélkül, zárolva mozdíthatatlanul.
void MainWindow::applyDockFeatures()
{
    const bool frozen = layoutLocked_ || !docking_;
    for (QDockWidget *d : std::as_const(docks_)) {
        d->setFeatures(frozen ? QDockWidget::NoDockWidgetFeatures
                              : QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        if (DockTitleBar *t = dockTitle(d)) {
            t->setLocked(frozen);
            t->setFixed(!docking_);
        }
    }
    if (layoutBtn_) {
        layoutBtn_->setIcon(layoutIcon(frozen));
        layoutBtn_->setText(!docking_ ? TR("Fix elrendezés") : layoutLocked_ ? TR("Elrendezés zárolva") : TR("Elrendezés"));
        layoutBtn_->setToolTip(!docking_ ? TR("A panelek fixen, az eredeti elrendezésben (Ablakok menü → Önálló panelek)")
                               : layoutLocked_ ? TR("A panelek zárolva: nem mozdíthatók el, és nem zárhatók be (Ablakok menü → Elrendezés zárolása)")
                                               : TR("Panelek és elrendezések"));
        layoutBtn_->adjustSize();
        layoutBtn_->updateGeometry();   // a menüsor sarka igazodjon a felirathoz
    }
    if (lockAction_) lockAction_->setEnabled(docking_);
    fillWindowsMenu();
}

bool MainWindow::dockingSetting() { return QSettings().value(QStringLiteral("ui/dock/enabled"), true).toBool(); }

void MainWindow::setDockingEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("ui/dock/enabled"), on);
    if (on == docking_ || !docksReady_) {
        docking_ = on;
        if (docksReady_) applyDockFeatures();
        return;
    }
    if (maxDock_) toggleMaximize(maxDock_);
    const int page = stack_ ? stack_->currentIndex() : 0;
    if (page != 0) showPage(0);
    if (!on) {
        // Kikapcsolás: a saját elrendezés megmarad (bekapcsoláskor visszajön), a panelek az eredeti helyükre.
        QSettings().setValue(QStringLiteral("ui/dock/state"), saveState(kDockVersion));
        QSettings().setValue(QStringLiteral("ui/dock/visible"), liveVisibleDocks());
        docking_ = false;
        applyDefaultLayout();
    } else {
        docking_ = true;
        const QByteArray saved = QSettings().value(QStringLiteral("ui/dock/state")).toByteArray();
        if (!saved.isEmpty() && restoreState(saved, kDockVersion)) {
            if (QSettings().contains(QStringLiteral("ui/dock/visible")))
                applyVisibleDocks(QSettings().value(QStringLiteral("ui/dock/visible")).toStringList());
        }
    }
    applyDockFeatures();
    if (page != 0) showPage(page);
    say(on ? TR("Önálló panelek bekapcsolva: a panelek áthelyezhetők, kiemelhetők.")
           : TR("Fix elrendezés: minden panel az eredeti helyén."), 5000);
}

QStringList MainWindow::namedLayouts()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ui/layouts"));
    QStringList names;
    for (const QString &k : s.childKeys()) names << QString::fromUtf8(QByteArray::fromHex(k.toLatin1()));
    s.endGroup();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool MainWindow::saveNamedLayout(const QString &name)
{
    if (name.trimmed().isEmpty()) return false;
    QJsonObject o;
    o["state"] = QString::fromLatin1(liveLayoutState().toBase64());
    o["geometry"] = QString::fromLatin1(saveGeometry().toBase64());
    o["visible"] = QJsonArray::fromStringList(liveVisibleDocks());
    QSettings().setValue(QStringLiteral("ui/layouts/") + QString::fromLatin1(name.trimmed().toUtf8().toHex()),
                         QJsonDocument(o).toJson(QJsonDocument::Compact));
    fillWindowsMenu();
    return true;
}

bool MainWindow::loadNamedLayout(const QString &name)
{
    const QJsonObject o = QJsonDocument::fromJson(
                              QSettings().value(QStringLiteral("ui/layouts/") + QString::fromLatin1(name.toUtf8().toHex())).toByteArray())
                              .object();
    const QByteArray state = QByteArray::fromBase64(o.value("state").toString().toLatin1());
    if (state.isEmpty()) return false;
    if (maxDock_) toggleMaximize(maxDock_);
    const int page = stack_ ? stack_->currentIndex() : 0;
    if (page != 0) showPage(0);
    const bool ok = restoreState(state, kDockVersion);
    if (ok && o.contains("visible")) {
        QStringList keys;
        for (const QJsonValue &v : o.value("visible").toArray()) keys << v.toString();
        applyVisibleDocks(keys);
    }
    if (page != 0) showPage(page);
    if (ok) say(TR("Elrendezés betöltve: %1").arg(name), 5000);
    return ok;
}

void MainWindow::fillLayoutMenu(QMenu *m)
{
    for (const QString &n : namedLayouts()) m->addAction(n, this, [this, n] { loadNamedLayout(n); });
    if (!namedLayouts().isEmpty()) m->addSeparator();
    m->addAction(TR("Elrendezés mentése…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, TR("Elrendezés mentése"), TR("Név (például: Reggeli műsor)"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        if (namedLayouts().contains(name.trimmed())
            && QMessageBox::question(this, TR("Elrendezés mentése"), TR("Már van ilyen nevű elrendezés: felülírod?")) != QMessageBox::Yes)
            return;
        if (saveNamedLayout(name)) say(TR("Elrendezés mentve: %1").arg(name.trimmed()), 5000);
    });
    if (!namedLayouts().isEmpty()) {
        QMenu *del = m->addMenu(TR("Elrendezés törlése"));
        for (const QString &n : namedLayouts())
            del->addAction(n, this, [this, n] {
                QSettings().remove(QStringLiteral("ui/layouts/") + QString::fromLatin1(n.toUtf8().toHex()));
                fillWindowsMenu();
            });
    }
    m->addAction(TR("Alaphelyzet (a megszokott főablak)"), this, &MainWindow::resetLayout);
    m->addSeparator();
    if (!lockAction_) {
        // Állandó művelet (a menük újraépülnek, a gyorsbillentyű viszont mindig működjön). Ctrl+L az Adásnaplóé.
        lockAction_ = new QAction(TR("Elrendezés zárolása"), this);
        lockAction_->setCheckable(true);
        lockAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
        lockAction_->setShortcutContext(Qt::ApplicationShortcut);
        connect(lockAction_, &QAction::triggered, this, [this](bool on) { setLayoutLocked(on); });
        addAction(lockAction_);
    }
    lockAction_->setChecked(layoutLocked_);
    m->addAction(lockAction_);
}

void MainWindow::fillWindowsMenu()
{
    if (windowsMenu_) fillPanelsMenu(windowsMenu_);
}

// A panelek listája (pipával), az Infó ablak, majd az elrendezések. Ugyanez az Ablakok menüben, az Elrendezés
// gomb menüjében és a panelek fejlécén / a fülsoron jobb gombbal.
void MainWindow::fillPanelsMenu(QMenu *m)
{
    m->clear();
    if (!dockingAction_) {
        dockingAction_ = new QAction(TR("Önálló panelek (áthelyezhető, kiemelhető ablakok)"), this);
        dockingAction_->setCheckable(true);
        connect(dockingAction_, &QAction::triggered, this, [this](bool on) { setDockingEnabled(on); });
    }
    dockingAction_->setChecked(docking_);
    m->addAction(dockingAction_);
    m->addSeparator();
    QAction *head = m->addAction(docking_ ? TR("Panelek") : TR("Panelek (fix elrendezés)"));
    head->setEnabled(false);
    for (const QString &k : std::as_const(dockOrder_)) {
        QDockWidget *d = docks_.value(k);
        if (!d) continue;
        QAction *a = m->addAction(d->windowTitle());
        a->setCheckable(true);
        a->setChecked(!d->isHidden() || liveHidden_.contains(d));
        // Fix elrendezésben csak az A/B/C sáv és a számkérések kapcsolható (ahogy korábban is).
        a->setEnabled(docking_ || k == QLatin1String("strip") || k == QLatin1String("requests"));
        connect(a, &QAction::triggered, this, [this, d](bool on) {
            if (on) {
                if (stack_ && stack_->currentIndex() != 0 && !d->isFloating()) showPage(0);
                d->show();
                d->raise();
            } else {
                liveHidden_.removeAll(d);
                d->hide();
            }
        });
    }
    m->addAction(TR("Infó ablak"), this, &MainWindow::toggleInfoWindow);
    if (!docking_) return;
    m->addSeparator();
    QAction *lh = m->addAction(TR("Elrendezés"));
    lh->setEnabled(false);
    fillLayoutMenu(m);
}

QMenu *MainWindow::createPopupMenu()
{
    auto *m = new QMenu(this);
    fillPanelsMenu(m);
    return m;
}
