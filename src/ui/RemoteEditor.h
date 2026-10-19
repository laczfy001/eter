// Szerkesztő mód: egy másik gépen futó playout már legenerált playlistjének szerkesztése a hálózaton át
// (sorrend, törlés, ráfektetés, Mix editor). Minden módosítás azonnal a playouthoz kerül, és ott hajtódik végre;
// a szerkesztő a playout adatainak helyi tükrén dolgozik, amelyet a playout folyamatosan frissít.
#pragma once
#include "db/Database.h"
#include "net/Remote.h"

#include <QDialog>
#include <QHash>
#include <functional>
#include <memory>
#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class MixEditorPage;
class LibraryPanel;
class QFile;
class PlaylistTable;
namespace eter { class Engine; }

struct RemoteTarget {
    QString host;
    quint16 port = remote::kDefaultPort;
    QString user, password;              // üres felhasználó: az alap jelszó
    QString pathFrom, pathTo;          // a hangfájlok mappája a playout gépen, és ugyanez ezen a gépen
};

class RemoteConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit RemoteConnectDialog(QWidget *parent = nullptr);
    RemoteTarget target() const;

private:
    QLineEdit *host_, *user_, *password_, *from_, *to_;
    QSpinBox *port_;
};

class RemoteEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    RemoteEditorWindow(Database *mirror, eter::Engine *engine, QWidget *parent = nullptr);
    void connectTo(const RemoteTarget &t);
    MixEditorPage *mixPage() const { return mix_; }
    PlaylistTable *table() const { return table_; }
    remote::Client *client() const { return client_; }
    // A sorokon végezhető műveletek (a helyi menü és a teszt is ezeket hívja).
    void requestMove(int entryId, int beforeEntryId);
    void requestRemove(int entryId);
    void requestOverlay(int entryId, bool on);
    void requestInsert(const QVector<int> &itemIds, int beforeEntryId);
    // Hangfájl feltöltése a playout hangtárába (vt: voice trackként). A done az új elem azonosítójával hívódik (0: hiba).
    void uploadFile(const QString &localPath, int categoryId, bool vt, const QString &title, std::function<void(int)> done);
    LibraryPanel *library() const { return library_; }
    QString debugState() const;
    bool busyUploading() const { return !uploads_.isEmpty(); }

signals:
    void stateApplied();                               // új állapot érkezett és megjelent
    void opAnswered(int id, bool ok, const QString &error);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    struct Row {
        Entry entry;
        QString state;                                 // pending, cued, playing, played, skipped, failed
        int player = -1;
        QString start;
        bool missing = false;                          // a fájl a playout gépen nem érhető el
        int voice = -1;                                // szóló ráfektetett elem csatornája
    };
    void onMessage(const QJsonObject &m);
    void applyLibrary(const QJsonObject &m);
    void applyState(const QJsonObject &m);
    void rebuildTable();
    void rebuildPairs();
    bool overlayActive(int index) const;
    bool live(int index) const;
    QString mapPath(const QString &path) const;
public:
    static QString localFolder(const QString &input, QString *problem = nullptr);
    static QString mapWith(const QString &path, const QString &pathFrom, const QString &localRoot);
    QString warningText() const;
private:
    void setOnline(bool on, const QString &text);
    void contextMenu(const QPoint &pos);
    void openConnectDialog();
    int send(const QString &op, const QJsonObject &args, std::function<void(const QJsonObject &ack)> onAck = {});
    void applyPerms();
    int chooseUploadCategory();                        // 0: mégse
    void uploadPaths(const QStringList &paths, int beforeEntryId);
    void nextUpload();
    void previewLibraryItem();

    Database *db_;
    eter::Engine *engine_;
    remote::Client *client_;
    RemoteTarget target_;
    QJsonObject lastState_;
    bool haveLibrary_ = false;
    QVector<Row> rows_;
    QHash<int, QString> pendingOps_;                   // elküldött, még meg nem válaszolt kérések
    QHash<int, std::function<void(const QJsonObject &)>> callbacks_;
    struct UploadJob {
        QString local, title;
        int categoryId = 0;
        bool vt = false;
        std::function<void(int)> done;
        std::shared_ptr<QFile> file;
        qint64 size = 0, sent = 0;
        int token = 0;
    };
    QVector<UploadJob> uploads_;                       // az első fut, a többi vár
    QHash<int, QString> uploadedLocal_;                // feltöltött elem → a helyi fájl (előhallgatáshoz)
    LibraryPanel *library_ = nullptr;
    bool libPreview_ = false;

    PlaylistTable *table_;
    MixEditorPage *mix_;
    QLabel *banner_, *status_, *nowLabel_, *warning_ = nullptr;
    QString localRoot_, pathProblem_;
    QPushButton *reconnect_;
    QTimer *retry_;
    bool everConnected_ = false;
    QString station_;
};
