// A főablak: pontos időre igazítás. A fix idejű elemek (például az óra eleji hírek) előtt kiszámolja, mennyit késik
// vagy siet hozzájuk a playlist, és kitöltést vagy rövidítést javasol, beállítástól függően végre is hajtja.
#include "MainWindow.h"
#include "i18n.h"
#include "Theme.h"

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

MainWindow::FillPlan MainWindow::proposeFill(int fixRowId, qint64 gapMs, int categoryId) const
{
    FillPlan best;
    if (gapMs <= 0 || categoryId <= 0) return best;
    const bool fade = db_->setting("fixed_time_policy", "fade") == QLatin1String("fade");
    // Kiúsztatásnál a kitöltés kicsit túl is lóghat (a fix időpontban úgyis kiúszik); megvárásnál alig.
    const qint64 allowOver = fade ? 15000 : 2000;
    const qint64 titleGap = db_->setting("title_separation_min", "240").toLongLong() * 60;
    const qint64 nowS = eter::now().toSecsSinceEpoch();
    QSet<int> inList;
    QSet<QString> nearArtists;
    const int at = indexOfRow(fixRowId);
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &r = rows_[i];
        if (r.state == Row::Pending || r.state == Row::Cued || r.state == Row::Playing) inList.insert(r.item.id);
        if (i < at && i >= at - 3 && !r.item.artist.trimmed().isEmpty()) nearArtists.insert(r.item.artist.trimmed().toLower());
    }
    QVector<Item> pool;
    for (const Item &it : const_cast<Database *>(db_)->items()) {
        if (it.categoryId != categoryId || it.durationMs <= 0 || inList.contains(it.id) || missingItems_.contains(it.id)) continue;
        if (it.lastPlayedAt > 0 && nowS - it.lastPlayedAt < titleGap) continue;
        if (nearArtists.contains(it.artist.trimmed().toLower())) continue;
        pool << it;
    }
    // A legrégebben szólt elemek előnyben; az azonosak között véletlenszerűen.
    std::shuffle(pool.begin(), pool.end(), *QRandomGenerator::global());
    std::stable_sort(pool.begin(), pool.end(), [](const Item &a, const Item &b) { return a.lastPlayedAt < b.lastPlayedAt; });
    if (pool.size() > 120) pool.resize(120);
    // Pontszám: az eltérés a hiánytól (túllógás csak a megengedett mértékig); több elem kicsit rosszabb.
    auto score = [&](qint64 total, int n) -> qint64 {
        if (total > gapMs + allowOver) return -1;
        return std::llabs(gapMs - total) + (n - 1) * 4000;
    };
    qint64 bestScore = -1;
    auto consider = [&](std::initializer_list<int> idx) {
        qint64 total = 0;
        for (int i : idx) total += pool[i].airMs();
        const qint64 sc = score(total, int(idx.size()));
        if (sc < 0 || (bestScore >= 0 && sc >= bestScore)) return;
        bestScore = sc;
        best.items.clear();
        for (int i : idx) best.items << pool[i];
        best.totalMs = total;
    };
    const int n = int(pool.size());
    for (int a = 0; a < n; ++a) consider({a});
    if (bestScore < 0 || bestScore > 8000)
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b) consider({a, b});
    if (bestScore < 0 || bestScore > 12000) {
        // Hosszú hiány: három elem (a hármasok közül csak a 40 legrégebben szólt elemből, hogy gyors maradjon).
        const int m = std::min(n, 40);
        for (int a = 0; a < m; ++a)
            for (int b = a + 1; b < m; ++b)
                for (int c = b + 1; c < m; ++c) consider({a, b, c});
    }
    // Egy elem se fér bele (például 10 mp hiány): akkor nincs javaslat.
    return best;
}

QVector<QPair<int, qint64>> MainWindow::proposeTrim(int fixRowId, qint64 overMs) const
{
    // A fix pont előtti, még várakozó zenei elemek; a kivételük után ennyi lenne az eltérés (> 0: hiány).
    QVector<QPair<int, qint64>> out;
    const int at = indexOfRow(fixRowId);
    for (int i = 0; i < at; ++i) {
        const Row &r = rows_[i];
        if (r.state != Row::Pending || r.fixedAt.isValid() || r.overlay || r.item.kind != QLatin1String("music")) continue;
        out.push_back({r.id, rowAirMs(r) - overMs});
    }
    // A legjobb elöl: ahol utána a legkisebb a hiány (kis túllógás belefér, azt a kiúsztatás elintézi).
    std::sort(out.begin(), out.end(), [](const QPair<int, qint64> &a, const QPair<int, qint64> &b) {
        auto cost = [](qint64 d) { return d > 0 ? d * 2 : -d; };
        return cost(a.second) < cost(b.second);
    });
    return out;
}

void MainWindow::autoAdjustFixes()
{
    if (mode_ != Auto) return;
    const QString how = db_->setting("fix_adjust", "manual");
    if (how == QLatin1String("manual")) return;
    const FixInfo *f = nextFix();
    if (!f || autoFixed_.contains(f->rowId)) return;
    const qint64 secs = eter::now().secsTo(f->fixedAt);
    if (secs > 20 * 60 || secs < 45) return;              // elég közel, hogy a becslés pontos legyen, de legyen idő
    const int rowId = f->rowId;
    const qint64 diff = f->diffMs;
    if (diff > 20000) {
        autoFixed_.insert(rowId);
        const FillPlan plan = proposeFill(rowId, diff, defaultFillCategory(rowId));
        if (plan.items.isEmpty()) {
            say(TR("Pontos idő: %1 hiányzik a fix pontig, de nincs illő elem a kitöltéshez.").arg(mmssText(diff)), 15000);
            return;
        }
        QVector<int> ids;
        QStringList titles;
        for (const Item &it : plan.items) { ids << it.id; titles << it.title; }
        appendItems(ids, indexOfRow(rowId), 0, {}, QStringLiteral("scheduler"));
        say(TR("Pontos idő: %1 hiányzott, kitöltve: %2.").arg(mmssText(diff), titles.join(QStringLiteral(", "))), 15000);
    } else if (diff < -45000 && how == QLatin1String("both")) {
        autoFixed_.insert(rowId);
        const QVector<QPair<int, qint64>> trims = proposeTrim(rowId, -diff);
        if (trims.isEmpty() || trims.first().second > 15000) return;   // a kivétel nagyobb hiányt hagyna
        const Row *r = rowById(trims.first().first);
        if (!r) return;
        const QString title = r->item.title;
        removeRows({trims.first().first});
        say(TR("Pontos idő: %1 túllógás, kimarad: %2.").arg(mmssText(diff), title), 15000);
    }
}

void MainWindow::openFixDialog()
{
    if (fixDialog_) {
        fixDialog_->close();
    }
    const FixInfo *f0 = nextFix();
    if (!f0) return;
    const FixInfo f = *f0;
    const Row *fixRow = rowById(f.rowId);
    if (!fixRow) return;

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(TR("Igazítás a fix időponthoz"));
    dlg->resize(720, 420);
    auto *v = new QVBoxLayout(dlg);
    auto *info = new QLabel;
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    v->addWidget(info);
    const QString head = TR("<b>%1</b> – %2: a playlist magától <b>%3</b>-kor érne ide.")
                             .arg(f.fixedAt.toString("HH:mm:ss"), fixRow->item.title.toHtmlEscaped(), f.arrive.toString("HH:mm:ss"));
    auto *table = new QTableWidget(0, 3);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    auto *buttons = new QHBoxLayout;
    auto *apply = new QPushButton;
    apply->setProperty("accent", true);
    auto *close = new QPushButton(TR("Bezárás"));
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    const int rowId = f.rowId;

    if (f.diffMs > 0) {
        info->setText(head + TR("<br>%1 hiányzik. Kitöltés a fix elem elé:").arg(mmssText(f.diffMs)));
        auto *catRow = new QHBoxLayout;
        auto *cat = new QComboBox;
        for (const Category &c : db_->itemCategories()) cat->addItem(c.name, c.id);
        cat->setCurrentIndex(std::max(0, cat->findData(defaultFillCategory(rowId))));
        auto *again = new QPushButton(TR("Másik javaslat"));
        catRow->addWidget(new QLabel(TR("Kategória")));
        catRow->addWidget(cat, 1);
        catRow->addWidget(again);
        v->addLayout(catRow);
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({TR("Cím"), TR("Előadó"), TR("Hossz")});
        table->setColumnWidth(0, 300);
        table->setColumnWidth(1, 220);
        auto *sum = new QLabel;
        auto plan = std::make_shared<FillPlan>();
        auto refill = [this, table, cat, sum, plan, apply, rowId, gap = f.diffMs] {
            *plan = proposeFill(rowId, gap, cat->currentData().toInt());
            table->setRowCount(0);
            for (const Item &it : plan->items) {
                const int t = table->rowCount();
                table->insertRow(t);
                table->setItem(t, 0, new QTableWidgetItem(it.title));
                table->setItem(t, 1, new QTableWidgetItem(it.artist));
                table->setItem(t, 2, new QTableWidgetItem(mmssText(it.airMs())));
            }
            if (plan->items.isEmpty()) sum->setText(TR("Ebben a kategóriában nincs a hiányhoz illő elem."));
            else {
                const qint64 left = gap - plan->totalMs;
                sum->setText(left >= 0 ? TR("Összesen %1 · utána %2 hiányzik").arg(mmssText(plan->totalMs), mmssText(left))
                                       : TR("Összesen %1 · utána %2-vel túllóg").arg(mmssText(plan->totalMs), mmssText(left)));
            }
            apply->setEnabled(!plan->items.isEmpty());
        };
        connect(cat, &QComboBox::activated, dlg, refill);
        connect(again, &QPushButton::clicked, dlg, refill);
        apply->setText(TR("Beszúrás a fix elem elé"));
        connect(apply, &QPushButton::clicked, dlg, [this, dlg, plan, rowId] {
            QVector<int> ids;
            for (const Item &it : plan->items) ids << it.id;
            if (ids.isEmpty() || indexOfRow(rowId) < 0) return;
            appendItems(ids, indexOfRow(rowId), 0, {}, QStringLiteral("scheduler"));
            dlg->close();
        });
        v->addWidget(table, 1);
        v->addWidget(sum);
        refill();
    } else {
        info->setText(head + TR("<br>%1-vel túllóg. Kivehető elemek a fix pont előtt (a legjobb elöl):").arg(mmssText(f.diffMs)));
        table->setHorizontalHeaderLabels({TR("Cím"), TR("Hossz"), TR("Utána")});
        table->setColumnWidth(0, 360);
        table->setColumnWidth(1, 90);
        for (const auto &t : proposeTrim(rowId, -f.diffMs)) {
            const Row *r = rowById(t.first);
            if (!r) continue;
            const int n = table->rowCount();
            table->insertRow(n);
            auto *cell = new QTableWidgetItem(r->item.artist.isEmpty() ? r->item.title : r->item.title + QStringLiteral(" – ") + r->item.artist);
            cell->setData(Qt::UserRole, r->id);
            table->setItem(n, 0, cell);
            table->setItem(n, 1, new QTableWidgetItem(mmssText(rowAirMs(*r))));
            table->setItem(n, 2, new QTableWidgetItem(t.second > 0 ? TR("%1 hiány").arg(mmssText(t.second))
                                                                    : TR("%1 túllógás").arg(mmssText(t.second))));
        }
        if (table->rowCount()) table->selectRow(0);
        apply->setText(TR("A kijelölt kivétele a playlistből"));
        apply->setEnabled(table->rowCount() > 0);
        connect(apply, &QPushButton::clicked, dlg, [this, dlg, table] {
            const int r = table->currentRow();
            if (r < 0) return;
            removeRows({table->item(r, 0)->data(Qt::UserRole).toInt()});
            dlg->close();
        });
        v->addWidget(table, 1);
        if (db_->setting("fixed_time_policy", "fade") == QLatin1String("fade"))
            v->addWidget(new QLabel(TR("Ha nem veszel ki semmit, a fix időpontban az éppen szóló elem kiúszik.")));
    }
    buttons->addStretch(1);
    buttons->addWidget(apply);
    buttons->addWidget(close);
    v->addLayout(buttons);
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
