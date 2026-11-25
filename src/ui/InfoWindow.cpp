#include "InfoWindow.h"

#include "Theme.h"
#include "i18n.h"
#include "core/Vocal.h"
#include "net/ArtistInfo.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QLabel *caption(const QString &t)
{
    auto *c = new QLabel(t);
    c->setObjectName("caption");
    return c;
}

QLabel *badge(const QString &t)
{
    auto *b = new QLabel(t);
    b->setObjectName("infoBadge");
    b->setStyleSheet(QStringLiteral("QLabel { font-size: 10px; font-weight: 600; letter-spacing: 0.5px; color: #bfe0ff; "
                                    "border: 1px solid #2f5f8f; border-radius: 8px; padding: 0 6px; }"));
    return b;
}

QLabel *chip(const QString &t)
{
    auto *c = new QLabel(t);
    c->setObjectName("infoChip");
    c->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: #c9ced6; background: #262b34; border-radius: 9px; padding: 1px 8px; }"));
    return c;
}

QFrame *separator()
{
    auto *s = new QFrame;
    s->setFixedHeight(1);
    s->setStyleSheet(QStringLiteral("background: #30353e;"));
    return s;
}

QString mmss(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString typeName(const ArtistData &d)
{
    const QString t = d.type.toLower();
    if (t == QLatin1String("group")) return TR("Együttes");
    if (t == QLatin1String("person")) return TR("Előadó");
    if (t == QLatin1String("orchestra")) return TR("Zenekar");
    if (t == QLatin1String("choir")) return TR("Kórus");
    if (t.isEmpty()) return d.members > 1 ? TR("Együttes") : QString();
    return d.type;
}

bool isGroup(const ArtistData &d)
{
    const QString t = d.type.toLower();
    return t == QLatin1String("group") || t == QLatin1String("orchestra") || t == QLatin1String("choir") || (t.isEmpty() && d.members > 1);
}

// A fotó négyzetesre vágva, lekerekítve; ha nincs, egy halvány helyőrző.
QLabel *photo(const QString &file)
{
    auto *l = new QLabel;
    l->setObjectName("infoPhoto");
    l->setFixedSize(96, 96);
    l->setAlignment(Qt::AlignCenter);
    QPixmap pm;
    const qreal dpr = 2.0;
    const int px = int(96 * dpr);
    QPixmap round(px, px);
    round.fill(Qt::transparent);
    QPainter p(&round);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, px, px), 10 * dpr, 10 * dpr);
    if (!file.isEmpty() && pm.load(file)) {
        const int side = std::min(pm.width(), pm.height());
        pm = pm.copy((pm.width() - side) / 2, (pm.height() - side) / 2, side, side).scaled(px, px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.setClipPath(path);
        p.drawPixmap(0, 0, pm);
        l->setAccessibleName(TR("Az előadó fotója"));
    } else {
        p.fillPath(path, theme::panel2);
        p.setPen(QPen(theme::muted, 1.6 * dpr, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const QPointF c(px / 2.0, px / 2.0 - 6 * dpr);
        p.drawEllipse(c, 12 * dpr, 12 * dpr);
        p.drawArc(QRectF(px / 2.0 - 24 * dpr, px / 2.0 + 10 * dpr, 48 * dpr, 40 * dpr), 0, 180 * 16);
        l->setAccessibleName(TR("Nincs fotó"));
    }
    p.end();
    round.setDevicePixelRatio(dpr);
    l->setPixmap(round);
    return l;
}

// Kétoszlopos adatsorok: felirat (halvány) és érték.
class Rows {
public:
    explicit Rows(QVBoxLayout *parent)
    {
        grid_ = new QGridLayout;
        grid_->setHorizontalSpacing(12);
        grid_->setVerticalSpacing(6);
        grid_->setColumnMinimumWidth(0, 92);
        grid_->setColumnStretch(1, 1);
        parent->addLayout(grid_);
    }
    void add(const QString &label, const QString &value, const QString &badgeText = {}, bool rich = false)
    {
        if (value.trimmed().isEmpty()) return;
        auto *l = new QLabel(label);
        l->setObjectName("muted");
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto *v = new QLabel(value);
        v->setWordWrap(true);
        v->setTextInteractionFlags(rich ? Qt::TextBrowserInteraction : Qt::TextSelectableByMouse);
        if (rich) v->setOpenExternalLinks(true);
        auto *cell = new QWidget;
        auto *h = new QHBoxLayout(cell);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        if (badgeText.isEmpty()) {
            h->addWidget(v, 1);
        } else {
            h->addWidget(v);
            h->addWidget(badge(badgeText));
            h->addStretch(1);
        }
        grid_->addWidget(l, n_, 0);
        grid_->addWidget(cell, n_, 1);
        ++n_;
    }
    bool empty() const { return n_ == 0; }

private:
    QGridLayout *grid_;
    int n_ = 0;
};

} // namespace

InfoWindow::InfoWindow(Database *db, ArtistInfoService *info, QWidget *parent)
    : QWidget(parent, Qt::Window), db_(db), info_(info)
{
    setObjectName("infoWindow");
    setWindowTitle(TR("Infó"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("#infoWindow { background: #1b1e24; }"));
    setMinimumSize(320, 360);
    resize(420, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(12, 8, 12, 8);
    h->setSpacing(8);
    onTop_ = new QCheckBox(TR("Mindig felül"));
    onTop_->setObjectName("infoOnTop");
    onTop_->setToolTip(TR("Az Infó ablak a többi ablak fölött marad."));
    h->addWidget(onTop_);
    h->addStretch(1);
    auto *sw = new QWidget;
    sw->setObjectName("viewSwitch");
    sw->setAttribute(Qt::WA_StyledBackground);
    auto *sl = new QHBoxLayout(sw);
    sl->setContentsMargins(2, 2, 2, 2);
    sl->setSpacing(2);
    auto mk = [&](const QString &t, const QString &tip, const char *name) {
        auto *b = new QToolButton;
        b->setText(t);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setObjectName(QLatin1String(name));
        sl->addWidget(b);
        return b;
    };
    srcSel_ = mk(TR("Kijelölt"), TR("A hangtárban kijelölt elem adatai"), "infoSelected");
    srcAir_ = mk(TR("Adásban"), TR("Mindig az éppen szóló elem adatai"), "infoOnAir");
    h->addWidget(sw);
    root->addWidget(head);

    scroll_ = new QScrollArea;
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scroll_, 1);

    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *f = new QHBoxLayout(foot);
    f->setContentsMargins(12, 8, 12, 8);
    f->setSpacing(8);
    footer_ = new QLabel;
    footer_->setObjectName("small");
    footer_->setWordWrap(true);
    refresh_ = new QPushButton(TR("Adatok frissítése"));
    refresh_->setObjectName("infoRefresh");
    refresh_->setToolTip(TR("Az előadó és a szám adatainak újbóli lekérése"));
    f->addWidget(footer_, 1);
    f->addWidget(refresh_);
    root->addWidget(foot);

    QSettings s;
    source_ = s.value(QStringLiteral("ui/info/source"), 0).toInt() == 1 ? OnAir : Selected;
    (source_ == OnAir ? srcAir_ : srcSel_)->setChecked(true);
    onTop_->setChecked(s.value(QStringLiteral("ui/info/ontop"), false).toBool());
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop_->isChecked());
    const QByteArray geo = s.value(QStringLiteral("ui/info/geometry")).toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);

    connect(onTop_, &QCheckBox::toggled, this, &InfoWindow::setOnTop);
    connect(srcSel_, &QToolButton::clicked, this, [this] { setSource(Selected); });
    connect(srcAir_, &QToolButton::clicked, this, [this] { setSource(OnAir); });
    connect(refresh_, &QPushButton::clicked, this, [this] {
        if (!itemId_ || !info_) return;
        info_->requestForInfo(itemId_, true);
        requested_ = true;
        rebuild();
    });

    timer_ = new QTimer(this);
    timer_->setInterval(500);
    connect(timer_, &QTimer::timeout, this, &InfoWindow::poll);
    auto *later = new QTimer(this);
    later->setSingleShot(true);
    later->setInterval(150);
    connect(later, &QTimer::timeout, this, [this] {
        if (isVisible()) rebuild();
    });
    if (info_) {
        connect(info_, &ArtistInfoService::artistUpdated, this, [this, later](const QString &key) {
            if (key == artistKey_) later->start();
        });
        connect(info_, &ArtistInfoService::statusChanged, this, [this, later] {
            if (state_ == QLatin1String("loading") || state_ == QLatin1String("data")) later->start();
        });
    }
    connect(db_, &Database::itemsChanged, this, [later] { later->start(); });
    connect(db_, &Database::categoriesChanged, this, [later] { later->start(); });
}

InfoWindow::~InfoWindow() { saveGeometryNow(); }

void InfoWindow::setProviders(Provider selected, Provider onAir)
{
    selected_ = std::move(selected);
    onAir_ = std::move(onAir);
}

void InfoWindow::setSource(Source s)
{
    source_ = s;
    (s == OnAir ? srcAir_ : srcSel_)->setChecked(true);
    QSettings().setValue(QStringLiteral("ui/info/source"), int(s));
    itemId_ = -1;           // a következő lekérdezés mindenképp frissít
    poll();
}

void InfoWindow::selectionChanged()
{
    if (isVisible() && source_ == Selected) poll();
}

void InfoWindow::openWindow()
{
    QSettings().setValue(QStringLiteral("ui/info/open"), true);
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void InfoWindow::setOnTop(bool on)
{
    QSettings().setValue(QStringLiteral("ui/info/ontop"), on);
    const bool vis = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (vis) show();
}

void InfoWindow::saveGeometryNow()
{
    if (isVisible() || !saveGeometry().isEmpty()) QSettings().setValue(QStringLiteral("ui/info/geometry"), saveGeometry());
}

void InfoWindow::closeEvent(QCloseEvent *e)
{
    QSettings().setValue(QStringLiteral("ui/info/open"), false);
    saveGeometryNow();
    QWidget::closeEvent(e);
}

void InfoWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    itemId_ = -1;
    poll();
    timer_->start();
    emit visibilityChanged(true);
}

void InfoWindow::hideEvent(QHideEvent *e)
{
    saveGeometryNow();
    timer_->stop();
    QWidget::hideEvent(e);
    emit visibilityChanged(false);
}

void InfoWindow::poll()
{
    const Provider &p = source_ == OnAir ? onAir_ : selected_;
    const Item it = p ? p() : Item{};
    // A hangtár frissítésekor (pl. amikor az album beíródik) a kijelölés egy pillanatra elveszhet: a legutóbb
    // kijelölt elem marad, amíg másikat nem jelölnek ki.
    if (source_ == Selected && !it.id && itemId_ > 0 && db_->item(itemId_).id) return;
    if (it.id != itemId_) {
        itemId_ = it.id;
        requested_ = false;
        if (scroll_->verticalScrollBar()) scroll_->verticalScrollBar()->setValue(0);
        rebuild();
        return;
    }
    // Lekérés közben: ha a sor kiürült (nem található, hiba), az állapot frissül.
    if (state_ == QLatin1String("loading") && info_) {
        const Item cur = db_->item(itemId_);
        if (!info_->pendingFor(cur.artist, cur.id)) rebuild();
    }
}

void InfoWindow::rebuild()
{
    const Item it = itemId_ > 0 ? db_->item(itemId_) : Item{};
    if (itemId_ > 0 && !it.id) itemId_ = 0;
    if (it.id && !requested_ && info_ && info_->anyEnabled()) {
        // A megjelenített elemhez a hiányzó adatok lekérése (a meglévőt nem kéri újra).
        info_->requestForInfo(it.id, false);
        requested_ = true;
    }
    const int pos = scroll_->verticalScrollBar() ? scroll_->verticalScrollBar()->value() : 0;
    QWidget *old = scroll_->takeWidget();
    scroll_->setWidget(buildContent(it));
    if (old) old->deleteLater();
    if (scroll_->verticalScrollBar()) scroll_->verticalScrollBar()->setValue(pos);
    setWindowTitle(it.id ? TR("Infó – %1").arg(it.artist.trimmed().isEmpty() ? it.title : QStringLiteral("%1 · %2").arg(it.title, it.artist))
                         : TR("Infó"));
    refresh_->setEnabled(it.id && info_ && info_->anyEnabled() && !(info_->primaryName(it.artist).isEmpty() && it.title.isEmpty()));
}

QWidget *InfoWindow::buildContent(const Item &it)
{
    auto *w = new QWidget;
    w->setObjectName("infoContent");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(10);

    auto centered = [&](const QString &title, const QString &text, QPushButton *button) {
        v->addStretch(1);
        auto *t = new QLabel(title);
        t->setAlignment(Qt::AlignCenter);
        t->setWordWrap(true);
        QFont f = t->font();
        f.setWeight(QFont::DemiBold);
        t->setFont(f);
        v->addWidget(t);
        if (!text.isEmpty()) {
            auto *l = new QLabel(text);
            l->setObjectName("small");
            l->setAlignment(Qt::AlignCenter);
            l->setWordWrap(true);
            v->addWidget(l);
        }
        if (button) {
            auto *row = new QHBoxLayout;
            row->addStretch(1);
            row->addWidget(button);
            row->addStretch(1);
            v->addSpacing(4);
            v->addLayout(row);
        }
        v->addStretch(2);
    };

    artistKey_.clear();
    if (!it.id) {
        state_ = QStringLiteral("empty");
        footer_->clear();
        if (source_ == OnAir) centered(TR("Most nem szól semmi"), TR("Amint egy elem adásba kerül, itt látszanak az adatai."), nullptr);
        else centered(TR("Nincs kijelölt elem"), TR("Jelölj ki egy elemet a hangtárban, vagy válts az „Adásban” nézetre."), nullptr);
        return w;
    }

    // ---- A szám ----
    v->addWidget(caption(TR("A SZÁM")));
    auto *title = new QLabel(it.title.isEmpty() ? TR("(cím nélkül)") : it.title);
    title->setObjectName("trackTitle");
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(title);
    QString album;
    int year = 0;
    db_->itemExtra(it.id, &album, &year);
    // Melyik forrásból jött az album és az év (csak akkor jelöljük, ha a tárolt érték egyezik a forráséval).
    QString albumFrom, yearFrom;
    const Database::OnlineMeta mbr = db_->onlineMeta(QStringLiteral("mbr:%1").arg(it.id));
    if (mbr.found) {
        const QJsonObject o = QJsonDocument::fromJson(mbr.data.toUtf8()).object();
        if (!album.isEmpty() && o.value(QStringLiteral("album")).toString() == album) albumFrom = QStringLiteral("MUSICBRAINZ");
        if (year > 0 && o.value(QStringLiteral("year")).toInt() == year) yearFrom = QStringLiteral("MUSICBRAINZ");
    }
    const Database::OnlineMeta adb = db_->onlineMeta(QStringLiteral("t:%1").arg(it.id));
    if (adb.found && albumFrom.isEmpty()) {
        const QJsonObject o = QJsonDocument::fromJson(adb.data.toUtf8()).object();
        if (!album.isEmpty() && o.value(QStringLiteral("album")).toString() == album) albumFrom = QStringLiteral("THEAUDIODB");
    }
    {
        Rows r(v);
        r.add(TR("Előadó"), it.artist);
        r.add(TR("Album"), album, albumFrom);
        r.add(TR("Megjelent"), year > 0 ? QString::number(year) : QString(), yearFrom);
        r.add(TR("Hossz"), it.durationMs > 0 ? mmss(it.durationMs) : QString());
        r.add(TR("Kategória"), it.categoryName);
        if (vocal::counts(it.vocal) || it.vocal == vocal::Instrumental) r.add(TR("Ének"), vocal::name(it.vocal));
    }

    // ---- Az előadó ----
    const QString name = info_ ? info_->primaryName(it.artist) : QString();
    if (name.isEmpty() || !info_) {
        state_ = QStringLiteral("data");
        footer_->setText(TR("Az elemnek nincs előadója."));
        v->addStretch(1);
        return w;
    }
    artistKey_ = ArtistInfoService::keyFor(name);
    const ArtistData d = info_->artist(it.artist);
    const bool enabled = info_->anyEnabled();
    const bool pending = info_->pendingFor(it.artist, it.id);
    v->addWidget(separator());

    if (!d.found) {
        if (!enabled) {
            state_ = QStringLiteral("off");
            auto *b = new QPushButton(TR("Beállítások → Előadói adatok"));
            b->setObjectName("infoSettings");
            b->setProperty("accent", true);
            connect(b, &QPushButton::clicked, this, &InfoWindow::settingsRequested);
            centered(TR("Az online adatok ki vannak kapcsolva"),
                     TR("A program nem kérdezi le a MusicBrainzt, a Wikipédiát és a TheAudioDB-t. A már letöltött adatok "
                        "ennek ellenére is látszanak."),
                     b);
            footer_->clear();
        } else if (pending || !d.known) {
            state_ = QStringLiteral("loading");
            v->addWidget(caption(TR("AZ ELŐADÓ")));
            auto *bar = new QProgressBar;
            bar->setRange(0, 0);
            bar->setTextVisible(false);
            v->addWidget(bar);
            auto *l = new QLabel(TR("Az adatok lekérése folyamatban: %1").arg(name));
            l->setObjectName("small");
            l->setWordWrap(true);
            v->addWidget(l);
            v->addStretch(1);
            const int n = info_->mbPending() + info_->pending();
            footer_->setText(n > 1 ? TR("Lekérés… (még %1 a sorban)").arg(n - 1) : TR("Lekérés…"));
        } else {
            state_ = QStringLiteral("notfound");
            auto *b = new QPushButton(TR("Újrapróbálás"));
            b->setObjectName("infoRetry");
            connect(b, &QPushButton::clicked, refresh_, &QPushButton::click);
            centered(TR("Nincs adat erről az előadóról"),
                     TR("A források nem ismerik a(z) „%1” nevet. Ellenőrizd az előadó mező írásmódját, vagy próbáld újra később.").arg(name), b);
            footer_->setText(d.fetchedAt > 0 ? TR("Utoljára keresve: %1").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(d.fetchedAt), QLocale::ShortFormat))
                                             : QString());
        }
        return w;
    }

    state_ = QStringLiteral("data");
    v->addWidget(caption(TR("AZ ELŐADÓ")));
    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    top->addWidget(photo(d.imageFile), 0, Qt::AlignTop);
    auto *col = new QVBoxLayout;
    col->setSpacing(6);
    auto *an = new QLabel(d.name.isEmpty() ? name : d.name);
    an->setObjectName("trackTitle");
    an->setWordWrap(true);
    an->setTextInteractionFlags(Qt::TextSelectableByMouse);
    col->addWidget(an);
    QStringList chips;
    if (const QString t = typeName(d); !t.isEmpty()) chips << t;
    for (const QString &g : (d.genre + QLatin1Char(',') + d.style).split(QRegularExpression(QStringLiteral("[,/;]")), Qt::SkipEmptyParts)) {
        const QString x = g.trimmed();
        bool dup = false;
        for (const QString &c : chips) dup = dup || c.compare(x, Qt::CaseInsensitive) == 0;
        if (!x.isEmpty() && !dup && chips.size() < 5) chips << x;
    }
    if (!chips.isEmpty()) {
        auto *cw = new QWidget;
        auto *cl = new QHBoxLayout(cw);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(5);
        int width = 0;
        for (const QString &c : chips) {
            // Egy sorban, ami elfér (kb. 230 px); a többi a tooltipben.
            width += QFontMetrics(font()).horizontalAdvance(c) + 22;
            if (width > 250 && cl->count() > 0) break;
            cl->addWidget(chip(c));
        }
        cl->addStretch(1);
        cw->setToolTip(chips.join(QStringLiteral(" · ")));
        col->addWidget(cw);
    }
    col->addStretch(1);
    top->addLayout(col, 1);
    v->addLayout(top);
    {
        Rows r(v);
        const bool group = isGroup(d);
        r.add(TR("Származás"), d.country);
        if (group) r.add(TR("Alakult"), d.formed > 0 ? QString::number(d.formed) : QString());
        else r.add(TR("Született"), d.born > 0 ? QString::number(d.born) : (d.formed > 0 ? QString::number(d.formed) : QString()));
        if (group) r.add(TR("Feloszlott"), d.disbanded);
        else r.add(TR("Elhunyt"), d.died > 0 ? QString::number(d.died) : QString());
        r.add(TR("Tagok"), d.members > 1 ? QString::number(d.members) : QString());
        r.add(TR("Kiadó"), d.label);
        r.add(TR("Hangulat"), d.mood);
        if (!d.website.isEmpty()) {
            QString shown = d.website;
            shown.remove(QRegularExpression(QStringLiteral("^https?://(www\\.)?"))).remove(QRegularExpression(QStringLiteral("/$")));
            const QString url = d.website.startsWith(QLatin1String("http")) ? d.website : QStringLiteral("https://") + d.website;
            r.add(TR("Weboldal"), QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), shown.toHtmlEscaped()), {}, true);
        }
    }

    if (!d.bio.trimmed().isEmpty()) {
        v->addWidget(separator());
        v->addWidget(caption(TR("ÉLETRAJZ")));
        auto *bio = new QLabel(d.bio.trimmed());
        bio->setObjectName("infoBio");
        bio->setWordWrap(true);
        bio->setTextFormat(Qt::PlainText);
        bio->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bio->setStyleSheet(QStringLiteral("QLabel { color: #d4d8de; line-height: 150%; }"));
        v->addWidget(bio);
        if (!d.wikiUrl.isEmpty()) {
            auto *link = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>").arg(d.wikiUrl.toHtmlEscaped(), TR("Tovább a Wikipédián")));
            link->setObjectName("infoWikiLink");
            link->setOpenExternalLinks(true);
            link->setTextInteractionFlags(Qt::TextBrowserInteraction);
            v->addWidget(link);
        }
    }
    v->addStretch(1);

    QStringList src = d.sources;
    if (src.contains(TR("Wikipédia"))) src.replace(src.indexOf(TR("Wikipédia")), TR("Wikipédia (CC BY-SA)"));
    QStringList parts;
    if (!src.isEmpty()) parts << TR("Forrás: %1").arg(src.join(QStringLiteral(", ")));
    if (d.fetchedAt > 0) parts << TR("lekérve: %1").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(d.fetchedAt), QLocale::ShortFormat));
    if (pending) parts << TR("frissítés folyamatban…");
    if (!enabled) parts << TR("a források ki vannak kapcsolva");
    footer_->setText(parts.join(QStringLiteral(" · ")));
    return w;
}
