#include "ui/StreamPage.h"

#include "audio/Engine.h"
#include "core/NowPlaying.h"
#include "i18n.h"
#include "stream/StreamManager.h"
#include "ui/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using stream::Config;
using stream::Encoder;
using stream::Format;
using stream::Server;
using stream::State;

namespace {

const int kRates[] = {32, 48, 64, 96, 128, 160, 192, 256, 320};

// Két vízszintes szintmérő sáv (bal, jobb), dB skálán, lassú visszaeséssel.
class LevelBars : public QWidget {
public:
    LevelBars() { setMinimumHeight(18); }
    void push(float l, float r)
    {
        auto upd = [](float &v, float p) { v = p > v ? p : v * 0.82f; };
        upd(l_, l);
        upd(r_, r);
        update();
    }
    void reset()
    {
        l_ = r_ = 0;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int h = (height() - 4) / 2;
        auto bar = [&](int y, float v) {
            const QRect r(0, y, width(), h);
            p.fillRect(r, theme::c("#121418"));
            const double db = v > 1e-5f ? 20.0 * std::log10(v) : -60.0;
            const double f = std::clamp((db + 48.0) / 48.0, 0.0, 1.0);
            const int w = int(f * width());
            const int warnX = int((48.0 - 6.0) / 48.0 * width());
            p.fillRect(QRect(0, y, std::min(w, warnX), h), theme::c("#45d483"));
            if (w > warnX) p.fillRect(QRect(warnX, y, w - warnX, h), theme::c("#f5a524"));
        };
        bar(0, l_);
        bar(h + 4, r_);
    }

private:
    float l_ = 0, r_ = 0;
};

// A kifelé menő sávszél sávja: enkóderenként egy szelet a feltöltési kerethez mérve.
class BandBar : public QWidget {
public:
    BandBar() { setFixedHeight(12); }
    void set(const QVector<int> &parts, int limit)
    {
        parts_ = parts;
        limit_ = limit;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(theme::c("#30353e"));
        p.setBrush(theme::c("#121418"));
        p.drawRoundedRect(r, 5, 5);
        int total = 0;
        for (int v : parts_) total += v;
        const double scale = limit_ > 0 ? double(limit_) : std::max(total, 1);
        p.setClipRect(r.adjusted(1, 1, -1, -1));
        if (limit_ > 0 && total > limit_) {
            p.fillRect(r, theme::c("#ff8f85"));
            return;
        }
        static const char *cols[] = {"#f5a524", "#5cb0ff", "#3fd0b5", "#c792ea", "#45d483"};
        double x = r.left() + 1;
        const double w = r.width() - 2;
        for (int i = 0; i < parts_.size(); ++i) {
            const double pw = parts_[i] / scale * w;
            p.fillRect(QRectF(x, r.top() + 1, pw, r.height() - 2), theme::c(cols[i % 5]));
            x += pw;
        }
    }

private:
    QVector<int> parts_;
    int limit_ = 0;
};

QLabel *caption(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("caption");
    return l;
}

QLabel *small(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("small");
    l->setWordWrap(true);
    return l;
}

QString duration(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString megabytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024) return QLocale().toString(bytes / 1073741824.0, 'f', 2) + QStringLiteral(" GB");
    return QLocale().toString(bytes / 1048576.0, 'f', 1) + QStringLiteral(" MB");
}

QWidget *segment(QButtonGroup *g, const QStringList &labels)
{
    auto *w = new QWidget;
    w->setObjectName("segment");
    w->setAttribute(Qt::WA_StyledBackground);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(2);
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QToolButton;
        b->setText(labels[i]);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::TabFocus);
        g->addButton(b, i);
        l->addWidget(b);
    }
    g->setExclusive(true);
    w->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return w;
}

QFrame *section()
{
    auto *f = new QFrame;
    f->setObjectName("streamSection");
    f->setStyleSheet(QStringLiteral("QFrame#streamSection { border: 0; }"));
    return f;
}

} // namespace

StreamPage::StreamPage(StreamManager *mgr, eter::Engine *engine, QWidget *parent) : QWidget(parent), mgr_(mgr), engine_(engine)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // ------------------------------------------------------------ bal oldal: enkóderek és sávszél
    auto *left = new QWidget;
    left->setMinimumWidth(340);
    left->setMaximumWidth(470);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(10);

    auto *encPanel = new QFrame;
    encPanel->setObjectName("panel");
    auto *ep = new QVBoxLayout(encPanel);
    ep->setContentsMargins(0, 0, 0, 0);
    ep->setSpacing(0);
    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(12, 8, 10, 8);
    auto *title = new QLabel(TR("Enkóderek"));
    title->setObjectName("panelTitle");
    hl->addWidget(title, 1);
    auto *addBtn = new QPushButton(TR("+ Új enkóder"));
    addBtn->setObjectName("streamAdd");
    hl->addWidget(addBtn);
    ep->addWidget(head);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *cardsHost = new QWidget;
    cardsLayout_ = new QVBoxLayout(cardsHost);
    cardsLayout_->setContentsMargins(10, 10, 10, 10);
    cardsLayout_->setSpacing(8);
    cardsLayout_->addStretch(1);
    scroll->setWidget(cardsHost);
    ep->addWidget(scroll, 1);
    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(10, 8, 10, 8);
    auto *startAll = new QPushButton(TR("Mind indítása"));
    auto *stopAll = new QPushButton(TR("Mind leállítása"));
    fl->addWidget(startAll);
    fl->addWidget(stopAll);
    ep->addWidget(foot);
    ll->addWidget(encPanel, 1);

    bwFrame_ = new QFrame;
    bwFrame_->setObjectName("panel");
    auto *bl = new QVBoxLayout(bwFrame_);
    bl->setContentsMargins(12, 10, 12, 12);
    bl->setSpacing(8);
    bl->addWidget(caption(TR("KIFELÉ MENŐ SÁVSZÉL")));
    auto *totRow = new QHBoxLayout;
    bwTotal_ = new QLabel;
    bwTotal_->setTextFormat(Qt::RichText);
    totRow->addWidget(bwTotal_);
    totRow->addStretch(1);
    bwHour_ = new QLabel;
    bwHour_->setObjectName("small");
    totRow->addWidget(bwHour_);
    bl->addLayout(totRow);
    auto *bar = new BandBar;
    bwBar_ = bar;
    bl->addWidget(bar);
    auto *limRow = new QHBoxLayout;
    limRow->addWidget(new QLabel(TR("Feltöltési keret")));
    limit_ = new QSpinBox;
    limit_->setObjectName("uploadLimit");
    limit_->setRange(0, 1000000);
    limit_->setSingleStep(100);
    limit_->setSpecialValueText(TR("nincs megadva"));
    limit_->setSuffix(QStringLiteral(" kbit/s"));
    limit_->setValue(mgr_->uploadLimitKbps());
    limit_->setToolTip(TR("Az internetkapcsolatod feltöltési sebessége (vagy amennyit a streamre szánsz). Csak figyelmeztet, "
                          "nem korlátoz. 0: nincs megadva."));
    limRow->addWidget(limit_);
    limRow->addStretch(1);
    bwPercent_ = new QLabel;
    bwPercent_->setObjectName("small");
    limRow->addWidget(bwPercent_);
    bl->addLayout(limRow);
    bwWarn_ = small(QString());
    bwWarn_->setStyleSheet(QStringLiteral("color: #ffb3ac;"));
    bl->addWidget(bwWarn_);
    bl->addWidget(small(TR("A szerver felé minden enkóderből egy példány megy; a hallgatók forgalmát a szerver viseli.")));
    ll->addWidget(bwFrame_);
    root->addWidget(left);

    // ------------------------------------------------------------ jobb oldal: a kijelölt enkóder
    rightStack_ = new QStackedWidget;
    auto *empty = new QFrame;
    empty->setObjectName("panel");
    {
        auto *el = new QVBoxLayout(empty);
        el->setContentsMargins(40, 40, 40, 40);
        el->addStretch(1);
        auto *t = new QLabel(TR("Még nincs stream enkóder"));
        t->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
        t->setAlignment(Qt::AlignCenter);
        el->addWidget(t);
        auto *d = small(TR("Az enkóder a program hangját (vagy egy hangkártya-bemenetet, például a keverőpultot) MP3-ba vagy "
                           "Ogg Vorbisba kódolja, és elküldi egy Icecast vagy Shoutcast szervernek – innen hallgatják a rádiót "
                           "az interneten. Több enkóder is futhat egyszerre (például egy 128 kbit/s-os MP3 és egy 64 kbit/s-os "
                           "mobil stream)."));
        d->setAlignment(Qt::AlignCenter);
        d->setFixedWidth(560);
        d->setMinimumHeight(d->heightForWidth(560));
        el->addSpacing(6);
        el->addWidget(d, 0, Qt::AlignHCenter);
        el->addSpacing(10);
        auto *b = new QPushButton(TR("+ Új enkóder"));
        b->setProperty("accent", true);
        el->addWidget(b, 0, Qt::AlignHCenter);
        connect(b, &QPushButton::clicked, addBtn, &QPushButton::click);
        el->addStretch(2);
    }
    rightStack_->addWidget(empty);

    auto *editor = new QFrame;
    editor->setObjectName("panel");
    auto *er = new QVBoxLayout(editor);
    er->setContentsMargins(0, 0, 0, 0);
    er->setSpacing(0);

    auto *top = new QFrame;
    top->setObjectName("panelHeader");
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(14, 8, 14, 8);
    tl->setSpacing(10);
    tl->addWidget(new QLabel(TR("Név")));
    name_ = new QLineEdit;
    name_->setObjectName("streamName");
    name_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600;"));
    tl->addWidget(name_, 1);
    autoStart_ = new QCheckBox(TR("Indítás a program indulásakor"));
    autoStart_->setObjectName("streamAutoStart");
    tl->addWidget(autoStart_);
    test_ = new QPushButton(TR("Kapcsolat tesztelése"));
    test_->setObjectName("streamTest");
    test_->setToolTip(TR("Kapcsolódik és bejelentkezik a szerverre, majd bont – hang nem megy ki."));
    tl->addWidget(test_);
    delete_ = new QPushButton(TR("Törlés"));
    delete_->setProperty("danger", true);
    tl->addWidget(delete_);
    er->addWidget(top);

    restartBar_ = new QFrame;
    restartBar_->setStyleSheet(QStringLiteral("QFrame { background: #2d2210; border: 0; border-bottom: 1px solid #8a6417; }"));
    {
        auto *rl = new QHBoxLayout(restartBar_);
        rl->setContentsMargins(14, 6, 14, 6);
        auto *t = new QLabel(TR("A szerver- és kódolási beállítások az újraindításkor lépnek életbe."));
        t->setStyleSheet(QStringLiteral("color: #ffd28a; background: transparent; border: 0;"));
        rl->addWidget(t, 1);
        auto *b = new QPushButton(TR("Újraindítás most"));
        b->setObjectName("streamRestart");
        rl->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this] {
            if (Encoder *e = selected()) {
                e->stop();
                e->start();
                restartBar_->hide();
            }
        });
    }
    restartBar_->hide();
    er->addWidget(restartBar_);

    auto *formScroll = new QScrollArea;
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    auto *form = new QWidget;
    auto *grid = new QGridLayout(form);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    formScroll->setWidget(form);
    er->addWidget(formScroll, 1);

    const QString sep = QStringLiteral("QFrame#streamSection { border: 0; border-color: #30353e; border-style: solid; %1 }");

    // Szerver
    {
        auto *f = section();
        f->setStyleSheet(sep.arg(QStringLiteral("border-right-width: 1px; border-bottom-width: 1px;")));
        auto *v = new QVBoxLayout(f);
        v->setContentsMargins(14, 12, 14, 14);
        v->setSpacing(10);
        v->addWidget(caption(TR("SZERVER")));
        serverGroup_ = new QButtonGroup(this);
        v->addWidget(segment(serverGroup_, {QStringLiteral("Icecast 2"), QStringLiteral("Shoutcast v1"), QStringLiteral("Shoutcast v2")}));
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnMinimumWidth(0, 110);
        g->addWidget(new QLabel(TR("Szerver címe")), 0, 0);
        auto *hp = new QHBoxLayout;
        host_ = new QLineEdit;
        host_->setObjectName("streamHost");
        host_->setPlaceholderText(TR("pl. stream.radio.hu"));
        hp->addWidget(host_, 1);
        port_ = new QSpinBox;
        port_->setObjectName("streamPort");
        port_->setRange(1, 65534);
        port_->setToolTip(TR("Shoutcastnál a szerver portja; a forrás a következő porton (port + 1) csatlakozik."));
        hp->addWidget(port_);
        g->addLayout(hp, 0, 1);
        mountLabel_ = new QLabel(TR("Mountpoint"));
        g->addWidget(mountLabel_, 1, 0);
        mount_ = new QLineEdit;
        mount_->setObjectName("streamMount");
        g->addWidget(mount_, 1, 1);
        sidLabel_ = new QLabel(TR("Stream azonosító (SID)"));
        g->addWidget(sidLabel_, 2, 0);
        sid_ = new QSpinBox;
        sid_->setRange(1, 999);
        g->addWidget(sid_, 2, 1, Qt::AlignLeft);
        userLabel_ = new QLabel(TR("Felhasználó"));
        g->addWidget(userLabel_, 3, 0);
        user_ = new QLineEdit;
        user_->setObjectName("streamUser");
        g->addWidget(user_, 3, 1);
        g->addWidget(new QLabel(TR("Jelszó")), 4, 0);
        password_ = new QLineEdit;
        password_->setObjectName("streamPassword");
        password_->setEchoMode(QLineEdit::Password);
        g->addWidget(password_, 4, 1);
        tls_ = new QCheckBox(TR("Titkosított kapcsolat (HTTPS)"));
        g->addWidget(tls_, 5, 1);
        v->addLayout(g);
        serverNote_ = small(QString());
        v->addWidget(serverNote_);
        v->addStretch(1);
        grid->addWidget(f, 0, 0);
    }

    // Kódolás és sávszél
    {
        auto *f = section();
        f->setStyleSheet(sep.arg(QStringLiteral("border-bottom-width: 1px;")));
        auto *v = new QVBoxLayout(f);
        v->setContentsMargins(14, 12, 14, 14);
        v->setSpacing(10);
        v->addWidget(caption(TR("KÓDOLÁS ÉS SÁVSZÉL")));
        formatGroup_ = new QButtonGroup(this);
        v->addWidget(segment(formatGroup_, {QStringLiteral("MP3"), QStringLiteral("Ogg Vorbis")}));
        v->addWidget(new QLabel(TR("Bitráta (kbit/s)")));
        auto *rates = new QHBoxLayout;
        rates->setSpacing(5);
        rateGroup_ = new QButtonGroup(this);
        rateGroup_->setExclusive(true);
        for (int r : kRates) {
            auto *b = new QToolButton;
            b->setObjectName("rateBtn");
            b->setText(QString::number(r));
            b->setCheckable(true);
            rateGroup_->addButton(b, r);
            rates->addWidget(b);
        }
        rates->addStretch(1);
        v->addLayout(rates);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnMinimumWidth(0, 110);
        g->addWidget(new QLabel(TR("Mód")), 0, 0);
        mode_ = new QComboBox;
        mode_->setObjectName("streamMode");
        g->addWidget(mode_, 0, 1);
        g->addWidget(new QLabel(TR("Mintavétel")), 1, 0);
        rateHz_ = new QComboBox;
        rateHz_->setObjectName("streamSampleRate");
        for (int hz : {22050, 32000, 44100, 48000})
            rateHz_->addItem(QLocale().toString(hz / 1000.0, 'g', 4) + QStringLiteral(" kHz"), hz);
        g->addWidget(rateHz_, 1, 1);
        g->addWidget(new QLabel(TR("Csatornák")), 2, 0);
        auto *chs = new QHBoxLayout;
        chs->setSpacing(18);
        stereo_ = new QRadioButton(TR("Sztereó"));
        mono_ = new QRadioButton(TR("Mono"));
        mono_->setObjectName("streamMono");
        chs->addWidget(stereo_);
        chs->addWidget(mono_);
        chs->addStretch(1);
        g->addLayout(chs, 2, 1);
        v->addLayout(g);
        info_ = new QLabel;
        info_->setTextFormat(Qt::RichText);
        info_->setStyleSheet(QStringLiteral("QLabel { background: #16181d; border: 1px solid #30353e; border-radius: 5px; padding: 8px 10px; color: #c9ced6; }"));
        v->addWidget(info_);
        codecNote_ = small(QString());
        v->addWidget(codecNote_);
        v->addStretch(1);
        grid->addWidget(f, 0, 1);
    }

    // Hangforrás
    {
        auto *f = section();
        f->setStyleSheet(sep.arg(QStringLiteral("border-right-width: 1px;")));
        auto *v = new QVBoxLayout(f);
        v->setContentsMargins(14, 12, 14, 14);
        v->setSpacing(10);
        v->addWidget(caption(TR("HANGFORRÁS")));
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnMinimumWidth(0, 110);
        g->addWidget(new QLabel(TR("Forrás")), 0, 0);
        auto *sr = new QHBoxLayout;
        source_ = new QComboBox;
        source_->setObjectName("streamSource");
        source_->setToolTip(TR("Fő kimenet: amit a program ad (lejátszók, voice track, cartwall), a fő kimenet szintjével. "
                               "Bemenet: egy hangkártya bemenete – például ha a keverőpult kimenete megy a gépbe, és a "
                               "mikrofonok is a pulton vannak."));
        sr->addWidget(source_, 1);
        auto *refresh = new QToolButton;
        refresh->setText(QStringLiteral("↻"));
        refresh->setToolTip(TR("A bemenetek listájának frissítése"));
        refresh->setAutoRaise(true);
        sr->addWidget(refresh);
        g->addLayout(sr, 0, 1);
        g->addWidget(new QLabel(TR("Szint a streamre")), 1, 0);
        auto *gr = new QHBoxLayout;
        gain_ = new QSlider(Qt::Horizontal);
        gain_->setObjectName("streamGain");
        gain_->setRange(-24, 24);       // fél dB-es lépések
        gr->addWidget(gain_, 1);
        gainLabel_ = new QLabel;
        gainLabel_->setMinimumWidth(56);
        gainLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gr->addWidget(gainLabel_);
        g->addLayout(gr, 1, 1);
        g->addWidget(new QLabel(TR("Szintmérő")), 2, 0);
        meter_ = new LevelBars;
        g->addWidget(meter_, 2, 1);
        v->addLayout(g);
        auto *limRow = new QHBoxLayout;
        limiter_ = new QCheckBox(TR("Csúcskorlátozó a kódolás előtt (ne torzítson a stream)"));
        limRow->addWidget(limiter_, 1);
        reduction_ = new QLabel;
        reduction_->setObjectName("small");
        limRow->addWidget(reduction_);
        v->addLayout(limRow);
        v->addStretch(1);
        grid->addWidget(f, 1, 0);
        connect(refresh, &QToolButton::clicked, this, [this] { fillSources(editorConfig().source); });
    }

    // Cím és állomásadatok
    {
        auto *f = section();
        auto *v = new QVBoxLayout(f);
        v->setContentsMargins(14, 12, 14, 14);
        v->setSpacing(10);
        v->addWidget(caption(TR("CÍM ÉS ÁLLOMÁSADATOK")));
        sendTitle_ = new QCheckBox(TR("Az adásban lévő cím küldése a hallgatóknak"));
        sendTitle_->setObjectName("streamSendTitle");
        v->addWidget(sendTitle_);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnMinimumWidth(0, 110);
        g->addWidget(new QLabel(TR("Cím sablonja")), 0, 0);
        auto *tr = new QHBoxLayout;
        titleFormat_ = new QLineEdit;
        titleFormat_->setObjectName("streamTitleFormat");
        titleFormat_->setToolTip(TR("%artist%, %title%, %category%, %duration%, %next_artist%, %next_title%, %time%"));
        tr->addWidget(titleFormat_, 1);
        likeNp_ = new QCheckBox(TR("mint a Now playing"));
        likeNp_->setToolTip(TR("A Beállítások → Now playing sablonja és szabályai (jingle alatt mi látszik, szünetben mi)."));
        tr->addWidget(likeNp_);
        g->addLayout(tr, 0, 1);
        auto addRow = [&](int row, const QString &label, QLineEdit *&edit, const char *obj, const QString &ph) {
            g->addWidget(new QLabel(label), row, 0);
            edit = new QLineEdit;
            edit->setObjectName(obj);
            edit->setPlaceholderText(ph);
            g->addWidget(edit, row, 1);
        };
        addRow(1, TR("Állomás neve"), station_, "streamStation", QString());
        addRow(2, TR("Leírás"), desc_, "streamDescription", QString());
        addRow(3, TR("Műfaj"), genre_, "streamGenre", TR("pl. Pop, 80s"));
        addRow(4, TR("Weboldal"), url_, "streamUrl", QStringLiteral("https://"));
        public_ = new QCheckBox(TR("Nyilvános (megjelenhet a stream-katalógusokban)"));
        g->addWidget(public_, 5, 1);
        v->addLayout(g);
        titleNote_ = small(QString());
        v->addWidget(titleNote_);
        v->addStretch(1);
        grid->addWidget(f, 1, 1);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    // Kapcsolat és napló
    auto *bottom = new QFrame;
    bottom->setObjectName("panelFooter");
    {
        auto *v = new QVBoxLayout(bottom);
        v->setContentsMargins(14, 10, 14, 12);
        v->setSpacing(8);
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        row->addWidget(caption(TR("KAPCSOLAT")));
        reconnect_ = new QCheckBox(TR("Megszakadáskor újracsatlakozás"));
        row->addWidget(reconnect_);
        row->addWidget(new QLabel(TR("ennyi másodperc után:")));
        retry_ = new QSpinBox;
        retry_->setRange(1, 300);
        row->addWidget(retry_);
        row->addStretch(1);
        auto *logNote = small(TR("A hibák a hibanaplóba is bekerülnek."));
        logNote->setWordWrap(false);
        row->addWidget(logNote);
        v->addLayout(row);
        log_ = new QPlainTextEdit;
        log_->setObjectName("streamLog");
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(200);
        log_->setFixedHeight(96);
        log_->setStyleSheet(QStringLiteral("QPlainTextEdit { font-family: 'IBM Plex Mono', monospace; font-size: 12px; background: #121418; }"));
        v->addWidget(log_);
    }
    er->addWidget(bottom);
    rightStack_->addWidget(editor);
    root->addWidget(rightStack_, 1);

    // ------------------------------------------------------------ kapcsolatok
    applyTimer_ = new QTimer(this);
    applyTimer_->setSingleShot(true);
    applyTimer_->setInterval(300);
    connect(applyTimer_, &QTimer::timeout, this, &StreamPage::applyEditor);
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(200);
    connect(statusTimer_, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        updateCards();
    });
    statusTimer_->start();

    connect(addBtn, &QPushButton::clicked, this, [this] {
        Encoder *e = mgr_->add();
        selectEncoder(e->config().id);
        host_->setFocus();
    });
    connect(startAll, &QPushButton::clicked, mgr_, &StreamManager::startAll);
    connect(stopAll, &QPushButton::clicked, mgr_, &StreamManager::stopAll);
    connect(limit_, &QSpinBox::valueChanged, this, [this](int v) {
        mgr_->setUploadLimitKbps(v);
        updateBandwidth();
    });
    connect(mgr_, &StreamManager::listChanged, this, [this] {
        if (cards_.size() != mgr_->encoders().size()) rebuildCards();
        updateCards();
    });
    connect(mgr_, &StreamManager::statusChanged, this, &StreamPage::updateCards);
    connect(mgr_, &StreamManager::event, this, &StreamPage::appendEvent);
    connect(test_, &QPushButton::clicked, this, [this] {
        applyEditor();
        if (Encoder *e = selected()) {
            if (e->active()) {
                emit message(TR("Az enkóder fut: a kapcsolat láthatóan működik. A teszthez előbb állítsd le."));
                return;
            }
            e->test();
        }
    });
    connect(delete_, &QPushButton::clicked, this, [this] {
        Encoder *e = selected();
        if (!e) return;
        if (QMessageBox::question(this, TR("Enkóder törlése"), TR("Törlöd ezt az enkódert: %1?").arg(e->config().name)) != QMessageBox::Yes)
            return;
        const QString id = e->config().id;
        selected_.clear();
        mgr_->remove(id);
        if (!mgr_->encoders().isEmpty()) selectEncoder(mgr_->encoders().first()->config().id);
        else loadEditor();
    });

    for (QLineEdit *e : {name_, host_, mount_, user_, password_, titleFormat_, station_, desc_, genre_, url_})
        connect(e, &QLineEdit::textEdited, this, &StreamPage::scheduleApply);
    for (QCheckBox *c : {autoStart_, tls_, limiter_, sendTitle_, likeNp_, public_, reconnect_})
        connect(c, &QCheckBox::toggled, this, &StreamPage::scheduleApply);
    for (QSpinBox *s : {port_, sid_, retry_}) connect(s, &QSpinBox::valueChanged, this, &StreamPage::scheduleApply);
    for (QComboBox *c : {mode_, rateHz_, source_}) connect(c, &QComboBox::activated, this, &StreamPage::scheduleApply);
    connect(stereo_, &QRadioButton::toggled, this, &StreamPage::scheduleApply);
    for (QButtonGroup *g : {serverGroup_, formatGroup_, rateGroup_}) connect(g, &QButtonGroup::idClicked, this, &StreamPage::scheduleApply);
    connect(gain_, &QSlider::valueChanged, this, [this](int v) {
        gainLabel_->setText(QStringLiteral("%1%2 dB").arg(v > 0 ? QStringLiteral("+") : QString()).arg(QLocale().toString(v / 2.0, 'f', v % 2 ? 1 : 0)));
        scheduleApply();
    });
    // A beállítás azonnal (a szint és a forrás élőben hat), de a mentés kicsit késleltetve.
    connect(formatGroup_, &QButtonGroup::idClicked, this, [this] { updateEditorState(); });
    connect(serverGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        // Új szervertípusnál az ahhoz illő alapérték (ha a mező még az előző alapértéket mutatja).
        if (id != 0 && formatGroup_->checkedId() == 1) formatGroup_->button(0)->setChecked(true);
        updateEditorState();
    });
    connect(likeNp_, &QCheckBox::toggled, this, [this] { updateEditorState(); });
    connect(sendTitle_, &QCheckBox::toggled, this, [this] { updateEditorState(); });

    rebuildCards();
    if (!mgr_->encoders().isEmpty()) selectEncoder(mgr_->encoders().first()->config().id);
    else loadEditor();
}

Encoder *StreamPage::selected() const { return mgr_->find(selected_); }

void StreamPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    updateCards();
}

// ------------------------------------------------------------------------------------------ kártyák

void StreamPage::rebuildCards()
{
    for (Card &c : cards_) c.frame->deleteLater();
    cards_.clear();
    int at = 0;
    for (Encoder *e : mgr_->encoders()) {
        Card c;
        c.id = e->config().id;
        c.frame = new QFrame;
        c.frame->setObjectName("streamCard");
        c.frame->setCursor(Qt::PointingHandCursor);
        c.frame->installEventFilter(this);
        auto *v = new QVBoxLayout(c.frame);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(6);
        auto *r1 = new QHBoxLayout;
        c.name = new QLabel;
        c.name->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px; background: transparent;"));
        r1->addWidget(c.name, 1);
        c.pill = new QLabel;
        r1->addWidget(c.pill);
        v->addLayout(r1);
        c.server = new QLabel;
        c.server->setStyleSheet(QStringLiteral("font-family: 'IBM Plex Mono', monospace; font-size: 11px; color: #c9ced6; background: transparent;"));
        v->addWidget(c.server);
        auto *chips = new QHBoxLayout;
        chips->setSpacing(5);
        auto chip = [&](QLabel *&l, bool strong) {
            l = new QLabel;
            l->setStyleSheet(QStringLiteral("font-size: 11px; border-radius: 8px; padding: 1px 8px; background: #262b34; color: %1; %2")
                                 .arg(strong ? QStringLiteral("#e8eaed") : QStringLiteral("#c9ced6"),
                                      strong ? QStringLiteral("font-weight: 600;") : QString()));
            chips->addWidget(l);
        };
        chip(c.format, true);
        chip(c.rate, false);
        chip(c.audio, false);
        chips->addStretch(1);
        v->addLayout(chips);
        auto *r3 = new QHBoxLayout;
        c.stats = new QLabel;
        c.stats->setObjectName("small");
        c.stats->setWordWrap(true);
        c.stats->setStyleSheet(QStringLiteral("background: transparent;"));
        r3->addWidget(c.stats, 1);
        c.button = new QPushButton;
        c.button->setObjectName(QStringLiteral("streamToggle_%1").arg(at));
        r3->addWidget(c.button);
        v->addLayout(r3);
        const QString id = c.id;
        connect(c.button, &QPushButton::clicked, this, [this, id] {
            Encoder *e = mgr_->find(id);
            if (!e) return;
            if (id == selected_) applyEditor();
            if (e->active()) e->stop();
            else e->start();
            updateCards();
        });
        cardsLayout_->insertWidget(at++, c.frame);
        cards_ << c;
    }
    updateCards();
}

bool StreamPage::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress)
        for (const Card &c : cards_)
            if (c.frame == o) {
                selectEncoder(c.id);
                return false;
            }
    return QWidget::eventFilter(o, e);
}

void StreamPage::updateCards()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Card &c : cards_) {
        Encoder *e = mgr_->find(c.id);
        if (!e) continue;
        const Config &cfg = e->config();
        const bool sel = c.id == selected_;
        const stream::Status st = sel ? e->status() : stream::Status{};
        const State state = sel ? st.state : e->state();
        const bool active = e->active();
        c.frame->setStyleSheet(QStringLiteral("QFrame#streamCard { background: %1; border: 1px solid %2; border-radius: 6px; }")
                                   .arg(sel ? QStringLiteral("#23303f") : QStringLiteral("#1f232a"),
                                        sel ? theme::amber.name() : QStringLiteral("#30353e")));
        theme::rawCss(c.frame);
        c.name->setText(cfg.name.isEmpty() ? TR("(névtelen)") : cfg.name);
        c.server->setText(cfg.endpoint());
        c.format->setText(cfg.formatText());
        c.rate->setText(QStringLiteral("%1 kbit/s").arg(cfg.kbps));
        c.audio->setText(cfg.audioText());
        const char *pillBg = "#262b34", *pillFg = "#c9ced6";
        switch (state) {
        case State::OnAir: pillBg = "#13261c"; pillFg = "#8ff0bb"; break;
        case State::Connecting: pillBg = "#14283d"; pillFg = "#bfe0ff"; break;
        case State::Retrying: pillBg = "#2d2210"; pillFg = "#ffd28a"; break;
        case State::Error: pillBg = "#2e1514"; pillFg = "#ffb3ac"; break;
        default: break;
        }
        c.pill->setText(stream::stateLabel(state));
        c.pill->setStyleSheet(QStringLiteral("font-size: 10px; font-weight: 600; letter-spacing: 1px; border-radius: 8px; padding: 1px 8px; background: %1; color: %2;")
                                  .arg(theme::hex(pillBg), theme::hex(pillFg)));
        theme::rawCss(c.pill);
        QString stats;
        const stream::Status full = sel ? st : stream::Status{};
        if (state == State::OnAir) {
            // A nem kijelölt kártyán is kell az idő és a forgalom (a csúcsszint nullázódása itt nem számít: nincs szintmérője).
            stream::Status s2 = sel ? full : e->status();
            QStringList parts;
            if (s2.listeners >= 0) parts << TR("%1 hallgató").arg(s2.listeners);
            if (s2.since) parts << duration(now - s2.since);
            parts << megabytes(s2.bytes);
            stats = parts.join(QStringLiteral(" · "));
        } else if (state == State::Retrying) {
            stats = TR("Újracsatlakozás…");
        } else if (state == State::Connecting) {
            stats = TR("Kapcsolódás…");
        } else if (state == State::Error) {
            stats = TR("Hiba – a részletek a napló alján");
        } else {
            stats = TR("Nincs elindítva");
        }
        c.stats->setText(stats);
        c.button->setText(active ? TR("Leállítás") : (state == State::Error ? TR("Újrapróbálás") : TR("Indítás")));
        c.button->setProperty("accent", !active);
        c.button->style()->unpolish(c.button);
        c.button->style()->polish(c.button);
        if (sel) {
            static_cast<LevelBars *>(meter_)->push(st.peakL, st.peakR);
            reduction_->setText(st.reduction < 0.97f ? TR("korlátoz: %1 dB").arg(QLocale().toString(20.0 * std::log10(st.reduction), 'f', 1)) : QString());
            restartBar_->setVisible(e->needsRestart());
            if (!st.message.isEmpty() && (state == State::Error || state == State::Retrying)) log_->setToolTip(st.message);
        }
    }
    updateBandwidth();
}

void StreamPage::updateBandwidth()
{
    QVector<int> parts;
    for (Encoder *e : mgr_->encoders())
        if (e->active()) parts << e->config().kbps;
    const int total = mgr_->totalKbps(), limit = mgr_->uploadLimitKbps();
    const bool over = limit > 0 && total > limit;
    bwTotal_->setText(QStringLiteral("<span style=\"font-size: 24px; font-weight: 500; color: %1;\">%2</span>&nbsp; <span style=\"color: %3;\">%4</span>")
                          .arg(over ? theme::hex("#ffb3ac") : theme::text.name())
                          .arg(total)
                          .arg(theme::hex("#c9ced6"), TR("kbit/s összesen")));
    bwHour_->setText(TR("≈ %1 MB/óra").arg(QLocale().toString(total * 125.0 * 3600 / 1e6, 'f', 0)));
    static_cast<BandBar *>(bwBar_)->set(parts, limit);
    bwBar_->setVisible(limit > 0 || total > 0);
    bwPercent_->setText(limit > 0 ? TR("%1% foglalt").arg(int(std::lround(100.0 * total / limit))) : QString());
    bwWarn_->setVisible(over);
    if (over)
        bwWarn_->setText(TR("Az enkóderek együtt többet küldenének, mint amennyit a feltöltés bír – a stream akadozhat. "
                            "Csökkentsd a bitrátát, vagy állíts le egy enkódert."));
    bwFrame_->setStyleSheet(over ? QStringLiteral("QFrame#panel { border-color: #8a3a35; }") : QString());
}

void StreamPage::selectEncoder(const QString &id)
{
    if (id == selected_) return;
    if (applyTimer_->isActive()) {
        applyTimer_->stop();
        applyEditor();
    }
    selected_ = id;
    static_cast<LevelBars *>(meter_)->reset();
    loadEditor();
    updateCards();
}

void StreamPage::selectFirstProblem()
{
    for (Encoder *e : mgr_->encoders())
        if (e->state() == State::Error) return selectEncoder(e->config().id);
    for (Encoder *e : mgr_->encoders())
        if (e->state() == State::Retrying) return selectEncoder(e->config().id);
}

// ------------------------------------------------------------------------------------------ szerkesztő

void StreamPage::fillSources(const QString &current)
{
    const bool was = loading_;
    loading_ = true;
    source_->clear();
    source_->addItem(TR("Fő kimenet (KI) – amit a hallgató hall"), QString());
    source_->addItem(TR("Bemenet: az alapértelmezett bemenet"), QStringLiteral("in:"));
    if (engine_)
        for (const eter::DeviceInfo &d : engine_->captureDevices())
            source_->addItem(TR("Bemenet: %1").arg(QString::fromStdString(d.name)), QStringLiteral("in:") + QString::fromStdString(d.name));
    int idx = source_->findData(current);
    if (idx < 0) {
        // A mentett bemenet most nincs (kihúzták): marad a listában, hogy ne vesszen el a beállítás.
        source_->addItem(TR("Bemenet: %1 (nem található)").arg(current.mid(3)), current);
        idx = source_->count() - 1;
    }
    source_->setCurrentIndex(idx);
    loading_ = was;
}

void StreamPage::loadEditor()
{
    Encoder *e = selected();
    rightStack_->setCurrentIndex(e ? 1 : 0);
    if (!e) return;
    loading_ = true;
    const Config &c = e->config();
    name_->setText(c.name);
    autoStart_->setChecked(c.autoStart);
    serverGroup_->button(int(c.server))->setChecked(true);
    host_->setText(c.host);
    port_->setValue(c.port);
    mount_->setText(c.mount);
    sid_->setValue(c.sid);
    user_->setText(c.user);
    password_->setText(c.password);
    tls_->setChecked(c.tls);
    formatGroup_->button(c.format == Format::Vorbis ? 1 : 0)->setChecked(true);
    if (QAbstractButton *b = rateGroup_->button(c.kbps)) b->setChecked(true);
    rateHz_->setCurrentIndex(std::max(0, rateHz_->findData(c.sampleRate)));
    (c.mono ? mono_ : stereo_)->setChecked(true);
    fillSources(c.source);
    gain_->setValue(int(std::lround(c.gainDb * 2)));
    gainLabel_->setText(QStringLiteral("%1%2 dB").arg(c.gainDb > 0 ? QStringLiteral("+") : QString()).arg(QLocale().toString(c.gainDb, 'f', std::fmod(c.gainDb, 1.0) != 0 ? 1 : 0)));
    limiter_->setChecked(c.limiter);
    sendTitle_->setChecked(c.sendTitle);
    likeNp_->setChecked(c.titleLikeNowPlaying);
    titleFormat_->setText(c.titleFormat);
    station_->setText(c.stationName);
    desc_->setText(c.description);
    genre_->setText(c.genre);
    url_->setText(c.url);
    public_->setChecked(c.isPublic);
    reconnect_->setChecked(c.reconnect);
    retry_->setValue(c.retrySec);
    updateEditorState();
    // mode_ a formátum szerint töltődött; most a mentett értékre
    if (c.format == Format::Mp3) mode_->setCurrentIndex(c.abr ? 1 : 0);
    log_->setPlainText(e->events().join(QLatin1Char('\n')));
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    loading_ = false;
    updateInfo();
}

Config StreamPage::editorConfig() const
{
    Encoder *e = selected();
    Config c = e ? e->config() : Config();
    c.name = name_->text().trimmed();
    c.autoStart = autoStart_->isChecked();
    c.server = Server(std::max(0, serverGroup_->checkedId()));
    c.host = host_->text().trimmed();
    c.port = port_->value();
    c.mount = mount_->text().trimmed();
    c.sid = sid_->value();
    c.user = user_->text().trimmed();
    c.password = password_->text();
    c.tls = tls_->isChecked();
    c.format = formatGroup_->checkedId() == 1 ? Format::Vorbis : Format::Mp3;
    if (rateGroup_->checkedId() > 0) c.kbps = rateGroup_->checkedId();
    c.abr = c.format == Format::Mp3 && mode_->currentIndex() == 1;
    c.sampleRate = rateHz_->currentData().toInt();
    c.mono = mono_->isChecked();
    c.source = source_->currentData().toString();
    c.gainDb = gain_->value() / 2.0;
    c.limiter = limiter_->isChecked();
    c.sendTitle = sendTitle_->isChecked();
    c.titleLikeNowPlaying = likeNp_->isChecked();
    c.titleFormat = titleFormat_->text();
    c.stationName = station_->text().trimmed();
    c.description = desc_->text().trimmed();
    c.genre = genre_->text().trimmed();
    c.url = url_->text().trimmed();
    c.isPublic = public_->isChecked();
    c.reconnect = reconnect_->isChecked();
    c.retrySec = retry_->value();
    return c;
}

void StreamPage::scheduleApply()
{
    if (loading_) return;
    updateInfo();
    applyTimer_->start();
}

void StreamPage::applyEditor()
{
    applyTimer_->stop();
    if (!selected()) return;
    const Config c = editorConfig();
    mgr_->update(c);
    updateCards();
}

void StreamPage::updateEditorState()
{
    const bool was = loading_;
    loading_ = true;
    const Server s = Server(std::max(0, serverGroup_->checkedId()));
    const bool ice = s == Server::Icecast;
    mountLabel_->setVisible(ice);
    mount_->setVisible(ice);
    sidLabel_->setVisible(s == Server::Shoutcast2);
    sid_->setVisible(s == Server::Shoutcast2);
    userLabel_->setVisible(ice);
    user_->setVisible(ice);
    tls_->setVisible(ice);
    serverNote_->setText(ice ? TR("A felhasználó általában „source”, a jelszó az Icecast source-password (vagy a mount saját jelszava).")
                             : s == Server::Shoutcast1 ? TR("Shoutcast v1: csak jelszó kell; a forrás a megadott port + 1 porton csatlakozik.")
                                                       : TR("Shoutcast v2: a stream azonosítója (SID) a szerver beállításaiból; a forrás a "
                                                            "port + 1 porton csatlakozik (régi, v1-kompatibilis módban)."));
    // Ogg Vorbis csak Icecastra.
    formatGroup_->button(1)->setEnabled(ice);
    formatGroup_->button(1)->setToolTip(ice ? QString() : TR("A Shoutcast nem fogad Ogg Vorbist; ehhez Icecast szerver kell."));
    if (!ice && formatGroup_->checkedId() == 1) formatGroup_->button(0)->setChecked(true);
    const bool vorbis = formatGroup_->checkedId() == 1;
    const int prevMode = mode_->currentIndex();
    mode_->clear();
    if (vorbis) {
        mode_->addItem(TR("Névleges bitráta (a Vorbis a bitráta körül változik)"));
        mode_->setEnabled(false);
    } else {
        mode_->addItem(TR("Állandó (CBR) – ajánlott streamhez"));
        mode_->addItem(TR("Átlagos (ABR)"));
        mode_->setEnabled(true);
        mode_->setCurrentIndex(std::max(0, std::min(prevMode, 1)));
    }
    // Vorbis: 32 kbit/s-mal sztereóban nem kódol.
    if (QAbstractButton *b = rateGroup_->button(32)) {
        b->setEnabled(!vorbis);
        if (vorbis && b->isChecked()) rateGroup_->button(48)->setChecked(true);
    }
    codecNote_->setText(vorbis ? TR("Ogg Vorbis: névleges bitráta 48–320 kbit/s; szabadalommentes, a legtöbb lejátszó és böngésző ismeri. "
                                    "A cím váltásakor a folyamba új fejléc kerül.")
                               : TR("MP3 (LAME): 32–320 kbit/s; minden lejátszó ismeri. 64 kbit/s alatt a mono szól jobban."));
    titleFormat_->setEnabled(sendTitle_->isChecked() && !likeNp_->isChecked());
    likeNp_->setEnabled(sendTitle_->isChecked());
    if (likeNp_->isChecked()) titleFormat_->setText(NowPlaying::loadConfig().format);
    titleNote_->setText(vorbis ? TR("Ogg streamnél a cím a hangfolyamba kerül; MP3-nál a szerver admin felületén frissül (ugyanazzal a jelszóval).")
                               : TR("A címet a szerver admin felületén frissíti (Icecast: admin/metadata, Shoutcast: admin.cgi), ugyanazzal a jelszóval."));
    loading_ = was;
    updateInfo();
}

void StreamPage::updateInfo()
{
    const int kbps = rateGroup_->checkedId() > 0 ? rateGroup_->checkedId() : 128;
    const QString b = theme::text.name();
    const double mbHour = kbps * 125.0 * 3600 / 1e6;
    info_->setText(TR("Kifelé: <b style=\"color:%1\">%2 kbit/s</b> &nbsp;·&nbsp; ≈ <b style=\"color:%1\">%3 MB</b> óránként &nbsp;·&nbsp; ≈ <b style=\"color:%1\">%4 GB</b> naponta")
                       .arg(b)
                       .arg(kbps)
                       .arg(QLocale().toString(mbHour, 'f', 1))
                       .arg(QLocale().toString(mbHour * 24 / 1000, 'f', 1)));
}

void StreamPage::appendEvent(const QString &encoderId, const QString &line)
{
    if (encoderId != selected_) return;
    log_->appendPlainText(line);
}
