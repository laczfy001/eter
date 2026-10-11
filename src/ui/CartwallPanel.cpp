#include "i18n.h"
#include "CartwallPanel.h"
#include "Library.h"
#include "SchedulerPage.h"
#include "Theme.h"
#include "audio/Decoder.h"

#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

static QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

static QColor textOn(const QColor &bg)
{
    const double lum = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
    return lum > 0.55 ? QColor("#101215") : QColor("#ffffff");
}

static const QColor kPanel("#1b1e24");

// ================================================================ gomb

CartButton::CartButton(int slot, QWidget *parent) : QWidget(parent), slot_(slot)
{
    setAcceptDrops(true);
    setMinimumSize(96, 62);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    cart_.slot = -1;
}

void CartButton::setCart(const Cart &cart, const QColor &wallColor)
{
    cart_ = cart;
    color_ = wallColor;
    if (cart_.slot < 0) {
        setToolTip(TR("Üres hely: kattints, vagy húzz ide egy elemet a hangtárból vagy a fájlkezelőből"));
    } else {
        setToolTip(TR("%1\n%2\n\nKattintás: indítás, újabb kattintás: leállítás · jobb gomb: beállítás")
                       .arg(cart_.label, cart_.path));
    }
    setAccessibleName(cart_.slot < 0 ? TR("Üres cart, %1. hely").arg(slot_ + 1) : cart_.label);
    update();
}

void CartButton::setPlaying(bool playing, qint64 posMs)
{
    // A visszaszámlálás tizedmásodpercenként változik; ritkább újrarajzolás elég.
    if (playing == playing_ && posMs / 100 == posMs_ / 100) return;
    playing_ = playing;
    posMs_ = posMs;
    update();
}

void CartButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const bool empty = cart_.slot < 0;
    QFont small = font();
    small.setPixelSize(11);
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);

    if (empty) {
        p.setPen(QPen(dropping_ ? theme::amber : (hover_ ? QColor("#6b7380") : QColor("#3a4150")), 1, Qt::DashLine));
        p.setBrush(dropping_ ? QColor("#262b34") : (hover_ ? QColor("#20242b") : QColor("#181b20")));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(QColor("#6b7380"));
        p.setFont(small);
        p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, QString::number(slot_ + 1));
        QFont plus = font();
        plus.setPixelSize(22);
        p.setFont(plus);
        p.setPen(hover_ || dropping_ ? QColor("#c9ced6") : QColor("#4a5160"));
        p.drawText(r, Qt::AlignCenter, QStringLiteral("+"));
        return;
    }

    QColor bg = playing_ ? color_ : mix(kPanel, color_, hover_ ? 0.42 : 0.30);
    if (pressed_) bg = bg.lighter(115);
    p.setPen(QPen(dropping_ ? theme::amber : (playing_ ? color_.lighter(135) : mix(kPanel, color_, 0.75)), dropping_ ? 2 : 1));
    p.setBrush(bg);
    p.drawRoundedRect(r, 6, 6);

    const QColor fg = playing_ ? textOn(bg) : QColor("#eef0f3");
    QColor dim = fg;
    dim.setAlphaF(0.72);

    // Haladás: az alsó szélen sáv
    if (playing_ && cart_.durationMs > 0) {
        const double f = std::clamp(double(posMs_) / double(cart_.durationMs), 0.0, 1.0);
        QColor track = fg;
        track.setAlphaF(0.22);
        const QRectF barRect(r.left() + 8, r.bottom() - 9, r.width() - 16, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRoundedRect(barRect, 2, 2);
        p.setBrush(fg);
        p.drawRoundedRect(QRectF(barRect.left(), barRect.top(), barRect.width() * f, barRect.height()), 2, 2);
    }

    // Fent: sorszám és idő (lejátszás közben a hátralévő idő)
    p.setFont(small);
    p.setPen(dim);
    p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, QString::number(slot_ + 1));
    p.setFont(mono);
    p.setPen(playing_ ? fg : dim);
    const qint64 shown = playing_ ? std::max<qint64>(0, cart_.durationMs - posMs_) : cart_.durationMs;
    QString time = QStringLiteral("%1:%2").arg(shown / 60000).arg(shown / 1000 % 60, 2, 10, QLatin1Char('0'));
    if (playing_) time = QStringLiteral("−%1.%2").arg(time).arg(shown / 100 % 10);
    p.drawText(r.adjusted(8, 5, -8, -6), Qt::AlignRight | Qt::AlignTop, time);

    // Felirat: legfeljebb annyi sor, amennyi elfér
    QFont label = font();
    label.setPixelSize(13);
    label.setWeight(QFont::DemiBold);
    p.setFont(label);
    p.setPen(fg);
    const QRectF textRect = r.adjusted(8, 24, -8, -14);
    const QFontMetrics fm(label);
    const int lines = std::max(1, int(textRect.height()) / fm.lineSpacing());
    QStringList out;
    QString line;
    for (const QString &word : cart_.label.split(' ', Qt::SkipEmptyParts)) {
        const QString tryLine = line.isEmpty() ? word : line + ' ' + word;
        if (line.isEmpty() || fm.horizontalAdvance(tryLine) <= textRect.width()) {
            line = tryLine;
        } else {
            out << line;
            line = word;
        }
    }
    if (!line.isEmpty()) out << line;
    // Ami nem fér ki, az utolsó látható sor végére kerül, levágva.
    while (out.size() > lines) {
        const QString last = out.takeLast();
        out.last() += ' ' + last;
    }
    for (QString &l : out) l = fm.elidedText(l, Qt::ElideRight, int(textRect.width()));
    const double total = out.size() * fm.lineSpacing();
    double y = textRect.top() + std::max(0.0, (textRect.height() - total) / 2);
    for (const QString &l : out) {
        p.drawText(QRectF(textRect.left(), y, textRect.width(), fm.lineSpacing()), Qt::AlignHCenter | Qt::AlignVCenter, l);
        y += fm.lineSpacing();
    }
}

void CartButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        emit menuRequested(slot_, e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    pressed_ = true;
    update();
    // Cartnál a lenyomás pillanata számít, nem a felengedésé.
    const bool empty = cart_.slot < 0;
    emit triggered(slot_);
    if (empty) {                                // üres helyen menü nyílt, a felengedés már nem ide érkezik
        pressed_ = false;
        update();
    }
}

void CartButton::mouseReleaseEvent(QMouseEvent *)
{
    pressed_ = false;
    update();
}

void CartButton::enterEvent(QEnterEvent *)
{
    hover_ = true;
    update();
}

void CartButton::leaveEvent(QEvent *)
{
    hover_ = false;
    update();
}

void CartButton::dragEnterEvent(QDragEnterEvent *e)
{
    if (!e->mimeData()->hasFormat(kItemsMime) && !e->mimeData()->hasUrls()) return;
    e->acceptProposedAction();
    dropping_ = true;
    update();
}

void CartButton::dragLeaveEvent(QDragLeaveEvent *)
{
    dropping_ = false;
    update();
}

void CartButton::dropEvent(QDropEvent *e)
{
    dropping_ = false;
    update();
    const QMimeData *mime = e->mimeData();
    if (mime->hasFormat(kItemsMime)) {
        e->acceptProposedAction();
        emit itemsDropped(slot_, decodeIds(mime->data(kItemsMime)));
    } else if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : mime->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (paths.isEmpty()) return;
        e->acceptProposedAction();
        emit pathsDropped(slot_, paths);
    }
}

// ================================================================ panel

CartwallPanel::CartwallPanel(Database *db, eter::Engine *engine, QWidget *parent) : QFrame(parent), db_(db), engine_(engine)
{
    setObjectName("panel");
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(1, 1, 1, 1);
    v->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    header_ = new QHBoxLayout(head);
    header_->setContentsMargins(10, 8, 10, 8);
    header_->setSpacing(6);
    stop_ = new QPushButton(TR("Mind leáll"));
    stop_->setFocusPolicy(Qt::NoFocus);
    stop_->setToolTip(TR("Minden szóló cart leállítása"));
    header_->addStretch(1);
    header_->addWidget(stop_);
    v->addWidget(head);

    // A falak fülei
    auto *tabRow = new QFrame;
    tabRow->setObjectName("panelHeader");
    tabs_ = new QHBoxLayout(tabRow);
    tabs_->setContentsMargins(10, 7, 10, 7);
    tabs_->setSpacing(6);
    v->addWidget(tabRow);

    auto *gridHost = new QWidget;
    auto *grid = new QGridLayout(gridHost);
    grid->setContentsMargins(10, 10, 10, 10);
    grid->setSpacing(8);
    for (int i = 0; i < kSlots; ++i) {
        auto *b = new CartButton(i);
        buttons_ << b;
        grid->addWidget(b, i / kColumns, i % kColumns);
        connect(b, &CartButton::triggered, this, &CartwallPanel::trigger);
        connect(b, &CartButton::menuRequested, this, &CartwallPanel::cartMenu);
        connect(b, &CartButton::itemsDropped, this, &CartwallPanel::assignItems);
        connect(b, &CartButton::pathsDropped, this, &CartwallPanel::assignPaths);
    }
    v->addWidget(gridHost, 1);

    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *f = new QHBoxLayout(foot);
    f->setContentsMargins(10, 7, 10, 7);
    info_ = new QLabel;
    info_->setObjectName("small");
    auto *hint = new QLabel(TR("Kattintás: indítás / leállítás · jobb gomb: beállítás · húzd a gombra a hangtárból vagy a fájlkezelőből"));
    hint->setObjectName("small");
    f->addWidget(info_);
    f->addStretch(1);
    f->addWidget(hint);
    v->addWidget(foot);

    connect(stop_, &QPushButton::clicked, this, [this] { stopAll(); });
    connect(db_, &Database::itemsChanged, this, [this] { reloadCarts(); });

    if (db_->cartwalls().isEmpty()) db_->addCartwall(TR("Cartwall 1"), QStringLiteral("#f5a524"));
    wallId_ = db_->setting("cartwall_current", "0").toInt();
    reloadWalls();
}

void CartwallPanel::setHeaderWidget(QWidget *w)
{
    header_->insertWidget(0, w);
}

void CartwallPanel::reloadWalls()
{
    walls_ = db_->cartwalls();
    bool found = false;
    for (const Cartwall &w : walls_) found = found || w.id == wallId_;
    if (!found) wallId_ = walls_.isEmpty() ? 0 : walls_.first().id;

    while (QLayoutItem *item = tabs_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    wallButtons_.clear();
    for (const Cartwall &w : walls_) {
        auto *b = new QToolButton;
        b->setText(w.name);
        b->setCheckable(true);
        b->setChecked(w.id == wallId_);
        b->setFocusPolicy(Qt::NoFocus);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        b->setToolTip(TR("Jobb gomb: átnevezés, szín, törlés"));
        const QColor c(w.color.isEmpty() ? QStringLiteral("#3a4150") : w.color);
        const QColor idle = mix(kPanel, c, 0.28);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { border: 1px solid %1; border-radius: 4px; padding: 4px 14px; background: %2; color: #e8eaed; }"
            "QToolButton:hover { background: %3; }"
            "QToolButton:checked { background: %4; color: %5; font-weight: 600; border-color: %6; }")
                             .arg(mix(kPanel, c, 0.7).name(), idle.name(), mix(kPanel, c, 0.42).name(), c.name(), textOn(c).name(), c.lighter(130).name()));
        connect(b, &QToolButton::clicked, this, [this, id = w.id] { selectWall(id); });
        connect(b, &QWidget::customContextMenuRequested, this, [this, b, id = w.id](const QPoint &pos) { wallMenu(id, b->mapToGlobal(pos)); });
        tabs_->addWidget(b);
        wallButtons_ << b;
    }
    auto *add = new QToolButton;
    add->setText(TR("+ Új fal"));
    add->setFocusPolicy(Qt::NoFocus);
    add->setToolTip(TR("Új cartwall létrehozása"));
    add->setStyleSheet("QToolButton { border: 1px dashed #3a4150; border-radius: 4px; padding: 4px 12px; color: #9aa3af; background: transparent; }"
                       "QToolButton:hover { color: #e8eaed; border-color: #6b7380; }");
    connect(add, &QToolButton::clicked, this, &CartwallPanel::newWall);
    tabs_->addWidget(add);
    tabs_->addStretch(1);
    reloadCarts();
}

void CartwallPanel::reloadCarts()
{
    carts_.fill(Cart(), kSlots);
    for (Cart &c : carts_) c.slot = -1;
    QColor color("#3a4150");
    for (const Cartwall &w : walls_)
        if (w.id == wallId_ && !w.color.isEmpty()) color = QColor(w.color);
    int used = 0;
    for (const Cart &c : db_->carts(wallId_)) {
        if (c.slot < 0 || c.slot >= kSlots) continue;
        carts_[c.slot] = c;
        ++used;
    }
    for (int i = 0; i < kSlots; ++i) {
        buttons_[i]->setCart(carts_[i], color);
        const int voice = voiceOf(wallId_, i);
        buttons_[i]->setPlaying(voice >= 0, voice >= 0 ? source(voice).position() * 1000 / eter::kRate : 0);
    }
    info_->setText(TR("%1 / %2 hely foglalt").arg(used).arg(kSlots));
}

void CartwallPanel::selectWall(int wallId)
{
    wallId_ = wallId;
    db_->setSetting("cartwall_current", QString::number(wallId));
    for (int i = 0; i < wallButtons_.size() && i < walls_.size(); ++i) wallButtons_[i]->setChecked(walls_[i].id == wallId);
    reloadCarts();
}

int CartwallPanel::addWall(const QString &name, const QString &color)
{
    const int id = db_->addCartwall(name, color);
    if (id) {
        wallId_ = id;
        db_->setSetting("cartwall_current", QString::number(id));
    }
    reloadWalls();
    return id;
}

void CartwallPanel::setWallColor(int wallId, const QString &color)
{
    for (const Cartwall &w : walls_)
        if (w.id == wallId) db_->updateCartwall(wallId, w.name, color);
    reloadWalls();
}

void CartwallPanel::newWall()
{
    bool ok = false;
    const QString name = QInputDialog::getText(window(), TR("Új cartwall"), TR("A fal neve:"), QLineEdit::Normal,
                                               TR("Cartwall %1").arg(walls_.size() + 1), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    // Új falnak olyan szín jut, amilyen még nincs.
    const auto palette = paletteColors();
    QString color = palette.value(2).second;
    for (int i = 1; i < palette.size(); ++i) {
        bool taken = false;
        for (const Cartwall &w : walls_) taken = taken || w.color.compare(palette[i].second, Qt::CaseInsensitive) == 0;
        if (!taken) { color = palette[i].second; break; }
    }
    addWall(name, color);
    emit message(TR("Új cartwall: %1. A színe a fülén jobb gombbal állítható.").arg(name));
}

void CartwallPanel::renameWall(int wallId)
{
    for (const Cartwall &w : walls_) {
        if (w.id != wallId) continue;
        bool ok = false;
        const QString name = QInputDialog::getText(window(), TR("Cartwall átnevezése"), TR("A fal neve:"),
                                                   QLineEdit::Normal, w.name, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        db_->updateCartwall(wallId, name, w.color);
        reloadWalls();
        return;
    }
}

void CartwallPanel::wallMenu(int wallId, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(TR("Átnevezés…"), this, [this, wallId] { renameWall(wallId); });
    QMenu *colors = menu.addMenu(TR("Szín"));
    QString current;
    for (const Cartwall &w : walls_)
        if (w.id == wallId) current = w.color;
    for (const auto &c : paletteColors()) {
        QAction *a = colors->addAction(swatchIcon(QColor(c.second)), c.first, this, [this, wallId, hex = c.second] { setWallColor(wallId, hex); });
        a->setCheckable(true);
        a->setChecked(current.compare(c.second, Qt::CaseInsensitive) == 0);
    }
    menu.addSeparator();
    QAction *del = menu.addAction(TR("Fal törlése…"), this, [this, wallId] {
        QString name;
        for (const Cartwall &w : walls_)
            if (w.id == wallId) name = w.name;
        if (QMessageBox::question(window(), TR("Cartwall törlése"),
                                  TR("Törlöd ezt a cartwallt a gombjaival együtt?\n\n%1\n\nA hangfájlok és a hangtár elemei megmaradnak.").arg(name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        for (int v = 0; v < eter::Engine::kCartVoices; ++v)
            if (voices_[v].wallId == wallId) source(v).fadeOutAndStop(150);
        db_->deleteCartwall(wallId);
        reloadWalls();
    });
    del->setEnabled(walls_.size() > 1);
    menu.exec(globalPos);
}

void CartwallPanel::cartMenu(int slot, const QPoint &globalPos)
{
    if (slot < 0 || slot >= kSlots) return;
    const bool empty = carts_[slot].slot < 0;
    QMenu menu(this);
    menu.addAction(TR("Hang a hangtárból…"), this, [this, slot] { pickFromLibrary(slot); });
    menu.addAction(TR("Hangfájl tallózása…"), this, [this, slot] { browse(slot); });
    if (!empty) {
        menu.addSeparator();
        menu.addAction(TR("Felirat…"), this, [this, slot] {
            bool ok = false;
            const QString label = QInputDialog::getText(window(), TR("A gomb felirata"), TR("Felirat:"), QLineEdit::Normal,
                                                        carts_[slot].label, &ok).trimmed();
            if (!ok) return;
            Cart c = carts_[slot];
            c.label = label;                    // üresen hagyva: a hangtárelem címe
            if (label.isEmpty() && !c.itemId) c.label = QFileInfo(c.path).completeBaseName();
            db_->setCart(wallId_, c);
            reloadCarts();
        });
        menu.addAction(TR("Gomb ürítése"), this, [this, slot] {
            const int voice = voiceOf(wallId_, slot);
            if (voice >= 0) source(voice).fadeOutAndStop(150);
            db_->clearCart(wallId_, slot);
            reloadCarts();
        });
    }
    menu.exec(globalPos);
}

void CartwallPanel::pickFromLibrary(int slot)
{
    if (dialog_) dialog_->close();
    auto *dlg = new ItemPickerDialog(db_, window(), QStringLiteral("picker_category_cart"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dialog_ = dlg;
    const int wall = wallId_;
    connect(dlg, &ItemPickerDialog::picked, this, [this, slot, wall](int itemId) {
        if (wall != wallId_) selectWall(wall);
        assignItems(slot, {itemId});
    });
    dlg->setModal(false);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void CartwallPanel::browse(int slot)
{
    if (dialog_) dialog_->close();
    auto *dlg = new QFileDialog(window(), TR("Hangfájl a cartwallra"), QSettings().value("ui/lastDir").toString(),
                                TR("Hangfájlok (*.%1);;Minden fájl (*)").arg(audioExtensions().join(" *.")));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFileMode(QFileDialog::ExistingFiles);
    dialog_ = dlg;
    const int wall = wallId_;
    connect(dlg, &QFileDialog::filesSelected, this, [this, slot, wall](const QStringList &files) {
        if (files.isEmpty()) return;
        QSettings().setValue("ui/lastDir", QFileInfo(files.first()).absolutePath());
        if (wall != wallId_) selectWall(wall);
        assignPaths(slot, files);
    });
    dlg->setModal(false);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void CartwallPanel::assignItems(int slot, const QVector<int> &itemIds)
{
    QVector<Cart> list;
    for (int id : itemIds) {
        const Item it = db_->item(id);
        if (!it.id) continue;
        Cart c;
        c.itemId = it.id;
        c.path = it.path;
        c.durationMs = it.durationMs;
        list << c;                              // a felirat üres marad: a hangtárelem címe látszik
    }
    assign(slot, list);
}

void CartwallPanel::assignPaths(int slot, const QStringList &paths)
{
    QVector<Cart> list;
    int failed = 0;
    for (const QString &path : paths) {
        if (QFileInfo(path).isDir()) continue;
        // Ha a fájl már a hangtárban van, arra hivatkozunk.
        if (const int id = db_->itemIdByPath(path)) {
            const Item it = db_->item(id);
            Cart c;
            c.itemId = id;
            c.path = it.path;
            c.durationMs = it.durationMs;
            list << c;
            continue;
        }
        eter::Decoder dec;
        std::string err;
        if (!dec.open(path.toStdString(), &err)) {
            ++failed;
            continue;
        }
        Cart c;
        c.path = QFileInfo(path).absoluteFilePath();
        c.label = QString::fromStdString(dec.title()).trimmed();
        if (c.label.isEmpty()) c.label = QFileInfo(path).completeBaseName();
        c.durationMs = dec.durationFrames() * 1000 / eter::kRate;
        list << c;
    }
    if (failed) emit message(TR("%1 fájl nem hangfájl vagy nem olvasható, kimaradt.").arg(failed));
    assign(slot, list);
}

// Az első elem a megadott gombra kerül (felülírja), a többi az utána következő üres helyekre.
void CartwallPanel::assign(int slot, const QVector<Cart> &list)
{
    if (list.isEmpty() || slot < 0 || slot >= kSlots || !wallId_) return;
    int placed = 0, at = slot;
    for (int i = 0; i < list.size(); ++i) {
        if (i > 0) {
            ++at;
            while (at < kSlots && carts_[at].slot >= 0) ++at;
        }
        if (at >= kSlots) break;
        const int voice = voiceOf(wallId_, at);
        if (voice >= 0) source(voice).fadeOutAndStop(150);
        Cart c = list[i];
        c.slot = at;
        db_->setCart(wallId_, c);
        carts_[at] = c;
        ++placed;
    }
    reloadCarts();
    if (placed < list.size())
        emit message(TR("%1 elem felkerült a falra, %2 nem fért el.").arg(placed).arg(list.size() - placed));
}

int CartwallPanel::voiceOf(int wallId, int slot) const
{
    for (int v = 0; v < eter::Engine::kCartVoices; ++v)
        if (voices_[v].wallId == wallId && voices_[v].slot == slot) return v;
    return -1;
}

void CartwallPanel::trigger(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    const Cart &cart = carts_[slot];
    if (cart.slot < 0) {
        cartMenu(slot, buttons_[slot]->mapToGlobal(QPoint(12, buttons_[slot]->height() / 2)));
        return;
    }
    const int playing = voiceOf(wallId_, slot);
    if (playing >= 0) {
        source(playing).fadeOutAndStop(120);    // rövid lecsengés, hogy ne kattanjon
        voices_[playing] = Voice();
        buttons_[slot]->setPlaying(false, 0);
        return;
    }
    int voice = -1;
    for (int v = 0; v < eter::Engine::kCartVoices; ++v)
        if (voices_[v].wallId < 0 && !source(v).playing()) { voice = v; break; }
    if (voice < 0) {
        emit message(TR("Egyszerre legfeljebb %1 cart szólhat.").arg(eter::Engine::kCartVoices));
        return;
    }
    std::string err;
    auto &src = source(voice);
    src.takeFinished();
    if (!src.load(cart.path.toStdString(), &err)) {
        emit message(TR("Nem játszható le: %1 – %2").arg(cart.label, QString::fromStdString(err)));
        return;
    }
    src.play();
    voices_[voice] = {wallId_, slot};
    buttons_[slot]->setPlaying(true, 0);
}

void CartwallPanel::stopAll(int fadeMs)
{
    for (int v = 0; v < eter::Engine::kCartVoices; ++v) {
        if (voices_[v].wallId < 0) continue;
        source(v).fadeOutAndStop(fadeMs);
        voices_[v] = Voice();
    }
    for (CartButton *b : buttons_) b->setPlaying(false, 0);
}

bool CartwallPanel::anyPlaying() const
{
    for (const Voice &v : voices_)
        if (v.wallId >= 0) return true;
    return false;
}

void CartwallPanel::tick(float *peakL, float *peakR)
{
    bool any = false;
    for (int v = 0; v < eter::Engine::kCartVoices; ++v) {
        auto &src = source(v);
        float l = 0.f, r = 0.f;
        src.takePeaks(l, r);
        if (peakL) *peakL = std::max(*peakL, l);
        if (peakR) *peakR = std::max(*peakR, r);
        Voice &voice = voices_[v];
        if (voice.wallId < 0) continue;
        if (src.takeFinished() || !src.playing()) {
            if (voice.wallId == wallId_ && voice.slot >= 0 && voice.slot < kSlots) buttons_[voice.slot]->setPlaying(false, 0);
            voice = Voice();
            continue;
        }
        any = true;
        if (voice.wallId == wallId_ && voice.slot >= 0 && voice.slot < kSlots)
            buttons_[voice.slot]->setPlaying(true, src.position() * 1000 / eter::kRate);
    }
    stop_->setEnabled(any);
}

QString CartwallPanel::debugState() const
{
    QStringList parts;
    for (const Cartwall &w : walls_) parts << QStringLiteral("%1%2[%3]").arg(w.id == wallId_ ? "*" : "", w.name, w.color);
    for (int i = 0; i < kSlots; ++i) {
        if (carts_[i].slot < 0) continue;
        parts << QStringLiteral("%1:%2%3(%4)").arg(i + 1).arg(carts_[i].label, carts_[i].itemId ? "" : "~").arg(buttons_[i]->playing() ? "szól" : "áll");
    }
    return parts.join("  ");
}
