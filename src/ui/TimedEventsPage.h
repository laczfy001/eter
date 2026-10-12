// Időzítések: hangtárelem vagy mentett playlist beküldése a playlistbe (vagy azonnali indítása)
// a megadott napon és időpontban. A végrehajtást a főablak végzi (MainWindow::runTimedEvents).
#pragma once
#include "db/Database.h"

#include <QPointer>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QDialog;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTimeEdit;

class TimedEventsPage : public QWidget {
    Q_OBJECT
public:
    TimedEventsPage(Database *db, QWidget *parent = nullptr);

    // Új esemény a megadott hangtárelemmel (a hangtár helyi menüjéből).
    void newForItem(int itemId);
    // A következő alkalom a megadott időpont után; érvénytelen, ha nincs több.
    static QDateTime nextOccurrence(const TimedEvent &e, const QDateTime &after);
    static QString daysText(const TimedEvent &e);
    static QString actionText(const QString &action);

signals:
    void message(const QString &text);

private:
    void reload(int selectId = 0);
    void showCurrent();
    void clearForm();
    void save();
    void remove();
    void markDirty();
    void updateSource();
    void reloadPlaylists();

    Database *db_;
    QTableWidget *table_;
    QLabel *formTitle_, *sourceLabel_, *hint_, *nextRun_;
    QRadioButton *srcItem_, *srcPlaylist_, *once_, *weekly_;
    QPushButton *chooseItem_, *save_, *delete_, *new_;
    QComboBox *playlist_, *action_;
    QTimeEdit *time_;
    QDateEdit *date_;
    QCheckBox *days_[7];
    QCheckBox *enabled_;
    QVector<TimedEvent> events_;
    int editingId_ = 0;
    int itemId_ = 0;
    QString itemTitle_;
    bool loading_ = false;
    QPointer<QDialog> picker_;
};
