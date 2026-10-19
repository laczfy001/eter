// Tartalék playout, készenlétben: a fő playout gépre csatlakozik, folyamatosan tükrözi az adatbázisát (teljes másolat)
// és az élő állapotát (mi szól, hol tart). Ha a fő gép eltűnik, beállítástól függően magától vagy gombnyomásra
// átveszi az adást: a másolattal egy rendes Éter-főablak indul, onnan folytatva, ahol a fő gép tartott.
#pragma once
#include "net/Remote.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

struct BackupConfig {
    QString host;
    quint16 port = remote::kDefaultPort;
    QString user;
    QByteArray salt, key;               // a belépés kulcsa (a jelszó maga nincs eltárolva)
    QString pathFrom, pathTo;           // a hangfájlok mappája a fő gépen, és ugyanez ezen a gépen
    bool autoTakeover = true;
    int timeoutS = 10;
};

class BackupWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BackupWindow(const QString &replicaPath, QWidget *parent = nullptr);
    static BackupConfig loadConfig();
    static void saveConfig(const BackupConfig &c);
    // A tükrözött élő állapot beírása a másolatba (az átvételkor): a playlist sorai és átmenetei.
    static bool applyLiveState(class Database &db, const QJsonObject &state);

    void start(const QString &password = QString());
    void stop();
    const BackupConfig &config() const { return cfg_; }
    bool hasReplica() const;
    QJsonObject lastState() const { return lastState_; }
    qint64 stateAgeMs() const { return stateClock_.isValid() ? stateClock_.elapsed() : -1; }
    // A fő gépen szóló elem és helyzete most (az utolsó állapot óta eltelt idővel együtt). 0, ha nem szólt semmi.
    int playingEntry(qint64 *positionMs) const;
    QString debugText() const;
    void takeOverNow(bool ask);

signals:
    void takeoverRequested();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void onMessage(const QJsonObject &m);
    void storeSnapshot(const QJsonObject &m);
    void loadTitles();
    void refresh();
    void check();
    void openSettings();

    QString replica_;
    BackupConfig cfg_;
    remote::Client *client_;
    QTimer *retry_, *checker_;
    QElapsedTimer stateClock_, lostClock_;
    QJsonObject lastState_;
    QHash<int, QPair<QString, QString>> titles_;      // elem → cím, előadó (a másolatból)
    QString snapshotAt_;
    int snapshotItems_ = 0;
    bool everConnected_ = false, online_ = false, bye_ = false, takenOver_ = false, silentNoted_ = false;
    QString lastError_;
    QLabel *banner_, *now_, *info_;
    QTableWidget *next_;
    QPushButton *takeBtn_;
};
