// MIDI-vezérlés (Beállítások → MIDI): a kiválasztott MIDI-eszköz (keverőpult, pl. RØDECaster Pro, vagy kontroller)
// gombjai és faderei a program műveleteihez rendelhetők. Tanuló mód: a művelet kiválasztása után az első megmozdított
// gomb vagy fader kerül hozzá. Gombbal: indítás, leállítás, következő, cartwall; faderrel: hangerő vagy fader-indítás
// (a fader felhúzására indul, lehúzására megáll a lejátszó). A hozzárendelések és az eszköz gépenként tárolódnak.
#pragma once

#include "midi/MidiIn.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <memory>

class MidiControl : public QObject {
    Q_OBJECT
public:
    enum Kind { Button, Volume, FaderStart };
    struct Action {
        QString id;         // "next", "play_a", "vol_out", "fstart_b", "cart_3", …
        QString label;      // a felületen (lefordítva)
        Kind kind;
    };
    struct PortInfo {
        QString id, name;
    };

    explicit MidiControl(QObject *parent = nullptr);
    ~MidiControl() override;
    static MidiControl *instance();

    static QVector<Action> actions();
    static Action action(const QString &id);
    static QVector<PortInfo> ports();

    QString deviceId() const { return deviceId_; }
    QString deviceName() const { return deviceName_; }
    void setDevice(const QString &id, const QString &name);     // üres: kikapcsolva
    bool connected() const;
    QString statusText() const;

    QString binding(const QString &actionId) const { return bindings_.value(actionId); }
    void setBinding(const QString &actionId, const QString &controlKey);   // üres: törlés
    void clearAll();
    QStringList actionsFor(const QString &controlKey) const;
    // A vezérlő leírása: „CC 7 · 1. csatorna”, „Hang 60 (C4) · 10. csatorna”, „Pitch bend · 2. csatorna”.
    static QString describe(const QString &controlKey);

    void startLearn(const QString &actionId);
    void cancelLearn();
    QString learning() const { return learning_; }

    int faderStartThreshold() const { return threshold_; }      // százalék (ennél nyitottabb fader = nyitva)
    void setFaderStartThreshold(int pct);

    void inject(const midi::Message &m);       // a beérkező üzenet feldolgozása (a felület szálán)

signals:
    void statusChanged();
    void bindingsChanged();
    void messageSeen(const QString &controlKey, int value);      // minden beérkező üzenet (a figyelőhöz)
    void learned(const QString &actionId, const QString &controlKey, const QStringList &movedFrom);
    void buttonPressed(const QString &actionId);
    void faderMoved(const QString &actionId, double value);       // 0–1
    void faderStart(const QString &actionId, bool open);

private:
    void reconnect();
    void load();
    void saveBindings();

    std::unique_ptr<midi::Input> in_;
    QString deviceId_, deviceName_, error_;
    QHash<QString, QString> bindings_;          // művelet → vezérlő
    QHash<QString, int> lastValue_;             // vezérlő → utolsó érték (a CC-gombok éléhez)
    QHash<QString, int> faderOpen_;             // művelet → −1 ismeretlen, 0 zárt, 1 nyitott
    QString learning_;
    int threshold_ = 5;
    QTimer retry_;
};
