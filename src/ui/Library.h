// Hangtár panel és a hozzá tartozó ablakok: importálás, kategóriák, adásnapló.
#pragma once
#include "db/Database.h"

#include <QAbstractTableModel>
#include <QDialog>
#include <QFrame>
#include <QPointer>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableView;
class QTableWidget;
class QTimer;
class QVBoxLayout;

inline const char *kItemsMime = "application/x-eter-items";
QStringList audioExtensions();
QByteArray encodeIds(const QVector<int> &ids);
QVector<int> decodeIds(const QByteArray &data);
QString formatMs(qint64 ms);
QColor readableOn(const QColor &background);
QIcon swatchIcon(const QColor &color);
void fillColorCombo(QComboBox *combo);
QVector<QPair<QString, QString>> paletteColors();   // név, #rrggbb

class LibraryModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LibraryModel(Database *db, QObject *parent = nullptr);
    void reload();
    void setFilter(int categoryId, const QString &text);
    const Item *itemAt(int row) const;
    int totalCount() const { return all_.size(); }

    int rowCount(const QModelIndex & = {}) const override { return rows_.size(); }
    int columnCount(const QModelIndex & = {}) const override { return 5; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    void sort(int column, Qt::SortOrder order) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override { return {QString::fromLatin1(kItemsMime)}; }
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

private:
    void apply();
    Database *db_;
    QVector<Item> all_;
    QVector<int> rows_;              // indexek az all_ tömbbe
    int categoryId_ = 0;
    QString text_;
    int sortColumn_ = 1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
};

class LibraryPanel : public QFrame {
    Q_OBJECT
public:
    using Done = std::function<void(const QVector<int> &itemIds)>;

    explicit LibraryPanel(Database *db, QWidget *parent = nullptr);
    void setFooter(QWidget *footer);
    QVector<int> selectedItemIds() const;
    Item currentItem() const;
    bool hasFocusInTable() const;

    void chooseFiles();
    void chooseFolder();
    // Fájlok és mappák beolvasása; kategóriát az importáló ablakban kell választani.
    void importPaths(const QStringList &paths, Done done = {});
    void openCategories();

signals:
    void addToPlaylist(const QVector<int> &itemIds);
    void previewRequested();
    void editRequested(int itemId);
    void selectionTouched();
    void message(const QString &text);

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    void reloadCategories();
    void contextMenu(const QPoint &pos);
    void deleteSelected();

    Database *db_;
    LibraryModel *model_;
    QTableView *view_;
    QComboBox *category_;
    QLineEdit *search_;
    QLabel *empty_;
    QVBoxLayout *layout_;
    QPointer<QDialog> importDialog_, categoriesDialog_;
};

class ImportDialog : public QDialog {
    Q_OBJECT
public:
    ImportDialog(Database *db, const QStringList &paths, QWidget *parent);

signals:
    void imported(const QVector<int> &itemIds, int added, int existing, int failed);
    void categoriesRequested();

private:
    void rescan();
    void reloadCategories(int selectId = 0);
    void start();
    void step();

    Database *db_;
    QStringList paths_, files_;
    QLabel *summary_;
    QCheckBox *recursive_;
    QComboBox *category_;
    QProgressBar *progress_;
    QPushButton *go_, *cancel_, *manage_;
    QTimer *timer_;
    QVector<int> ids_;
    int index_ = 0, added_ = 0, existing_ = 0, failed_ = 0, categoryId_ = 0;
    bool running_ = false;
};

class CategoriesDialog : public QDialog {
    Q_OBJECT
public:
    CategoriesDialog(Database *db, QWidget *parent);

private:
    void reload(int selectId = 0);
    void showCurrent();
    void save();
    void remove();

    Database *db_;
    QListWidget *list_;
    QLineEdit *name_;
    QComboBox *kind_, *color_;
    QLabel *info_;
    QPushButton *save_, *delete_;
    int editingId_ = 0;
};

class LogDialog : public QDialog {
    Q_OBJECT
public:
    LogDialog(Database *db, QWidget *parent);

private:
    void reload();
    void exportCsv();
    Database *db_;
    QTableWidget *table_;
    QVector<LogRow> rows_;
};
