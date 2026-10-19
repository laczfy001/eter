#include "RemoteEditor.h"
#include "i18n.h"
#include "MixEditorPage.h"
#include "PlaylistTable.h"
#include "Theme.h"
#include <QFileDialog>
#include <QFile>
#include <QComboBox>
#include "audio/Decoder.h"
#include "Library.h"
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
#include <QApplication>
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
    user_ = new QLineEdit(s.value("remoteEditor/user").toString());
    user_->setPlaceholderText(TR("üresen: az alap jelszó (teljes jog)"));
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    from_ = new QLineEdit(s.value("remoteEditor/pathFrom").toString());
    from_->setPlaceholderText(TR("például D:\\Zene vagy /srv/zene"));
    to_ = new QLineEdit(s.value("remoteEditor/pathTo").toString());
    to_->setPlaceholderText(TR("például smb://radio/Zene, \\\\radio\\Zene vagy /mnt/zene"));
    f->addRow(TR("Playout címe (IP)"), host_);
    f->addRow(TR("Port"), port_);
    f->addRow(TR("Felhasználó"), user_);
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
            QMessageBox::warning(this, TR("Csatlakozás a playouthoz"), TR("Add meg a playout gép címét (IP-címét)."));
            host_->setFocus();
            return;
        }
        if (password_->text().isEmpty()) {
            // Jelszó nélkül meg sem próbál csatlakozni: a playout úgyis elutasítaná.
            QMessageBox::warning(this, TR("Csatlakozás a playouthoz"),
                                 TR("Add meg a távoli szerkesztés jelszavát. A jelszót a playout gépen, a Beállítások → "
                                    "Távoli szerkesztés részben lehet megadni."));
            password_->setFocus();
            return;
        }
        QSettings st;
        st.setValue("remoteEditor/host", host_->text().trimmed());
        st.setValue("remoteEditor/port", port_->value());
        st.setValue("remoteEditor/user", user_->text().trimmed());
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
    t.user = user_->text().trimmed();
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
    // A playout hangtára: innen húzható elem a playlistbe; ide (vagy a playlistbe) ejtett fájl feltöltésre kerül.
    library_ = new LibraryPanel(db_);
    library_->setRemoteMode(true);
    auto *libFoot = new QFrame;
    libFoot->setObjectName("panelFooter");
    auto *lf = new QHBoxLayout(libFoot);
    lf->setContentsMargins(10, 6, 10, 6);
    auto *uploadBtn = new QPushButton(TR("Feltöltés a playoutra…"));
    uploadBtn->setObjectName("uploadButton");
    uploadBtn->setToolTip(TR("Hangfájlok feltöltése erről a gépről a playout hangtárába"));
    auto *libHint = new QLabel(TR("dupla kattintás vagy húzás: a playlistbe"));
    libHint->setObjectName("small");
    lf->addWidget(uploadBtn);
    lf->addStretch(1);
    lf->addWidget(libHint);
    library_->setFooter(libFoot);
    auto *leftSplit = new QSplitter(Qt::Vertical);
    leftSplit->setHandleWidth(8);
    leftSplit->setChildrenCollapsible(false);
    leftSplit->addWidget(left);
    leftSplit->addWidget(library_);
    leftSplit->setStretchFactor(0, 3);
    leftSplit->setStretchFactor(1, 2);
    split->addWidget(leftSplit);
    connect(uploadBtn, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, TR("Feltöltés a playoutra"), QSettings().value("ui/lastDir").toString(),
                                                                TR("Hangfájlok (*.%1);;Minden fájl (*)").arg(audioExtensions().join(" *.")));
        if (files.isEmpty()) return;
        QSettings().setValue("ui/lastDir", QFileInfo(files.first()).absolutePath());
        uploadPaths(files, -1);
    });
    connect(library_, &LibraryPanel::filesDropped, this, [this](const QStringList &paths) { uploadPaths(paths, -1); });
    connect(library_, &LibraryPanel::addToPlaylist, this, [this](const QVector<int> &ids) { requestInsert(ids, 0); });
    connect(library_, &LibraryPanel::previewRequested, this, &RemoteEditorWindow::previewLibraryItem);
    connect(library_, &LibraryPanel::message, this, [this](const QString &t) { statusBar()->showMessage(t, 8000); });
    connect(table_, &PlaylistTable::itemsDropped, this, [this](const QVector<int> &ids, int at) {
        requestInsert(ids, at >= 0 && at < rows_.size() ? rows_[at].entry.id : 0);
    });
    connect(table_, &PlaylistTable::pathsDropped, this, [this](const QStringList &paths, int at) {
        uploadPaths(paths, at >= 0 && at < rows_.size() ? rows_[at].entry.id : 0);
    });

    mix_ = new MixEditorPage(db_, engine_);
    mix_->setRemoteMode(true);
    connect(mix_, &MixEditorPage::previewStarting, this, [this] {
        if (libPreview_) {
            engine_->source(eter::Engine::Pfl).stop();
            libPreview_ = false;
        }
    });
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
    connect(retry_, &QTimer::timeout, this, [this] { client_->connectTo(target_.host, target_.port, target_.user, target_.password); });

    connect(reconnect_, &QPushButton::clicked, this, &RemoteEditorWindow::openConnectDialog);
    connect(client_, &remote::Client::loggedIn, this, [this](const QString &version) {
        // Belépéskor a playout elküldi a hangtárat és az állapotot; a régi (akár másik playouttól való) tükör érvénytelen.
        lastState_ = QJsonObject();
        haveLibrary_ = false;
        pendingOps_.clear();
        everConnected_ = true;
        retry_->stop();
        QString who = target_.user.isEmpty() ? QString() : TR(" · felhasználó: %1").arg(target_.user);
        setOnline(true, TR("Kapcsolódva: %1:%2 · playout %3").arg(target_.host).arg(target_.port).arg(version) + who);
        applyPerms();
    });
    connect(client_, &remote::Client::failed, this, [this](const QString &reason) {
        if (everConnected_ && !retry_->isActive()) retry_->start();
        setOnline(false, everConnected_ ? TR("Újracsatlakozás… (%1)").arg(reason) : reason);
        if (!everConnected_) reconnect_->setText(TR("Csatlakozás…"));
    });
    connect(client_, &remote::Client::lost, this, [this](const QString &reason) {
        setOnline(false, TR("A kapcsolat megszakadt (%1) – újracsatlakozás…").arg(reason));
        retry_->start();
        // A félbemaradt feltöltések elvesznek (a playout törli a darabjaikat).
        for (UploadJob &j : uploads_)
            if (j.done) j.done(0);
        if (!uploads_.isEmpty()) statusBar()->showMessage(TR("A feltöltés megszakadt a kapcsolattal együtt."), 10000);
        uploads_.clear();
        callbacks_.clear();
        pendingOps_.clear();
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
        if (!client_->can(QStringLiteral("delete"))) return;
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
    retry_->stop();
    client_->disconnectFromHost();
    QMainWindow::closeEvent(e);
    // A szerkesztő ablak bezárása mindig kilépés, akkor is, ha valami (párbeszédablak, kapcsolat) még nyitva maradt.
    if (e->isAccepted()) QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void RemoteEditorWindow::openConnectDialog()
{
    RemoteConnectDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) connectTo(dlg.target());
}

void RemoteEditorWindow::connectTo(const RemoteTarget &t)
{
    if (t.password.isEmpty()) {
        setOnline(false, TR("Nincs megadva jelszó – nem csatlakozik."));
        return;
    }
    target_ = t;
    pathProblem_.clear();
    localRoot_ = localFolder(t.pathTo, &pathProblem_);
    everConnected_ = false;
    haveLibrary_ = false;
    retry_->stop();
    setOnline(false, TR("Csatlakozás: %1:%2…").arg(t.host).arg(t.port));
    client_->connectTo(t.host, t.port, t.user, t.password);
}

void RemoteEditorWindow::setOnline(bool on, const QString &text)
{
    banner_->setText(text);
    banner_->setStyleSheet(on ? QStringLiteral("color: #45d483; font-weight: 600;") : QStringLiteral("color: #ffb4ae; font-weight: 600;"));
    // Kapcsolat nélkül nem lehet szerkeszteni: a módosítás nem jutna el a playouthoz.
    table_->setDragEnabled(on && client_->can(QStringLiteral("order")));
    mix_->setEnabled(on && client_->can(QStringLiteral("mix")));
    library_->setEnabled(on);
    reconnect_->setText(on ? TR("Másik playout…") : TR("Csatlakozás…"));
    if (!on) mix_->stopPreview();
}

int RemoteEditorWindow::send(const QString &op, const QJsonObject &args, std::function<void(const QJsonObject &)> onAck)
{
    const int id = client_->sendOp(op, args);
    if (!id) {
        statusBar()->showMessage(TR("Nincs kapcsolat a playouttal – a módosítás nem ment át."), 8000);
        if (onAck) onAck(QJsonObject{{"ok", false}, {"error", TR("nincs kapcsolat")}});
        return 0;
    }
    pendingOps_.insert(id, op);
    if (onAck) callbacks_.insert(id, std::move(onAck));
    if (!op.startsWith(QLatin1String("upload"))) status_->setText(TR("Küldés a playoutnak…"));
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
        if (auto cb = callbacks_.take(id)) cb(m);
        if (op.startsWith(QLatin1String("upload")) && ok) return;    // a feltöltés a saját állapotát mutatja
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
        // A innen feltöltött fájl a playout feltöltési mappájába került, ami innen nem biztos, hogy látszik.
        if (uploadedLocal_.contains(it.id) && !QFileInfo::exists(it.path)) it.path = uploadedLocal_.value(it.id);
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
        r.missing = o.value("missing").toBool();
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
        if (r.missing) status = TR("HIÁNYZÓ FÁJL");
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
        if (r.missing) { bg = QColor("#3a1d1f"); fg = QColor("#ffb4ae"); }
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
    const bool canMix = client_->can(QStringLiteral("mix")), canDelete = client_->can(QStringLiteral("delete"));
    if (waiting && (canMix || canDelete)) {
        if (!canMix) {
        } else if (row.entry.overlay) {
            menu.addAction(TR("Ráfektetés megszüntetése (önálló elem)"), this, [this, id = row.entry.id] { requestOverlay(id, false); });
        } else {
            menu.addAction(TR("Ráfektetés az előtte és utána álló elem átmenetére"), this,
                           [this, id = row.entry.id] { requestOverlay(id, true); });
        }
        if (canDelete) {
            menu.addSeparator();
            menu.addAction(TR("Törlés a playlistből"), this, [this, id = row.entry.id, title = row.entry.item.title] {
                if (QMessageBox::question(this, TR("Törlés a playlistből"), TR("Törlöd a playlistből: „%1”?").arg(title),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                    requestRemove(id);
            });
        }
    } else if (waiting) {
        menu.addAction(TR("Ehhez a sorhoz nincs jogod műveletet végezni."))->setEnabled(false);
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

// ------------------------------------------------------------- jogok, beszúrás, feltöltés

void RemoteEditorWindow::applyPerms()
{
    const bool order = client_->can(QStringLiteral("order")), insert = client_->can(QStringLiteral("insert")),
               mix = client_->can(QStringLiteral("mix")), upload = client_->can(QStringLiteral("upload"));
    table_->setDragEnabled(order);
    table_->setAcceptDrops(order || insert || upload);
    mix_->setEnabled(mix);
    if (auto *b = library_->findChild<QPushButton *>(QStringLiteral("uploadButton"))) b->setVisible(upload);
    library_->setVisible(insert || upload);
    // A voice track felvétele és fájlból betöltése a playoutra töltődik fel.
    mix_->setVtUploader([this](const QString &path, const QString &title, std::function<void(int)> done) {
        uploadFile(path, 0, true, title, std::move(done));
    }, upload);
    QStringList names;
    for (const QString &p : remote::allPerms())
        if (client_->can(p)) names << remote::permName(p);
    nowLabel_->setToolTip(names.isEmpty() ? TR("Csak megtekintés") : TR("Jogaid: %1").arg(names.join(QStringLiteral(", "))));
    if (names.isEmpty()) statusBar()->showMessage(TR("Ezzel a felhasználóval csak megtekinteni lehet a playlistet."), 10000);
}

void RemoteEditorWindow::requestInsert(const QVector<int> &itemIds, int beforeEntryId)
{
    if (itemIds.isEmpty()) return;
    if (!client_->can(QStringLiteral("insert"))) {
        statusBar()->showMessage(TR("Nincs jogod elemet beszúrni a playlistbe."), 8000);
        return;
    }
    QJsonArray ids;
    for (int id : itemIds) ids.push_back(id);
    send(QStringLiteral("insert"), QJsonObject{{"items", ids}, {"before", beforeEntryId}});
}

int RemoteEditorWindow::chooseUploadCategory()
{
    QDialog dlg(this);
    dlg.setWindowTitle(TR("Feltöltés a playoutra"));
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(TR("Melyik kategóriába kerüljenek a feltöltött elemek a playout hangtárában?")));
    auto *cat = new QComboBox;
    for (const Category &c : db_->itemCategories()) cat->addItem(c.name, c.id);
    cat->setCurrentIndex(std::max(0, cat->findData(QSettings().value("remoteEditor/uploadCategory").toInt())));
    v->addWidget(cat);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(TR("Feltöltés"));
    bb->button(QDialogButtonBox::Cancel)->setText(TR("Mégse"));
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (cat->count() == 0 || dlg.exec() != QDialog::Accepted) return 0;
    QSettings().setValue("remoteEditor/uploadCategory", cat->currentData().toInt());
    return cat->currentData().toInt();
}

void RemoteEditorWindow::uploadPaths(const QStringList &paths, int beforeEntryId)
{
    // beforeEntryId < 0: csak a hangtárba; 0: a hangtárba és a playlistbe (a kézzel betett elemek végére); > 0: e sor elé.
    if (!client_->can(QStringLiteral("upload"))) {
        statusBar()->showMessage(TR("Nincs jogod fájlt feltölteni a playoutra."), 8000);
        return;
    }
    QStringList files;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.isFile() && audioExtensions().contains(fi.suffix().toLower())) files << fi.absoluteFilePath();
    }
    if (files.isEmpty()) {
        statusBar()->showMessage(TR("Nincs feltölthető hangfájl a kijelöltek között."), 8000);
        return;
    }
    const int category = qEnvironmentVariableIsSet("ETER_EDITOR_UPLOADCAT") ? qEnvironmentVariableIntValue("ETER_EDITOR_UPLOADCAT")
                                                                             : chooseUploadCategory();
    if (!category) return;
    const bool insert = beforeEntryId >= 0 && client_->can(QStringLiteral("insert"));
    auto ids = std::make_shared<QVector<int>>();
    auto left = std::make_shared<int>(int(files.size()));
    for (const QString &f : files)
        uploadFile(f, category, false, QString(), [this, ids, left, insert, beforeEntryId](int id) {
            if (id) ids->push_back(id);
            if (--*left == 0 && insert && !ids->isEmpty()) requestInsert(*ids, beforeEntryId);
        });
}

void RemoteEditorWindow::uploadFile(const QString &localPath, int categoryId, bool vt, const QString &title, std::function<void(int)> done)
{
    UploadJob j;
    j.local = localPath;
    j.categoryId = categoryId;
    j.vt = vt;
    j.title = title;
    j.done = std::move(done);
    uploads_.push_back(j);
    if (uploads_.size() == 1) nextUpload();
}

void RemoteEditorWindow::nextUpload()
{
    if (uploads_.isEmpty()) return;
    UploadJob &j = uploads_.first();
    auto fail = [this](const QString &why) {
        UploadJob job = uploads_.takeFirst();
        statusBar()->showMessage(TR("A feltöltés nem sikerült (%1): %2").arg(QFileInfo(job.local).fileName(), why), 12000);
        status_->setText(TR("A feltöltés nem sikerült."));
        if (job.done) job.done(0);
        QTimer::singleShot(0, this, &RemoteEditorWindow::nextUpload);
    };
    j.file = std::make_shared<QFile>(j.local);
    if (!j.file->open(QIODevice::ReadOnly)) {
        fail(TR("a fájl nem olvasható"));
        return;
    }
    j.size = j.file->size();
    j.sent = 0;
    const QString name = QFileInfo(j.local).fileName();
    send(QStringLiteral("uploadBegin"), QJsonObject{{"name", name}, {"size", j.size}, {"category", j.categoryId}, {"vt", j.vt}, {"title", j.title}},
         [this, fail](const QJsonObject &ack) {
             if (uploads_.isEmpty()) return;
             if (!ack.value("ok").toBool()) {
                 fail(ack.value("error").toString());
                 return;
             }
             uploads_.first().token = ack.value("upload").toInt();
             // Darabonként: a következő a nyugta után megy, így a hálózat és a playout lemeze diktálja az iramot.
             auto step = std::make_shared<std::function<void()>>();
             std::weak_ptr<std::function<void()>> weak = step;          // önmagára csak gyengén mutat (nincs körkörös hivatkozás)
             *step = [this, fail, weak] {
                 const auto self = weak.lock();
                 if (!self || uploads_.isEmpty()) return;
                 UploadJob &u = uploads_.first();
                 const QString name = QFileInfo(u.local).fileName();
                 if (u.sent >= u.size) {
                     send(QStringLiteral("uploadEnd"), QJsonObject{{"upload", u.token}}, [this, fail, name](const QJsonObject &end) {
                         if (uploads_.isEmpty()) return;
                         if (!end.value("ok").toBool()) {
                             fail(end.value("error").toString());
                             return;
                         }
                         UploadJob done = uploads_.takeFirst();
                         const int id = end.value("item").toInt();
                         // Az új elem azonnal a tükörbe kerül (a teljes hangtár kicsit később jön), a helyi fájllal.
                         Item it = remote::itemFromJson(end.value("itemData").toObject());
                         it.id = id;
                         it.path = done.local;
                         // Ha a helyi fájl már egy másik elemé a tükörben (ugyanaz a fájl a közös mappában), a playout útvonala marad.
                         const int other = db_->itemIdByPath(done.local);
                         if (other && other != id) it.path = mapPath(end.value("path").toString());
                         else uploadedLocal_.insert(id, done.local);
                         if (db_->itemIdByPath(it.path) && db_->itemIdByPath(it.path) != id) it.path += QStringLiteral("#%1").arg(id);
                         if (it.categoryId) db_->mirrorUpsertItem(it, remote::categoryFromJson(end.value("category").toObject()));
                         status_->setText(TR("Feltöltve: %1").arg(name));
                         statusBar()->showMessage(TR("Feltöltve a playoutra: %1").arg(name), 8000);
                         if (done.done) done.done(id);
                         QTimer::singleShot(0, this, &RemoteEditorWindow::nextUpload);
                     });
                     return;
                 }
                 const QByteArray chunk = u.file->read(512 * 1024);
                 if (chunk.isEmpty()) {
                     fail(TR("a fájl olvasása megszakadt"));
                     return;
                 }
                 const qint64 offset = u.sent;
                 u.sent += chunk.size();
                 status_->setText(TR("Feltöltés: %1 – %2 %").arg(name).arg(u.size ? int(100 * u.sent / u.size) : 100));
                 send(QStringLiteral("uploadChunk"), QJsonObject{{"upload", u.token}, {"offset", offset}, {"data", QString::fromLatin1(chunk.toBase64())}},
                      [fail, self](const QJsonObject &ack) {
                          if (!ack.value("ok").toBool()) fail(ack.value("error").toString());
                          else (*self)();
                      });
             };
             (*step)();
         });
}

void RemoteEditorWindow::previewLibraryItem()
{
    auto &pfl = engine_->source(eter::Engine::Pfl);
    if (libPreview_) {
        pfl.stop();
        libPreview_ = false;
        return;
    }
    const Item it = library_->currentItem();
    if (!it.id) return;
    mix_->stopPreview();
    std::string err;
    if (!QFileInfo::exists(it.path) || !pfl.load(it.path.toStdString(), &err)) {
        statusBar()->showMessage(TR("Nem játszható le ezen a gépen: %1").arg(it.path), 8000);
        return;
    }
    if (it.cueInMs > 0) pfl.seek(it.cueInMs * eter::kRate / 1000);
    pfl.play();
    libPreview_ = true;
    statusBar()->showMessage(TR("Előhallgatás: %1 (újra az Előhallgatás: leállítás)").arg(it.title), 6000);
}
