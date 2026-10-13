#include "RemoteEditor.h"
#include "i18n.h"
#include "MixEditorPage.h"
#include "PlaylistTable.h"
#include "Theme.h"
#include "audio/Engine.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

// ----------------------------------------------------------- csatlakozás

RemoteConnectDialog::RemoteConnectDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(TR("Csatlakozás a playouthoz"));
    setMinimumWidth(520);
    QSettings s;
    auto *v = new QVBoxLayout(this);
    auto *intro = new QLabel(TR("Add meg a playout gép címét és a távoli szerkesztés jelszavát (a playout Beállításaiban "
                                "kapcsolható be). A hangfájlokat a szerkesztő a közös hálózati mappából olvassa."));
    intro->setWordWrap(true);
    intro->setObjectName("small");
    v->addWidget(intro);
    auto *f = new QFormLayout;
    host_ = new QLineEdit(s.value("remoteEditor/host").toString());
    host_->setPlaceholderText(TR("például 192.168.1.20"));
    port_ = new QSpinBox;
    port_->setRange(1, 65535);
    port_->setValue(s.value("remoteEditor/port", remote::kDefaultPort).toInt());
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    from_ = new QLineEdit(s.value("remoteEditor/pathFrom").toString());
    from_->setPlaceholderText(TR("például D:\\Zene vagy /srv/zene"));
    to_ = new QLineEdit(s.value("remoteEditor/pathTo").toString());
    to_->setPlaceholderText(TR("például smb://radio/Zene, \\\\radio\\Zene vagy /mnt/zene"));
    f->addRow(TR("Playout címe (IP)"), host_);
    f->addRow(TR("Port"), port_);
    f->addRow(TR("Jelszó"), password_);
    f->addRow(TR("Hangfájlok a playout gépen"), from_);
    f->addRow(TR("Ugyanez ezen a gépen"), to_);
    v->addLayout(f);
    auto *note = new QLabel(TR("Ha a két gépen ugyanaz az útvonal, a két mappamező üresen hagyható. Linuxon smb:// cím is "
                               "megadható, ha a megosztást a fájlkezelőben már megnyitottad; vagy egy felcsatolt mappa."));
    note->setObjectName("small");
    note->setWordWrap(true);
    v->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(TR("Csatlakozás"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(TR("Mégse"));
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (host_->text().trimmed().isEmpty()) {
            host_->setFocus();
            return;
        }
        QSettings st;
        st.setValue("remoteEditor/host", host_->text().trimmed());
        st.setValue("remoteEditor/port", port_->value());
        st.setValue("remoteEditor/pathFrom", from_->text().trimmed());
        st.setValue("remoteEditor/pathTo", to_->text().trimmed());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    (host_->text().isEmpty() ? host_ : password_)->setFocus();
}

RemoteTarget RemoteConnectDialog::target() const
{
    RemoteTarget t;
    t.host = host_->text().trimmed();
    t.port = quint16(port_->value());
    t.password = password_->text();
    t.pathFrom = from_->text().trimmed();
    t.pathTo = to_->text().trimmed();
    return t;
}

// --------------------------------------------------------------- ablak

static const char *kStateText(const QString &s)
{
    if (s == QLatin1String("playing")) return "ADÁSBAN";
    if (s == QLatin1String("cued")) return "KÉSZENLÉT";
    if (s == QLatin1String("played")) return "lejátszva";
    if (s == QLatin1String("skipped")) return "kihagyva";
    if (s == QLatin1String("failed")) return "hibás fájl";
    return "";
}

RemoteEditorWindow::RemoteEditorWindow(Database *mirror, eter::Engine *engine, QWidget *parent)
    : QMainWindow(parent), db_(mirror), engine_(engine), client_(new remote::Client(this))
{
    setWindowTitle(TR("Éter – távoli szerkesztő"));
    setWindowIcon(theme::appIcon());
    resize(1440, 900);
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // Felső sáv: a kapcsolat állapota
    auto *bar = new QFrame;
    bar->setObjectName("panel");
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(12, 8, 12, 8);
    banner_ = new QLabel;
    banner_->setTextFormat(Qt::PlainText);
    nowLabel_ = new QLabel;
    nowLabel_->setObjectName("small");
    reconnect_ = new QPushButton(TR("Csatlakozás…"));
    bl->addWidget(banner_, 1);
    bl->addWidget(nowLabel_);
    bl->addWidget(reconnect_);
    root->addWidget(bar);
    warning_ = new QLabel;
    warning_->setWordWrap(true);
    warning_->setTextFormat(Qt::PlainText);
    warning_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    warning_->setStyleSheet(QStringLiteral("background: #3a2c10; color: #ffd58a; border: 1px solid #f5a524; border-radius: 4px; padding: 8px 12px;"));
    warning_->hide();
    root->addWidget(warning_);

    // Bal: az élő playlist; jobb: a Mix editor
    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);
    split->setChildrenCollapsible(false);
    auto *left = new QFrame;
    left->setObjectName("panel");
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(1, 1, 1, 1);
    lv->setSpacing(0);
    auto *lhead = new QFrame;
    lhead->setObjectName("panelHeader");
    auto *lh = new QHBoxLayout(lhead);
    lh->setContentsMargins(12, 8, 12, 8);
    auto *ltitle = new QLabel(TR("Playlist a playouton"));
    ltitle->setObjectName("panelTitle");
    auto *lhint = new QLabel(TR("húzással átrendezhető · jobb gomb: műveletek"));
    lhint->setObjectName("small");
    lh->addWidget(ltitle);
    lh->addStretch(1);
    lh->addWidget(lhint);
    lv->addWidget(lhead);
    table_ = new PlaylistTable;
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({TR("Kezdés"), TR("Cím"), TR("Hossz"), TR("Állapot")});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(30);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setWordWrap(false);
    auto *hh = table_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setColumnWidth(0, 72);
    table_->setColumnWidth(2, 50);
    table_->setColumnWidth(3, 150);
    lv->addWidget(table_, 1);
    split->addWidget(left);

    mix_ = new MixEditorPage(db_, engine_);
    mix_->setRemoteMode(true);
    split->addWidget(mix_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({470, 970});
    root->addWidget(split, 1);
    setCentralWidget(central);
    status_ = new QLabel;
    statusBar()->addWidget(status_, 1);

    retry_ = new QTimer(this);
    retry_->setInterval(3000);
    connect(retry_, &QTimer::timeout, this, [this] { client_->connectTo(target_.host, target_.port, target_.password); });

    connect(reconnect_, &QPushButton::clicked, this, &RemoteEditorWindow::openConnectDialog);
    connect(client_, &remote::Client::loggedIn, this, [this](const QString &version) {
        // Belépéskor a playout elküldi a hangtárat és az állapotot; a régi (akár másik playouttól való) tükör érvénytelen.
        lastState_ = QJsonObject();
        haveLibrary_ = false;
        pendingOps_.clear();
        everConnected_ = true;
        retry_->stop();
        setOnline(true, TR("Kapcsolódva: %1:%2 · playout %3").arg(target_.host).arg(target_.port).arg(version));
    });
    connect(client_, &remote::Client::failed, this, [this](const QString &reason) {
        if (everConnected_ && !retry_->isActive()) retry_->start();
        setOnline(false, everConnected_ ? TR("Újracsatlakozás… (%1)").arg(reason) : reason);
        if (!everConnected_) reconnect_->setText(TR("Csatlakozás…"));
    });
    connect(client_, &remote::Client::lost, this, [this](const QString &reason) {
        setOnline(false, TR("A kapcsolat megszakadt (%1) – újracsatlakozás…").arg(reason));
        retry_->start();
    });
    connect(client_, &remote::Client::message, this, &RemoteEditorWindow::onMessage);

    // A Mix editor mentései a tükörbe íródnak, és innen a playouthoz mennek.
    db_->remoteHook = [this](const QString &op, const QJsonObject &args) { send(op, args); };
    connect(mix_, &MixEditorPage::overlayEdited, this, [this](int entryId, bool on, qint64 offsetMs, double duckDb) {
        send(QStringLiteral("overlay"), QJsonObject{{"entry", entryId}, {"on", on}, {"offset", offsetMs}, {"duck", duckDb}});
    });
    connect(mix_, &MixEditorPage::mixSaved, this, [this](const QVector<int> &) { rebuildPairs(); });
    connect(mix_, &MixEditorPage::message, this, [this](const QString &t) { statusBar()->showMessage(t, 8000); });

    connect(table_, &PlaylistTable::rowMoved, this, [this](int from, int to) {
        if (from < 0 || from >= rows_.size()) return;
        requestMove(rows_[from].entry.id, to < rows_.size() ? rows_[to].entry.id : 0);
    });
    connect(table_, &PlaylistTable::deletePressed, this, [this] {
        const int r = table_->currentRow();
        if (r >= 0 && r < rows_.size()) requestRemove(rows_[r].entry.id);
    });
    connect(table_, &QWidget::customContextMenuRequested, this, &RemoteEditorWindow::contextMenu);
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int) {
        // A sor és az utána következő elem átmenete a Mix editorban.
        if (r < 0 || r >= rows_.size() || !live(r)) return;
        for (int j = r + 1; j < rows_.size(); ++j)
            if (live(j) && !overlayActive(j)) {
                mix_->selectEntries(rows_[r].entry.id, rows_[j].entry.id);
                return;
            }
    });
    setOnline(false, TR("Nincs kapcsolat."));
}

void RemoteEditorWindow::closeEvent(QCloseEvent *e)
{
    mix_->stopPreview();
    db_->remoteHook = nullptr;
    client_->disconnectFromHost();
    QMainWindow::closeEvent(e);
}

void RemoteEditorWindow::openConnectDialog()
{
    RemoteConnectDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) connectTo(dlg.target());
}

void RemoteEditorWindow::connectTo(const RemoteTarget &t)
{
    target_ = t;
    pathProblem_.clear();
    localRoot_ = localFolder(t.pathTo, &pathProblem_);
    everConnected_ = false;
    haveLibrary_ = false;
    retry_->stop();
    setOnline(false, TR("Csatlakozás: %1:%2…").arg(t.host).arg(t.port));
    client_->connectTo(t.host, t.port, t.password);
}

void RemoteEditorWindow::setOnline(bool on, const QString &text)
{
    banner_->setText(text);
    banner_->setStyleSheet(on ? QStringLiteral("color: #45d483; font-weight: 600;") : QStringLiteral("color: #ffb4ae; font-weight: 600;"));
    // Kapcsolat nélkül nem lehet szerkeszteni: a módosítás nem jutna el a playouthoz.
    table_->setDragEnabled(on);
    mix_->setEnabled(on);
    reconnect_->setText(on ? TR("Másik playout…") : TR("Csatlakozás…"));
    if (!on) mix_->stopPreview();
}

int RemoteEditorWindow::send(const QString &op, const QJsonObject &args)
{
    const int id = client_->sendOp(op, args);
    if (!id) {
        statusBar()->showMessage(TR("Nincs kapcsolat a playouttal – a módosítás nem ment át."), 8000);
        return 0;
    }
    pendingOps_.insert(id, op);
    status_->setText(TR("Küldés a playoutnak…"));
    return id;
}

// A szerkesztő gépen megadott mappa valódi helye. A fájlkezelők smb://gép/megosztás/… címe (és a
// \\gép\megosztás\… alak) nem fájlútvonal: Windowson UNC-útvonallá, Linuxon a GNOME (gvfs) felcsatolási
// helyévé alakítjuk – ez akkor létezik, ha a megosztást a fájlkezelőben megnyitották.
QString RemoteEditorWindow::localFolder(const QString &input, QString *problem)
{
    QString in = input.trimmed();
    if (in.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) in = QUrl(in).toLocalFile();
    QString host, share, rest;
    QString t = in;
    if (t.startsWith(QLatin1String("smb://"), Qt::CaseInsensitive)) t = t.mid(6);
    else if (t.startsWith(QLatin1String("\\\\")) || t.startsWith(QLatin1String("//"))) t = t.mid(2);
    else return in;
    t.replace('\\', '/');
    const QStringList parts = t.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        if (problem) *problem = TR("A hálózati címben a gép neve után a megosztás nevét is meg kell adni (smb://gép/megosztás/mappa).");
        return in;
    }
    host = parts[0];
    share = QUrl::fromPercentEncoding(parts[1].toUtf8());
    rest = QUrl::fromPercentEncoding(parts.mid(2).join('/').toUtf8());
    if (host.contains('@')) host = host.section('@', -1);            // smb://felhasználó@gép/…
#ifdef Q_OS_WIN
    Q_UNUSED(problem);
    return QStringLiteral("//%1/%2/%3").arg(host, share, rest);
#else
    const QString runtime = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR") ? qEnvironmentVariable("XDG_RUNTIME_DIR")
                                                                         : QStringLiteral("/run/user/%1").arg(::getuid());
    QString root;
    for (const QString &h : {host, host.toLower()})
        for (const QString &sh : {share, share.toLower()}) {
            const QString cand = QStringLiteral("%1/gvfs/smb-share:server=%2,share=%3").arg(runtime, h, sh);
            if (root.isEmpty() && QFileInfo::exists(cand)) root = cand;
        }
    if (root.isEmpty()) {
        root = QStringLiteral("%1/gvfs/smb-share:server=%2,share=%3").arg(runtime, host.toLower(), share.toLower());
        if (problem)
            *problem = TR("A(z) smb://%1/%2 megosztás nincs felcsatolva ezen a gépen. Nyisd meg egyszer a fájlkezelőben "
                          "(a jelszót megjegyeztetve), vagy csatold fel egy mappába, és azt add meg.").arg(host, share);
    }
    return rest.isEmpty() ? root : root + '/' + rest;
#endif
}

QString RemoteEditorWindow::mapPath(const QString &path) const { return mapWith(path, target_.pathFrom, localRoot_); }

QString RemoteEditorWindow::mapWith(const QString &path, const QString &pathFrom, const QString &localRoot)
{
    if (pathFrom.isEmpty()) return path;
    auto norm = [](QString p) {
        p.replace('\\', '/');
        while (p.size() > 1 && p.endsWith('/')) p.chop(1);
        return p;
    };
    const QString from = norm(pathFrom), to = norm(localRoot);
    const QString p = norm(path);
    const Qt::CaseSensitivity cs = (from.size() > 1 && from[1] == ':') ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (!p.startsWith(from, cs) || (p.size() > from.size() && p[from.size()] != '/')) return path;
    return QDir::cleanPath(to + p.mid(from.size()));
}

void RemoteEditorWindow::onMessage(const QJsonObject &m)
{
    const QString t = m.value("t").toString();
    if (t == QLatin1String("library")) applyLibrary(m);
    else if (t == QLatin1String("state")) applyState(m);
    else if (t == QLatin1String("ack")) {
        const int id = m.value("id").toInt();
        const QString op = pendingOps_.take(id);
        const bool ok = m.value("ok").toBool();
        const QString err = m.value("error").toString();
        status_->setText(ok ? TR("A playout átvette a módosítást.") : TR("A playout elutasította: %1").arg(err));
        if (!ok) statusBar()->showMessage(TR("A playout elutasította: %1").arg(err), 10000);
        emit opAnswered(id, ok, err);
    }
}

void RemoteEditorWindow::applyLibrary(const QJsonObject &m)
{
    QVector<Category> cats;
    for (const QJsonValue &v : m.value("categories").toArray()) cats.push_back(remote::categoryFromJson(v.toObject()));
    QVector<Item> items;
    items.reserve(m.value("items").toArray().size());
    for (const QJsonValue &v : m.value("items").toArray()) {
        Item it = remote::itemFromJson(v.toObject());
        it.path = mapPath(it.path);
        items.push_back(it);
    }
    const QJsonObject settings = m.value("settings").toObject();
    for (auto it = settings.begin(); it != settings.end(); ++it) db_->setSetting(it.key(), it.value().toString());
    station_ = m.value("station").toString();
    setWindowTitle(TR("Éter – távoli szerkesztő · %1").arg(station_.isEmpty() ? target_.host : station_));
    const auto hook = std::move(db_->remoteHook);
    db_->remoteHook = nullptr;
    haveLibrary_ = db_->mirrorLibrary(cats, items);
    db_->remoteHook = hook;
    // Elérhetők-e a hangfájlok ezen a gépen? (Néhány elem próbája; ha egy sincs meg, szólunk.)
    int tried = 0, found = 0;
    QString example;
    for (int i = 0; i < items.size() && tried < 40; i += std::max<int>(1, items.size() / 40)) {
        ++tried;
        if (QFileInfo::exists(items[i].path)) ++found;
        else if (example.isEmpty()) example = items[i].path;
    }
    if (tried > 0 && found == 0) {
        QString text = TR("A hangfájlok nem érhetők el ezen a gépen, ezért nincs hullámforma és előhallgatás (például: %1).").arg(example);
        if (!pathProblem_.isEmpty()) text += QLatin1Char(' ') + pathProblem_;
        else if (target_.pathFrom.isEmpty()) text += QLatin1Char(' ') + TR("Add meg a csatlakozásnál, hol van a hangfájlok mappája a playout gépen és ezen a gépen.");
        else text += QLatin1Char(' ') + TR("Ellenőrizd a csatlakozásnál megadott két mappát.");
        warning_->setText(text);
        warning_->show();
    } else {
        warning_->hide();
    }
    if (!lastState_.isEmpty()) applyState(lastState_);
}

void RemoteEditorWindow::applyState(const QJsonObject &m)
{
    lastState_ = m;
    if (!haveLibrary_) return;
    QVector<Row> rows;
    QVector<Database::MirrorRow> mirror;
    for (const QJsonValue &v : m.value("rows").toArray()) {
        const QJsonObject o = v.toObject();
        Row r;
        r.entry.id = o.value("id").toInt();
        r.entry.playlistId = o.value("pl").toInt();
        r.entry.playlistKey = o.value("key").toInt();
        r.entry.source = o.value("src").toString();
        r.entry.item = db_->item(o.value("item").toInt());
        if (!r.entry.item.id) continue;                  // a hangtár még nem frissült: a következő állapot hozza
        if (o.contains("fixed")) {
            const QDateTime at = QDateTime::fromString(o.value("fixed").toString(), Qt::ISODate);
            if (at.isValid()) r.entry.fixedStartS = at.time().minute() * 60 + at.time().second();
        }
        r.entry.groupId = o.value("group").toInt();
        r.entry.groupName = o.value("groupName").toString();
        if (o.contains("ovl")) {
            const QJsonObject ov = o.value("ovl").toObject();
            r.entry.overlay = true;
            r.entry.overlayOffsetMs = ov.value("offset").toInteger();
            r.entry.overlayDuckDb = ov.value("duck").toDouble();
            r.voice = ov.value("voice").toInt(-1);
        }
        r.state = o.value("state").toString();
        r.player = o.value("player").toInt(-1);
        r.start = o.value("start").toString();
        rows.push_back(r);
        const QString status = r.state == QLatin1String("cued") ? QStringLiteral("pending")
                               : r.state == QLatin1String("failed") ? QStringLiteral("skipped") : r.state;
        mirror.push_back({r.entry, status});
    }
    QVector<Transition> trs;
    for (const QJsonValue &v : m.value("transitions").toArray()) trs.push_back(remote::transitionFromJson(v.toObject()));
    db_->mirrorState(mirror, trs);
    rows_ = rows;
    const QDateTime now = QDateTime::fromString(m.value("now").toString(), Qt::ISODate);
    nowLabel_->setText(TR("playout ideje: %1 · %2").arg(now.toString("HH:mm:ss"),
                                                        m.value("mode").toString() == QLatin1String("auto") ? TR("automata") : TR("asszisztált")));
    rebuildTable();
    rebuildPairs();
    emit stateApplied();
}

bool RemoteEditorWindow::live(int i) const
{
    const QString &s = rows_[i].state;
    return s == QLatin1String("pending") || s == QLatin1String("cued") || s == QLatin1String("playing");
}

// Ugyanaz a szabály, mint a playouton: ráfektetett elem, ha előtte és utána is van (nem ráfektetett) elem.
bool RemoteEditorWindow::overlayActive(int i) const
{
    const Row &r = rows_[i];
    if (!r.entry.overlay) return false;
    if (r.voice >= 0) return true;
    if (r.state != QLatin1String("pending") && r.state != QLatin1String("cued")) return false;
    bool before = false;
    for (int j = i - 1; j >= 0 && !before; --j)
        if (!rows_[j].entry.overlay) before = true;
    for (int j = i + 1; j < rows_.size(); ++j) {
        if (rows_[j].entry.overlay) continue;
        return before && live(j);
    }
    return false;
}

void RemoteEditorWindow::rebuildTable()
{
    const int keepId = table_->currentRow() >= 0 && table_->currentRow() < table_->rowCount()
                           ? table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toInt() : -1;
    const QHash<int, int> custom = db_->transitionPairs();
    QSet<int> customFrom;
    for (auto it = custom.begin(); it != custom.end(); ++it) customFrom.insert(it.value());
    table_->setRowCount(rows_.size());
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);
    int select = -1;
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &r = rows_[i];
        const bool ovl = overlayActive(i);
        QString status = TR(kStateText(r.state));
        if (r.state == QLatin1String("playing") || r.state == QLatin1String("cued")) {
            if (r.player >= 0) status += QStringLiteral(" · %1").arg(r.player + 1);
        }
        if (ovl) status = r.state == QLatin1String("playing") ? TR("RÁFEKTETVE · SZÓL") : TR("RÁFEKTETVE");
        QStringList chips;
        if (r.entry.fixedStartS >= 0) chips << QStringLiteral("FIX");
        if (customFrom.contains(r.entry.id)) chips << QStringLiteral("MIX");
        if (custom.contains(r.entry.id) && db_->transition(r.entry.id).vtItemId) chips << QStringLiteral("VT");
        const QString title = (ovl ? QStringLiteral("↳ ") : QString())
                              + (r.entry.item.artist.isEmpty() ? r.entry.item.title : r.entry.item.title + QStringLiteral(" – ") + r.entry.item.artist);
        if (!chips.isEmpty()) status += (status.isEmpty() ? QString() : QStringLiteral("  ·  ")) + chips.join(' ');
        const QString cells[4] = {r.start, title, theme::formatTime(r.entry.item.durationMs * eter::kRate / 1000), status};
        QColor bg = Qt::transparent, fg = theme::text;
        if (r.state == QLatin1String("playing")) { bg = QColor("#3a2c10"); fg = theme::amberSoft; }
        else if (r.state == QLatin1String("cued")) { bg = QColor("#14283d"); fg = theme::blueSoft; }
        else if (!live(i)) fg = QColor("#8a93a0");
        if (ovl) { bg = QColor(r.state == QLatin1String("playing") ? "#3d3160" : "#272338"); fg = QColor("#ddd2ff"); }
        const bool movable = r.state == QLatin1String("pending") || r.state == QLatin1String("cued");
        for (int c = 0; c < 4; ++c) {
            auto *item = table_->item(i, c);
            if (!item) {
                item = new QTableWidgetItem;
                table_->setItem(i, c, item);
            }
            item->setText(cells[c]);
            item->setBackground(bg);
            item->setForeground(fg);
            item->setFont(c == 0 || c == 2 ? mono : table_->font());
            item->setToolTip(c == 1 ? r.entry.item.path : QString());
            item->setData(Qt::UserRole, r.entry.id);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | (movable ? Qt::ItemIsDragEnabled : Qt::NoItemFlags));
        }
        if (r.entry.id == keepId) select = i;
    }
    if (select >= 0) table_->setCurrentCell(select, 0);
    table_->viewport()->update();
}

// A Mix editor párjai: ugyanúgy, mint a playouton (a ráfektetett elem a két szomszédja átmenetéhez tartozik).
void RemoteEditorWindow::rebuildPairs()
{
    QVector<MixPair> pairs;
    int prev = -1, ovl = -1;
    for (int i = 0; i < rows_.size() && pairs.size() < 60; ++i) {
        if (!live(i)) continue;
        if (overlayActive(i)) {
            if (rows_[i].state == QLatin1String("pending") && ovl < 0) ovl = i;
            continue;
        }
        if (prev >= 0) {
            MixPair p;
            p.fromEntry = rows_[prev].entry.id;
            p.toEntry = rows_[i].entry.id;
            p.from = db_->item(rows_[prev].entry.item.id);
            p.to = db_->item(rows_[i].entry.item.id);
            p.fromOnAir = rows_[prev].state == QLatin1String("playing");
            p.toOnAir = rows_[i].state == QLatin1String("playing");
            p.startText = rows_[i].start;
            if (ovl >= 0) {
                p.ovlEntry = rows_[ovl].entry.id;
                p.ovlItem = db_->item(rows_[ovl].entry.item.id);
                p.ovlOffsetMs = rows_[ovl].entry.overlayOffsetMs;
                p.ovlDuckDb = rows_[ovl].entry.overlayDuckDb;
            }
            pairs.push_back(p);
        }
        prev = i;
        ovl = -1;
    }
    mix_->setPairs(pairs);
}

void RemoteEditorWindow::requestMove(int entryId, int beforeEntryId)
{
    send(QStringLiteral("move"), QJsonObject{{"entry", entryId}, {"before", beforeEntryId}});
}

void RemoteEditorWindow::requestRemove(int entryId)
{
    send(QStringLiteral("remove"), QJsonObject{{"entry", entryId}});
}

void RemoteEditorWindow::requestOverlay(int entryId, bool on)
{
    send(QStringLiteral("overlay"), QJsonObject{{"entry", entryId}, {"on", on}, {"offset", 0}, {"duck", 0}});
}

void RemoteEditorWindow::contextMenu(const QPoint &pos)
{
    const int r = table_->rowAt(pos.y());
    if (r < 0 || r >= rows_.size() || !client_->ready()) return;
    table_->setCurrentCell(r, 0);
    const Row row = rows_[r];
    const bool waiting = row.state == QLatin1String("pending") || row.state == QLatin1String("cued");
    QMenu menu(this);
    if (waiting) {
        if (row.entry.overlay) {
            menu.addAction(TR("Ráfektetés megszüntetése (önálló elem)"), this, [this, id = row.entry.id] { requestOverlay(id, false); });
        } else {
            menu.addAction(TR("Ráfektetés az előtte és utána álló elem átmenetére"), this,
                           [this, id = row.entry.id] { requestOverlay(id, true); });
        }
        menu.addSeparator();
        menu.addAction(TR("Törlés a playlistből"), this, [this, id = row.entry.id, title = row.entry.item.title] {
            if (QMessageBox::question(this, TR("Törlés a playlistből"), TR("Törlöd a playlistből: „%1”?").arg(title),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                requestRemove(id);
        });
    } else {
        menu.addAction(TR("Ez a sor már szól vagy lejátszódott – nem módosítható."))->setEnabled(false);
    }
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

QString RemoteEditorWindow::debugState() const
{
    QStringList parts;
    for (const Row &r : rows_)
        parts << QStringLiteral("%1=%2%3").arg(r.entry.item.title, r.state, r.entry.overlay ? QStringLiteral("[ráf %1]").arg(r.entry.overlayOffsetMs) : QString());
    return parts.join(QStringLiteral("  "));
}

QString RemoteEditorWindow::warningText() const { return warning_ && !warning_->isHidden() ? warning_->text() : QString(); }
