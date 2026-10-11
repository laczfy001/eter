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
    void generateNow();

signals:
    void generated(const GenerateResult &result);
    void message(const QString &text);

private:
    QWidget *buildGridPanel();
    QWidget *buildClockPanel();
    void reloadGrid();
    void reloadClocks(int selectId = 0);
    void reloadSlots(int selectSlotId = 0);
    void showSlot();
    void applySlot();
    void assignSelection(int clockId);
    int currentClockId() const;
    int currentSlotRow() const;
    void pickItem();
    void moveSlot(int delta);

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
    QVector<ClockSlot> slots_;
    QHash<int, qint64> categoryAvgMs_;

    QWidget *slotForm_;
    QComboBox *slotType_, *slotCategory_;
    QLabel *slotCategoryLabel_, *slotItemLabel_, *slotCountLabel_;
    QPushButton *slotPick_;
    QSpinBox *slotCount_;
    QCheckBox *slotFixed_;
    QTimeEdit *slotTime_;
    QLabel *slotHint_;
    bool loading_ = false;

    QSpinBox *artistGap_, *titleGap_, *ahead_;
    QComboBox *policy_;
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
