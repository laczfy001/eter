// Playlist szerkesztő: egyéni playlist összeállítása a hangtárból, mentése playlist típusú
// kategóriába, és beillesztése a fő playlistbe.
#pragma once
#include "db/Database.h"

#include <QPointer>
#include <QWidget>

class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class LibraryPanel;
class PlaylistTable;

class PlaylistEditorPage : public QWidget {
    Q_OBJECT
public:
    PlaylistEditorPage(Database *db, QWidget *parent = nullptr);

    // Fejlesztői ellenőrzéshez és a főablaknak
    void newPlaylist();
    void addItems(const QVector<int> &itemIds, int at = -1);
    void setName(const QString &name);
    bool save();
    int currentId() const { return id_; }
    int itemCount() const { return items_.size(); }
    bool dirty() const { return dirty_; }

signals:
    void insertRequested(int savedPlaylistId);
    void editRequested(int itemId);
    void message(const QString &text);

private:
    void reloadSaved();
    void reloadCategories();
    void open(int id);
    void refreshTable();
    void updateState();
    void removeSelected();
    void moveRow(int from, int to);
    void deleteCurrent();
    bool confirmDiscard();

    Database *db_;
    QComboBox *filter_, *category_;
    QListWidget *saved_;
    QLineEdit *name_;
    PlaylistTable *table_;
    QLabel *info_, *title_, *hint_;
    QPushButton *save_, *insert_, *remove_, *delete_;
    LibraryPanel *library_;
    QVector<Item> items_;
    int id_ = 0;                 // a szerkesztett mentett playlist; 0 = még nincs elmentve
    bool dirty_ = false;
    bool loading_ = false;
};
