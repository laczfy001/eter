#include "ui/DockPanels.h"

#include "i18n.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolButton>

namespace {

enum class Glyph { Float, Dock, Max, Restore, Close };

// A gombok ikonjai rajzolva (minden rendszeren egyformák, a színséma színével).
QIcon glyphIcon(Glyph g)
{
    QIcon icon;
    for (const QColor &col : {theme::c("#9aa3af"), theme::c("#e8eaed")}) {
        const qreal dpr = 2;
        QPixmap pm(QSize(16, 16) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(col, 1.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        switch (g) {
        case Glyph::Float:
            p.drawRoundedRect(QRectF(2.5, 5.5, 8, 8), 1, 1);
            p.drawLine(QPointF(7.5, 2.5), QPointF(13.5, 2.5));
            p.drawLine(QPointF(13.5, 2.5), QPointF(13.5, 8.5));
            p.drawLine(QPointF(13.5, 2.5), QPointF(8.5, 7.5));
            break;
        case Glyph::Dock:
            p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1, 1);
            p.drawLine(QPointF(2.5, 9.5), QPointF(13.5, 9.5));
            break;
        case Glyph::Max:
            p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1, 1);
            break;
        case Glyph::Restore:
            p.drawRoundedRect(QRectF(2.5, 5.5, 8, 8), 1, 1);
            p.drawLine(QPointF(5.5, 2.5), QPointF(13.5, 2.5));
            p.drawLine(QPointF(13.5, 2.5), QPointF(13.5, 10.5));
            break;
        case Glyph::Close:
            p.drawLine(QPointF(4, 4), QPointF(12, 12));
            p.drawLine(QPointF(12, 4), QPointF(4, 12));
            break;
        }
        p.end();
        icon.addPixmap(pm, col == theme::c("#9aa3af") ? QIcon::Normal : QIcon::Active);
    }
    return icon;
}

// A fogantyú: hat pötty (ennél fogva húzható a panel).
class Grip : public QWidget {
public:
    Grip() { setFixedSize(10, 16); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::c("#6b7380"));
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c) p.drawEllipse(QPointF(3 + c * 4, 4 + r * 4), 1.2, 1.2);
    }
};

QToolButton *titleButton(const QString &tip, const char *name)
{
    auto *b = new QToolButton;
    b->setObjectName(name);
    b->setAutoRaise(true);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setIconSize(QSize(14, 14));
    b->setFixedSize(26, 22);
    return b;
}

} // namespace

DockTitleBar::DockTitleBar(QDockWidget *dock) : QWidget(dock), dock_(dock)
{
    setObjectName("dockTitle");
    setAttribute(Qt::WA_StyledBackground);
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(8, 0, 4, 0);
    l->setSpacing(8);
    grip_ = new Grip;
    l->addWidget(grip_);
    title_ = new QLabel;
    title_->setObjectName("dockTitleText");
    l->addWidget(title_);
    extra_ = new QLabel;
    extra_->setObjectName("dockTitleExtra");
    extra_->hide();
    l->addWidget(extra_);
    l->addStretch(1);
    float_ = titleButton(TR("Kiemelés önálló ablakba"), "dockFloat");
    max_ = titleButton(TR("Teljes méret (újra: vissza)"), "dockMax");
    close_ = titleButton(TR("Panel bezárása (az Ablakok menüből visszahozható)"), "dockClose");
    max_->setIcon(glyphIcon(Glyph::Max));
    close_->setIcon(glyphIcon(Glyph::Close));
    l->addWidget(float_);
    l->addWidget(max_);
    l->addWidget(close_);
    connect(float_, &QToolButton::clicked, dock, [dock] { dock->setFloating(!dock->isFloating()); });
    connect(max_, &QToolButton::clicked, this, &DockTitleBar::maximizeRequested);
    connect(close_, &QToolButton::clicked, dock, &QDockWidget::close);
    connect(dock, &QDockWidget::topLevelChanged, this, &DockTitleBar::refresh);
    connect(dock, &QDockWidget::windowTitleChanged, this, &DockTitleBar::refresh);
    connect(dock, &QDockWidget::featuresChanged, this, &DockTitleBar::refresh);
    refresh();
}

void DockTitleBar::refresh()
{
    title_->setText(dock_->windowTitle());
    const bool floating = dock_->isFloating();
    float_->setIcon(glyphIcon(floating ? Glyph::Dock : Glyph::Float));
    float_->setToolTip(floating ? TR("Visszadokkolás a főablakba") : TR("Kiemelés önálló ablakba"));
    const QDockWidget::DockWidgetFeatures f = dock_->features();
    float_->setVisible(f.testFlag(QDockWidget::DockWidgetFloatable));
    close_->setVisible(f.testFlag(QDockWidget::DockWidgetClosable));
    max_->setVisible(!floating);
    grip_->setVisible(f.testFlag(QDockWidget::DockWidgetMovable));
    setCursor(f.testFlag(QDockWidget::DockWidgetMovable) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void DockTitleBar::setExtra(const QString &text, const QString &color)
{
    extra_->setVisible(!text.isEmpty() && !fixed_);
    extra_->setText(text);
    extra_->setStyleSheet(color.isEmpty() ? QString()
                                          : QStringLiteral("QLabel { font-size: 10px; font-weight: 600; border-radius: 8px; padding: 1px 7px; "
                                                           "background: %1; color: %2; }")
                                                .arg(color, theme::hex("#0f1114")));
    theme::rawCss(extra_);
}

void DockTitleBar::setLocked(bool locked)
{
    locked_ = locked;
    refresh();
}

void DockTitleBar::setMaximized(bool on)
{
    max_->setIcon(glyphIcon(on ? Glyph::Restore : Glyph::Max));
    max_->setToolTip(on ? TR("Vissza az elrendezéshez") : TR("Teljes méret (újra: vissza)"));
}

void DockTitleBar::paintEvent(QPaintEvent *e)
{
    if (fixed_) return;
    QPainter p(this);
    p.fillRect(rect(), theme::c("#1f232a"));
    p.fillRect(QRect(0, height() - 1, width(), 1), theme::c("#30353e"));
    QWidget::paintEvent(e);
}

void DockTitleBar::setFixed(bool on)
{
    fixed_ = on;
    for (QWidget *w : {static_cast<QWidget *>(grip_), static_cast<QWidget *>(title_), static_cast<QWidget *>(float_),
                       static_cast<QWidget *>(max_), static_cast<QWidget *>(close_)})
        w->setVisible(!on);
    if (on) extra_->hide();
    if (!on) refresh();
    updateGeometry();
    if (dock_->layout()) dock_->layout()->invalidate();
}

QSize DockTitleBar::sizeHint() const { return fixed_ ? QSize(0, 0) : QSize(QWidget::sizeHint().width(), 28); }
QSize DockTitleBar::minimumSizeHint() const { return fixed_ ? QSize(0, 0) : QSize(80, 28); }

QDockWidget *makeDock(const QString &key, const QString &title, QWidget *content, QWidget *parent)
{
    auto *d = new QDockWidget(title, parent);
    d->setObjectName(QStringLiteral("dock_") + key);
    d->setWidget(content);
    d->setTitleBarWidget(new DockTitleBar(d));
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    return d;
}

DockTitleBar *dockTitle(QDockWidget *d) { return d ? qobject_cast<DockTitleBar *>(d->titleBarWidget()) : nullptr; }
