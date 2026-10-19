// Hangfájlok elérhetőségének ellenőrzése háttérszálon. Egy elérhetetlen hálózati meghajtó a fájlrendszer-hívást
// akár perceken át is feltarthatja; ezért nem a felület szálán fut, és a szál sosem tartja fel a kilépést.
#pragma once
#include <QObject>
#include <QSet>
#include <QStringList>
#include <memory>

class FileCheck : public QObject {
    Q_OBJECT
public:
    explicit FileCheck(QObject *parent = nullptr);
    ~FileCheck() override;
    // Az útvonalak ellenőrzése; ha egy korábbi kérés még fut, utána ez jön (a köztes kérések összevonódnak).
    void check(const QStringList &paths);
    bool busy() const;
    static bool reachable(const QString &path);

signals:
    void progress(int done, int total);
    void finished(const QStringList &checked, const QSet<QString> &missing);

private:
    struct Shared;
    std::shared_ptr<Shared> s_;
};
