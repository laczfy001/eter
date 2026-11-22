// A főablak: pontos időre igazítás. A fix idejű elemek (például az óra eleji hírek) előtt kiszámolja, mennyit késik
// vagy siet hozzájuk a playlist, és kitöltést vagy rövidítést javasol, beállítástól függően végre is hajtja.
#include "MainWindow.h"
#include "i18n.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace {
QString mmssText(qint64 ms)
{
    ms = std::llabs(ms);
    const qint64 s = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
}

qint64 MainWindow::rowAirMs(const Row &r)
{
    // Ugyanígy számol a kezdési idők becslése is: a mix pontig, ennek híján a teljes hossz.
    return r.item.mixOutMs > 0 ? r.item.mixOutMs : r.frames * 1000 / eter::kRate;
}

const MainWindow::FixInfo *MainWindow::nextFix() const
{
    return fixes_.isEmpty() ? nullptr : &fixes_.first();
}

void MainWindow::updateFixBar()
{
    if (!fixBar_) return;
    const FixInfo *f = nextFix();
    const QDateTime now = eter::now();
    const Row *row = f ? const_cast<MainWindow *>(this)->rowById(f->rowId) : nullptr;
    if (!f || !row || now.secsTo(f->fixedAt) > 90 * 60) {
        fixBar_->hide();
        return;
    }
    const bool fade = db_->setting("fixed_time_policy", "fade") == QLatin1String("fade");
    const QString head = QStringLiteral("<b>%1</b> · %2").arg(f->fixedAt.toString("HH:mm:ss"), row->item.title.toHtmlEscaped());
    QString text, color;
    bool action = true;
    if (std::llabs(f->diffMs) < 5000) {
        text = TR("%1 — pontosan érkezik").arg(head);
        color = QStringLiteral("#1b2e24");
        action = false;
    } else if (f->diffMs > 0) {
        text = TR("%1 — <span style=\"color:#ffb4ae\"><b>%2 hiányzik</b></span>").arg(head, mmssText(f->diffMs));
        text += mode_ == Auto ? TR(" (előtte csend lenne)") : TR(" (előtte elfogy a műsor)");
        color = QStringLiteral("#3a1d1f");
    } else {
        text = TR("%1 — <span style=\"color:#ffd27a\"><b>%2-vel túllóg</b></span>").arg(head, mmssText(f->diffMs));
        text += fade ? TR(" (az előtte szóló elem ki lesz úsztatva)") : TR(" (a fix elem ennyit késik)");
        color = QStringLiteral("#33291a");
    }
    fixText_->setText(text);
    fixBtn_->setVisible(action);
    fixBar_->setStyleSheet(QStringLiteral("#fixBar { background: %1; border-bottom: 1px solid #0f1114; }").arg(color));
    fixBar_->show();
}

int MainWindow::defaultFillCategory(int fixRowId) const
{
    const int chosen = db_->setting("fix_fill_category", "0").toInt();
    if (chosen > 0) return chosen;
    // A fix pont előtti utolsó zenei elem kategóriája.
    const int at = indexOfRow(fixRowId);
    for (int i = at - 1; i >= 0; --i)
        if (rows_[i].item.kind == QLatin1String("music")) return rows_[i].item.categoryId;
    for (const Category &c : db_->itemCategories())
        if (c.kind == QLatin1String("music")) return c.id;
    return 0;
}

// Az eltérés „ára”: a csend (hiány) rosszabb, mint az ugyanakkora túllógás, amit a fix időpontban a kiúsztatás
// elintéz; a tűrésen túli túllógás meredeken drágul (az a hallgató számára hirtelen vágás).
qint64 MainWindow::adjustCost(qint64 d) const
{
    const qint64 tol = std::clamp(db_->setting("fill_tolerance_s", "15").toLongLong(), 0LL, 120LL) * 1000;
    const bool fade = db_->setting("fixed_time_policy", "fade") == QLatin1String("fade");
    const qint64 over = fade ? std::max<qint64>(tol, 3000) : 2000;
    if (d >= 0) return d <= 1500 ? d : 1500 + (d - 1500) * 3;
    const qint64 o = -d;
    return o <= over ? o : over + (o - over) * 4;
}

bool MainWindow::adjustAcceptable(qint64 d) const
{
    const qint64 tol = std::clamp(db_->setting("fill_tolerance_s", "15").toLongLong(), 0LL, 120LL) * 1000;
    const bool fade = db_->setting("fixed_time_policy", "fade") == QLatin1String("fade");
    const qint64 over = fade ? std::max<qint64>(tol, 3000) : 2000;
    // Néhány másodperc csend még elmegy (a fix elem úgyis a saját idejében indul), a túllógás a tűrésig.
    return d <= std::min<qint64>(std::max<qint64>(tol / 3, 2000), 5000) && -d <= over;
}

QVector<Item> MainWindow::fillPool(int fixRowId, int categoryId, const QSet<int> &avoidItems) const
{
    const int at = indexOfRow(fixRowId);
    const QDateTime now = eter::now();
    // Beszúrható elemek a kategóriából: nem hiányzik a fájljuk, az előadójuk nem szól közvetlenül a fix pont előtt,
    // és a fix időponthoz képest a szám-ismétlési időn belül nem szólnak (se a naplóban, se a playlistben máshol).
    // A playlist többi része nem zárja ki őket: 24 órányi előre generált listában egy kis hangtár minden száma
    // szerepel valahol, de egy hat órával későbbi ismétlés nem akadály. Ha így kevés a jelölt, az ismétlési időt
    // fokozatosan rövidíti (felére, majd fél órára).
    QVector<Item> pool;
    if (at < 0) return pool;
    if (categoryId > 0) {
        const qint64 titleGap = db_->setting("title_separation_min", "240").toLongLong() * 60;
        const Row *fixRow = const_cast<MainWindow *>(this)->rowById(fixRowId);
        const qint64 fixS = (fixRow && fixRow->fixedAt.isValid() ? fixRow->fixedAt : now).toSecsSinceEpoch();
        QHash<int, QVector<qint64>> times;   // elem → mikor szól (mp)
        QSet<QString> nearArtists;
        for (int i = 0; i < rows_.size(); ++i) {
            const Row &r = rows_[i];
            if (r.state == Row::Playing) times[r.item.id] << (r.startedAt.isValid() ? r.startedAt : now).toSecsSinceEpoch();
            else if (r.state == Row::Played && r.startedAt.isValid()) times[r.item.id] << r.startedAt.toSecsSinceEpoch();
            else if (r.state == Row::Pending || r.state == Row::Cued)
                times[r.item.id] << plannedStart_.value(r.id, fixRow ? fixRow->fixedAt : now).toSecsSinceEpoch();
            if (i < at && i >= at - 4 && !r.item.artist.trimmed().isEmpty()) nearArtists.insert(r.item.artist.trimmed().toLower());
        }
        struct Cand { Item item; qint64 nearest; };
        QVector<Cand> all;
        for (const Item &it : const_cast<Database *>(db_)->items()) {
            if (it.categoryId != categoryId || it.airMs() <= 0 || missingItems_.contains(it.id) || avoidItems.contains(it.id)) continue;
            if (const qint64 t = removedByHand_.value(it.id); t && QDateTime::currentMSecsSinceEpoch() - t < 2 * 3600 * 1000LL) continue;
            if (nearArtists.contains(it.artist.trimmed().toLower())) continue;
            qint64 nearest = std::numeric_limits<qint64>::max();
            for (qint64 t : times.value(it.id)) nearest = std::min(nearest, std::llabs(t - fixS));
            if (it.lastPlayedAt > 0) nearest = std::min(nearest, std::llabs(fixS - it.lastPlayedAt));
            all.push_back({it, nearest});
        }
        for (qint64 gap : {titleGap, titleGap / 2, qint64(1800)}) {
            pool.clear();
            for (const Cand &c : all)
                if (c.nearest >= gap) pool << c.item;
            if (pool.size() >= 8) break;
        }
        // A fix ponttól legtávolabb szólók előnyben (órás pontossággal; azon belül véletlenszerűen).
        QHash<int, qint64> dist;
        for (const Cand &c : all) dist.insert(c.item.id, std::min<qint64>(c.nearest, 48 * 3600) / 3600);
        std::shuffle(pool.begin(), pool.end(), *QRandomGenerator::global());
        std::stable_sort(pool.begin(), pool.end(), [&](const Item &a, const Item &b) { return dist.value(a.id) > dist.value(b.id); });
        if (pool.size() > 120) pool.resize(120);
    }
    return pool;
}

MainWindow::AdjustPlan MainWindow::planAdjust(int fixRowId, qint64 diffMs, int categoryId, bool allowRemove,
                                              const QSet<int> &avoidItems) const
{
    AdjustPlan best;
    best.afterMs = diffMs;
    best.cost = adjustCost(diffMs);
    const int at = indexOfRow(fixRowId);
    if (at < 0) return best;
    const QDateTime now = eter::now();

    // Kivehető sorok: a fix pont előtt, zene, az ütemező tette be (a kézzel betett elemhez nem nyúlunk), nem blokk
    // része, nem ráfektetés szomszédja, és legalább másfél perc múlva indulna (a betöltött lejátszó még cserélhető).
    struct Removable { int rowId; qint64 ms; };
    QVector<Removable> removable;
    if (allowRemove) {
        for (int i = at - 1; i >= 0 && removable.size() < 6; --i) {
            const Row &r = rows_[i];
            if (r.state == Row::Playing || r.state == Row::Played) break;
            if (r.state != Row::Pending && r.state != Row::Cued) continue;
            if (r.fixedAt.isValid() || r.overlay || r.groupId || !r.scheduled || r.item.kind != QLatin1String("music")) continue;
            if (i + 1 < rows_.size() && rows_[i + 1].overlay) continue;
            if (i > 0 && rows_[i - 1].overlay) continue;
            const QDateTime start = plannedStart_.value(r.id);
            if (!start.isValid() || now.msecsTo(start) < 90000) continue;
            removable.push_back({r.id, rowAirMs(r)});
        }
    }

    const QVector<Item> pool = fillPool(fixRowId, categoryId, avoidItems);
    const int n = int(pool.size()), n3 = std::min(n, 40);
    auto artist = [&](int i) { return pool[i].artist.trimmed().toLower(); };
    auto distinct = [&](int a, int b) { return artist(a).isEmpty() || artist(a) != artist(b); };

    // Minden kivétel-változat (semmi, egy vagy két sor) mellé a legjobb 0–3 beszúrt elem. Költség: az eltérés ára,
    // plusz minden változtatás egy kicsit (a kevesebb beavatkozás jobb), és a frissebb elemek egy hajszállal.
    auto evaluate = [&](const QVector<int> &removeIdx) {
        qint64 gap = diffMs;
        for (int k : removeIdx) gap += removable[k].ms;
        const qint64 base = 4000 * removeIdx.size();
        auto take = [&](std::initializer_list<int> add) {
            qint64 sum = 0, rank = 0;
            for (int i : add) { sum += pool[i].airMs(); rank += i; }
            const qint64 d = gap - sum;
            const qint64 c = adjustCost(d) + base + 1500 * qint64(add.size()) + rank * 20;
            if (c >= best.cost) return;
            best.cost = c;
            best.afterMs = d;
            best.remove.clear();
            for (int k : removeIdx) best.remove << removable[k].rowId;
            best.add.clear();
            for (int i : add) best.add << pool[i];
        };
        take({});
        if (gap <= 0) return;   // semmi sem fér be
        for (int a = 0; a < n; ++a) take({a});
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b) {
                if (pool[a].airMs() + pool[b].airMs() > gap + 120000 || !distinct(a, b)) continue;
                take({a, b});
                if (a < n3 && b < n3)
                    for (int c = b + 1; c < n3; ++c)
                        if (distinct(a, c) && distinct(b, c)) take({a, b, c});
            }
    };
    evaluate({});
    for (int i = 0; i < removable.size(); ++i) {
        evaluate({i});
        for (int j = i + 1; j < removable.size(); ++j) evaluate({i, j});
    }
    return best;
}

void MainWindow::applyAdjust(int fixRowId, const AdjustPlan &plan)
{
    if (!plan.remove.isEmpty()) {
        autoRemoving_ = true;
        removeRows(plan.remove);
        autoRemoving_ = false;
    }
    QVector<int> ids;
    for (const Item &it : plan.add) ids << it.id;
    const int at = indexOfRow(fixRowId);
    if (!ids.isEmpty() && at >= 0) appendItems(ids, at, 0, {}, QStringLiteral("scheduler"));
}

static QString diffText(qint64 d)
{
    if (std::llabs(d) < 1500) return TR("pontosan érkezik");
    return d > 0 ? TR("%1 hiányzik").arg(mmssText(d)) : TR("%1-vel túllóg").arg(mmssText(d));
}

void MainWindow::autoAdjustFixes()
{
    if (mode_ != Auto) return;
    const QString how = db_->setting("fix_adjust", "manual");
    const bool gapFill = db_->setting("fix_gap_fill", "0") == QLatin1String("1");
    if (how == QLatin1String("manual") && !gapFill) return;
    const FixInfo *f = nextFix();
    if (!f) return;
    const qint64 secs = eter::now().secsTo(f->fixedAt);
    if (secs > 30 * 60 || secs < 45) return;              // elég közel, hogy a becslés pontos legyen, de legyen idő
    const int rowId = f->rowId;
    const qint64 diff = f->diffMs;
    if (adjustAcceptable(diff)) return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    AutoFixState &st = autoFixed_[rowId];
    // A fix pont előtti, még szóló vagy várakozó sorok lenyomata: ha változott (törlés, beszúrás, átrendezés, a
    // következő szám elindult), 3 mp múlva azonnal újra próbál, nem várja ki a két percet.
    size_t sig = 0;
    for (const Row &r : rows_) {
        if (r.id == rowId) break;
        if (r.state == Row::Pending || r.state == Row::Cued || r.state == Row::Playing)
            sig = sig * 1000003u + size_t(r.id) * 7u + size_t(r.state);
    }
    if (sig != st.sig) {
        st.sig = sig;
        st.sigAt = nowMs;
        st.lastTry = st.lastGapTry = 0;
    }
    if (nowMs - st.sigAt < 3000) return;   // a kézi szerkesztés még tarthat
    if (how != QLatin1String("manual")) {
        // Ha most nincs jobb megoldás, két perc múlva (vagy a playlist változásakor) újra megnézi. Biztonsági korlát:
        // fix pontonként legfeljebb 20 végrehajtott igazítás.
        if (st.applied < 20 && (st.lastTry == 0 || nowMs - st.lastTry >= 120000)) {
            st.lastTry = nowMs;
            const AdjustPlan plan = planAdjust(rowId, diff, defaultFillCategory(rowId), how == QLatin1String("both"));
            if (plan.changes() && plan.cost + 5000 < adjustCost(diff)) {
                ++st.applied;
                QStringList out, in;
                for (int id : plan.remove)
                    if (const Row *r = rowById(id)) out << r->item.title;
                for (const Item &it : plan.add) in << it.title;
                applyAdjust(rowId, plan);
                QString msg = TR("Pontos idő: %1 helyett %2.").arg(diffText(diff), diffText(plan.afterMs));
                if (!out.isEmpty()) msg += TR(" Kimarad: %1.").arg(out.join(QStringLiteral(", ")));
                if (!in.isEmpty()) msg += TR(" Bekerül: %1.").arg(in.join(QStringLiteral(", ")));
                say(msg, 20000);
                return;
            }
            if (!gapFill || diff < 0)
                say(TR("Pontos idő: a fix pontig %1, de nincs jobb megoldás a hangtárból.").arg(diffText(diff)), 15000);
        }
    }
    // Hiány (csend lenne a fix pont előtt): egy szám, amely kitölti; kicsit túllóghat, azt a fix időpontban a
    // kiúsztatás elintézi. Percenként legfeljebb egyszer (ha egy szám nem elég, a következő körben jön még egy).
    if (gapFill && diff > 0) {
        if (st.lastGapTry && nowMs - st.lastGapTry < 60000) return;
        st.lastGapTry = nowMs;
        if (st.applied < 20 && gapFillOne(rowId, diff)) ++st.applied;
    }
}

bool MainWindow::gapFillOne(int fixRowId, qint64 gapMs)
{
    const qint64 maxOver = std::clamp(db_->setting("fix_gap_over_s", "20").toLongLong(), 0LL, 600LL) * 1000;
    const QVector<Item> pool = fillPool(fixRowId, defaultFillCategory(fixRowId));
    // A legjobb: legalább akkora, mint a hiány, és legfeljebb maxOver-rel hosszabb – ezek közül a legkisebb
    // túllógású. Ha ilyen nincs: a legkisebb túllógású (a csendnél jobb, a fix időpontban kiúszik); ha minden szám
    // rövidebb a hiánynál: a leghosszabb (a maradékot a következő kör tölti ki).
    int best = -1, over = -1, longest = -1;
    for (int i = 0; i < pool.size(); ++i) {
        const qint64 ms = pool[i].airMs();
        if (ms >= gapMs) {
            const bool within = ms - gapMs <= maxOver;
            const bool bestWithin = best >= 0 && pool[best].airMs() - gapMs <= maxOver;
            // A tűrésen belül a sorrend (a fix ponttól legtávolabb szóló) számít, azon kívül a kisebb túllógás.
            if (best < 0 || (within && !bestWithin) || (!within && !bestWithin && ms < pool[best].airMs())) best = i;
        } else if (longest < 0 || ms > pool[longest].airMs()) {
            longest = i;
        }
    }
    const int pick = best >= 0 ? best : longest;
    if (pick < 0) {
        say(TR("Pontos idő: %1 hiányzik, de a kitöltés kategóriájában nincs beszúrható szám.").arg(mmssText(gapMs)), 15000);
        return false;
    }
    const Item it = pool[pick];
    over = int(it.airMs() - gapMs);
    const int at = indexOfRow(fixRowId);
    if (at < 0) return false;
    appendItems({it.id}, at, 0, {}, QStringLiteral("scheduler"));
    say(over >= 0 ? TR("Kitöltés: %1 hiányzott, bekerült „%2” (%3); %4-vel túllóg, a fix időpontban kiúszik.")
                        .arg(mmssText(gapMs), it.title, mmssText(it.airMs()), mmssText(over))
                  : TR("Kitöltés: %1 hiányzott, bekerült „%2” (%3); még %4 hiányzik.")
                        .arg(mmssText(gapMs), it.title, mmssText(it.airMs()), mmssText(-over)),
        20000);
    return true;
}

void MainWindow::openFixDialog()
{
    if (fixDialog_) fixDialog_->close();
    const FixInfo *f0 = nextFix();
    if (!f0) return;
    const FixInfo f = *f0;
    const Row *fixRow = rowById(f.rowId);
    if (!fixRow) return;

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(TR("Igazítás a fix időponthoz"));
    dlg->resize(760, 440);
    auto *v = new QVBoxLayout(dlg);
    auto *info = new QLabel;
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    info->setText(TR("<b>%1</b> – %2: a playlist magától <b>%3</b>-kor érne ide, azaz %4.")
                      .arg(f.fixedAt.toString("HH:mm:ss"), fixRow->item.title.toHtmlEscaped(), f.arrive.toString("HH:mm:ss"),
                           diffText(f.diffMs)));
    v->addWidget(info);
    auto *catRow = new QHBoxLayout;
    auto *cat = new QComboBox;
    for (const Category &c : db_->itemCategories()) cat->addItem(c.name, c.id);
    cat->setCurrentIndex(std::max(0, cat->findData(defaultFillCategory(f.rowId))));
    auto *allowRemove = new QCheckBox(TR("Elemet ki is vehet (cserélhet)"));
    allowRemove->setChecked(true);
    allowRemove->setToolTip(TR("Az ütemező által betett, még el nem indult zenéket kiveheti, és jobban illő számokat tehet a helyükre"));
    auto *again = new QPushButton(TR("Másik javaslat"));
    catRow->addWidget(new QLabel(TR("Kitöltés ebből")));
    catRow->addWidget(cat, 1);
    catRow->addWidget(allowRemove);
    catRow->addWidget(again);
    v->addLayout(catRow);
    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({QString(), TR("Cím"), TR("Előadó"), TR("Hossz")});
    table->verticalHeader()->hide();
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 110);
    table->setColumnWidth(1, 300);
    table->setColumnWidth(2, 200);
    v->addWidget(table, 1);
    auto *sum = new QLabel;
    sum->setWordWrap(true);
    v->addWidget(sum);
    auto *buttons = new QHBoxLayout;
    auto *apply = new QPushButton(TR("Igazítás"));
    apply->setProperty("accent", true);
    auto *close = new QPushButton(TR("Bezárás"));
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    const int rowId = f.rowId;
    auto plan = std::make_shared<AdjustPlan>();
    auto avoid = std::make_shared<QSet<int>>();
    auto refill = [this, table, cat, allowRemove, sum, plan, avoid, apply, rowId, diff = f.diffMs] {
        *plan = planAdjust(rowId, diff, cat->currentData().toInt(), allowRemove->isChecked(), *avoid);
        table->setRowCount(0);
        auto addRow = [table](const QString &what, const QColor &color, const Item &it, qint64 ms) {
            const int t = table->rowCount();
            table->insertRow(t);
            auto *w = new QTableWidgetItem(what);
            w->setForeground(color);
            table->setItem(t, 0, w);
            table->setItem(t, 1, new QTableWidgetItem(it.title));
            table->setItem(t, 2, new QTableWidgetItem(it.artist));
            table->setItem(t, 3, new QTableWidgetItem(mmssText(ms)));
        };
        for (int id : plan->remove)
            if (const Row *r = rowById(id)) addRow(TR("− kimarad"), QColor("#ffb4ae"), r->item, rowAirMs(*r));
        for (const Item &it : plan->add) addRow(TR("+ bekerül"), QColor("#9be8b8"), it, it.airMs());
        if (!plan->changes()) sum->setText(TR("Nincs jobb megoldás: a hangtárban nincs ide illő elem."));
        else sum->setText(TR("Utána: %1.").arg(diffText(plan->afterMs))
                          + (plan->afterMs < -1500 && db_->setting("fixed_time_policy", "fade") == QLatin1String("fade")
                                 ? TR(" A fix időpontban az éppen szóló elem kiúszik.") : QString()));
        apply->setEnabled(plan->changes());
    };
    connect(cat, &QComboBox::activated, dlg, refill);
    connect(allowRemove, &QCheckBox::toggled, dlg, refill);
    connect(again, &QPushButton::clicked, dlg, [plan, avoid, refill] {
        for (const Item &it : plan->add) avoid->insert(it.id);
        refill();
    });
    connect(apply, &QPushButton::clicked, dlg, [this, dlg, plan, rowId] {
        if (!plan->changes() || indexOfRow(rowId) < 0) return;
        applyAdjust(rowId, *plan);
        dlg->close();
    });
    buttons->addStretch(1);
    buttons->addWidget(apply);
    buttons->addWidget(close);
    v->addLayout(buttons);
    refill();
    fixDialog_ = dlg;
    dlg->show();
}

QString MainWindow::fixDebug() const
{
    QStringList out;
    for (const FixInfo &f : fixes_) {
        const Row *r = const_cast<MainWindow *>(this)->rowById(f.rowId);
        out << QStringLiteral("%1 %2 érkezés %3 eltérés %4%5").arg(f.fixedAt.toString("HH:mm:ss"), r ? r->item.title : QString(),
                                                                  f.arrive.toString("HH:mm:ss"), f.diffMs >= 0 ? "−" : "+",
                                                                  mmssText(f.diffMs));
    }
    return out.join(QStringLiteral(" | "));
}
