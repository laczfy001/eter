// Ütemező munkaterület: heti rács, órasémák szerkesztése, szabályok, generálás.
#pragma once
#include "sched/Scheduler.h"

#include <QDialog>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimeEdit;
class QTableView;
class LibraryModel;

class SchedulerPage : public QWidget {
    Q_OBJECT
public:
    SchedulerPage(Database *db, Scheduler *scheduler, QWidget *parent = nullptr);
    void generateNow();             // mostantól a beállított óraszámra (újra) generál
    void generateDialog();          // a „Playlistek generálása” gomb: melyik napok, melyik órák
    QString generateInfo() const;   // a legutóbbi generálás összegzése (fejlesztéshez)
    void refreshAll() { categoryAvgMs_.clear(); reloadGrid(); reloadClocks(); }   // kívülről változott adatok után
    void refreshFixSettings();      // a fix pontok igazításának beállításai kívülről változtak

signals:
    void generated(const GenerateResult &result);
    void message(const QString &text);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QWidget *buildGridPanel();
    QWidget *buildClockPanel();
    void reloadGrid();
    void reloadGridList();
    void reportGenerated(const GenerateResult &r, int firstKey);                // a heti rácsok listája (előbeállítások)
    void editPeriods();                   // időszakok: mikor melyik rács érvényes
    int editGrid_ = 1;                    // a szerkesztett heti rács
    class QComboBox *gridSel_ = nullptr;
    QLabel *gridState_ = nullptr;
    QPushButton *gridActive_ = nullptr, *gridDelete_ = nullptr;
    void reloadClocks(int selectId = 0);
    void reloadSlots(int selectSlotId = 0);
    void showSlot();
    void applySlot();
    void assignSelection(int clockId);
    int currentClockId() const;
    int currentSlotRow() const;
    void pickItem();
    void moveSlot(int delta);
    void moveSlotTo(int from, int to);     // húzással: a sor a „to” helyre kerül (a többi előtt)

    Database *db_;
    Scheduler *scheduler_;
    QTableWidget *grid_;
    QPushButton *assign_;
    QLabel *genInfo_;

    QComboBox *clock_;
    QLineEdit *clockName_;
    QComboBox *clockColor_;
    QTableWidget *slotTable_;
    QLabel *total_;
    QLabel *overflow_ = nullptr;          // figyelmeztetés: a darabszámos elemek nem férnek a fix pontok közé
    class QPushButton *overflowFix_ = nullptr;
    QVector<int> overflowSlots_;          // ezek a sorok csordulnak túl
    class QCheckBox *slotUpToFit_ = nullptr;
    void fixOverflow();
    QVector<ClockSlot> slots_;
    QHash<int, qint64> categoryAvgMs_;

    QWidget *slotForm_;
    QComboBox *slotType_, *slotCategory_;
    QLabel *slotCategoryLabel_, *slotItemLabel_, *slotCountLabel_;
    QPushButton *slotPick_;
    QSpinBox *slotCount_;
    QComboBox *slotEnergyMin_, *slotEnergyMax_;
    QLabel *slotEnergyLabel_, *slotEnergyDash_;
    QCheckBox *slotFixed_;
    QTimeEdit *slotTime_;
    QLabel *slotHint_;
    class QLineEdit *slotNote_ = nullptr;           // „Jegyzet”: a felirat
    QComboBox *slotSweeper_ = nullptr;              // sweeper az ide vezető átmenetre
    class QDoubleSpinBox *slotSweeperDuck_ = nullptr;
    QLabel *slotSweeperLabel_ = nullptr, *slotSweeperDuckLabel_ = nullptr;
    bool loading_ = false;

    QSpinBox *artistGap_, *titleGap_, *ahead_;
    QComboBox *policy_, *fixAdjust_, *fixCategory_;
    void reloadFixCategories();
    void reloadClockFill();               // a kijelölt séma saját „Kitöltés ebből” beállítása
    QComboBox *clockFill_ = nullptr;
    QCheckBox *autoGen_;
    QPointer<QDialog> picker_;
};

// Egy elem kiválasztása a hangtárból (órasémába tett konkrét elemhez).
class ItemPickerDialog : public QDialog {
    Q_OBJECT
public:
    // rememberKey: ezen a beállításon jegyzi meg a legutóbb választott kategóriát (üres: nem jegyzi meg)
    ItemPickerDialog(Database *db, QWidget *parent, const QString &rememberKey = {});
signals:
    void picked(int itemId);
private:
    LibraryModel *model_;
    QTableView *view_;
};
