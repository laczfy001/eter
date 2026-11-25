#include "MidiControl.h"

#include "i18n.h"

#include <QJsonDocument>
#include <algorithm>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>

static MidiControl *g_instance = nullptr;

MidiControl::MidiControl(QObject *parent) : QObject(parent), in_(std::make_unique<midi::Input>())
{
    g_instance = this;
    load();
    // Ha az eszköz nincs bekapcsolva vagy kihúzták: néhány másodpercenként újra próbálja.
    retry_.setInterval(3000);
    connect(&retry_, &QTimer::timeout, this, &MidiControl::reconnect);
    reconnect();
    retry_.start();
}

MidiControl::~MidiControl()
{
    retry_.stop();
    in_->close();
    if (g_instance == this) g_instance = nullptr;
}

MidiControl *MidiControl::instance() { return g_instance; }

QVector<MidiControl::Action> MidiControl::actions()
{
    QVector<Action> a;
    a << Action{QStringLiteral("next"), TR("Következő (Szóköz)"), Button}
      << Action{QStringLiteral("stop_all"), TR("Stop: mindhárom lejátszó"), Button};
    for (int i = 0; i < 3; ++i) {
        const QString l = QString(QChar('A' + i)), k = l.toLower();
        a << Action{QStringLiteral("play_") + k, TR("%1 lejátszó: indítás / leállítás").arg(l), Button}
          << Action{QStringLiteral("fade_") + k, TR("%1 lejátszó: kiúsztatás").arg(l), Button}
          << Action{QStringLiteral("fstart_") + k, TR("%1 lejátszó: fader-indítás").arg(l), FaderStart}
          << Action{QStringLiteral("vol_") + k, TR("%1 lejátszó: hangerő").arg(l), Volume};
    }
    a << Action{QStringLiteral("vol_vt"), TR("Voice track: hangerő"), Volume}
      << Action{QStringLiteral("vol_cart"), TR("Cartwall: hangerő"), Volume}
      << Action{QStringLiteral("vol_out"), TR("Kimenet (KI): hangerő"), Volume}
      << Action{QStringLiteral("cart_stop"), TR("Cartwall: minden leállítása"), Button};
    for (int n = 1; n <= 16; ++n) a << Action{QStringLiteral("cart_%1").arg(n), TR("Cartwall: %1. gomb").arg(n), Button};
    return a;
}

MidiControl::Action MidiControl::action(const QString &id)
{
    for (const Action &a : actions())
        if (a.id == id) return a;
    return {};
}

QVector<MidiControl::PortInfo> MidiControl::ports()
{
    QVector<PortInfo> out;
    for (const midi::Port &p : midi::Input::ports()) out << PortInfo{QString::fromStdString(p.id), QString::fromStdString(p.name)};
    return out;
}

void MidiControl::load()
{
    QSettings s;
    deviceId_ = s.value(QStringLiteral("midi/device")).toString();
    deviceName_ = s.value(QStringLiteral("midi/deviceName")).toString();
    threshold_ = std::clamp(s.value(QStringLiteral("midi/faderStartThreshold"), 5).toInt(), 1, 50);
    bindings_.clear();
    const QJsonObject o = QJsonDocument::fromJson(s.value(QStringLiteral("midi/bindings")).toByteArray()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!it.value().toString().isEmpty()) bindings_.insert(it.key(), it.value().toString());
}

void MidiControl::saveBindings()
{
    QJsonObject o;
    for (auto it = bindings_.cbegin(); it != bindings_.cend(); ++it) o[it.key()] = it.value();
    QSettings().setValue(QStringLiteral("midi/bindings"), QJsonDocument(o).toJson(QJsonDocument::Compact));
    emit bindingsChanged();
}

void MidiControl::setDevice(const QString &id, const QString &name)
{
    if (id == deviceId_ && (id.isEmpty() || in_->isOpen())) return;
    in_->close();
    deviceId_ = id;
    deviceName_ = name;
    error_.clear();
    QSettings s;
    s.setValue(QStringLiteral("midi/device"), id);
    s.setValue(QStringLiteral("midi/deviceName"), name);
    faderOpen_.clear();
    lastValue_.clear();
    reconnect();
    emit statusChanged();
}

void MidiControl::reconnect()
{
    if (deviceId_.isEmpty() || in_->isOpen()) return;
    // Az eszköz azonosító szerint, ennek híján (pl. Windows alatt átszámozva) a neve szerint.
    QString id;
    for (const PortInfo &p : ports())
        if (p.id == deviceId_) id = p.id;
    if (id.isEmpty())
        for (const PortInfo &p : ports())
            if (!deviceName_.isEmpty() && p.name == deviceName_) id = p.id;
    if (id.isEmpty()) {
        const QString e = TR("nem található");
        if (error_ != e) {
            error_ = e;
            emit statusChanged();
        }
        return;
    }
    QPointer<MidiControl> self(this);
    std::string err;
    const bool ok = in_->open(
        id.toStdString(),
        [self](const midi::Message &m) {
            // A rendszer szálán érkezik: a feldolgozás a felület szálán.
            QMetaObject::invokeMethod(self.data(), [self, m] {
                if (self) self->inject(m);
            }, Qt::QueuedConnection);
        },
        [self](const std::string &e) {
            const QString text = QString::fromStdString(e);
            QMetaObject::invokeMethod(self.data(), [self, text] {
                if (!self) return;
                self->in_->close();
                self->error_ = TR("a kapcsolat megszakadt (%1)").arg(text);
                emit self->statusChanged();
            }, Qt::QueuedConnection);
        },
        &err);
    error_ = ok ? QString() : QString::fromStdString(err);
    emit statusChanged();
}

bool MidiControl::connected() const { return in_->isOpen(); }

QString MidiControl::statusText() const
{
    if (deviceId_.isEmpty()) return TR("Kikapcsolva (nincs kiválasztott MIDI-eszköz).");
    const QString name = deviceName_.isEmpty() ? deviceId_ : deviceName_;
    if (in_->isOpen()) return TR("Csatlakozva: %1").arg(name);
    return TR("Nincs kapcsolat: %1 – %2. A program néhány másodpercenként újra próbálja.").arg(name, error_.isEmpty() ? TR("nem nyitható meg") : error_);
}

void MidiControl::setBinding(const QString &actionId, const QString &controlKey)
{
    if (controlKey.isEmpty()) bindings_.remove(actionId);
    else bindings_.insert(actionId, controlKey);
    faderOpen_.remove(actionId);
    saveBindings();
}

void MidiControl::clearAll()
{
    bindings_.clear();
    faderOpen_.clear();
    saveBindings();
}

QStringList MidiControl::actionsFor(const QString &controlKey) const
{
    QStringList out;
    for (auto it = bindings_.cbegin(); it != bindings_.cend(); ++it)
        if (it.value() == controlKey) out << it.key();
    return out;
}

QString MidiControl::describe(const QString &key)
{
    if (key.isEmpty()) return QString();
    const QStringList p = key.split(QLatin1Char(':'));
    const QString ch = p.value(1);
    if (p.value(0) == QLatin1String("note")) {
        static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int n = p.value(2).toInt();
        return TR("Hang %1 (%2%3) · %4. csatorna").arg(n).arg(QLatin1String(names[n % 12])).arg(n / 12 - 1).arg(ch);
    }
    if (p.value(0) == QLatin1String("cc")) return TR("CC %1 · %2. csatorna").arg(p.value(2), ch);
    if (p.value(0) == QLatin1String("pc")) return TR("Programváltás %1 · %2. csatorna").arg(p.value(2), ch);
    if (p.value(0) == QLatin1String("pb")) return TR("Pitch bend · %1. csatorna").arg(ch);
    return key;
}

void MidiControl::startLearn(const QString &actionId)
{
    learning_ = actionId;
    emit statusChanged();
}

void MidiControl::cancelLearn()
{
    if (learning_.isEmpty()) return;
    learning_.clear();
    emit statusChanged();
}

void MidiControl::setFaderStartThreshold(int pct)
{
    threshold_ = std::clamp(pct, 1, 50);
    QSettings().setValue(QStringLiteral("midi/faderStartThreshold"), threshold_);
}

void MidiControl::inject(const midi::Message &m)
{
    // Az üzenetből a vezérlő kulcsa és értéke (0–127, a pitch bend 0–16383).
    QString key;
    int value = 0;
    bool press = false, release = false, continuous = false;
    const int ch = m.channel();
    switch (m.type()) {
    case 0x90:
        key = QStringLiteral("note:%1:%2").arg(ch).arg(m.data1);
        value = m.data2;
        press = m.data2 > 0;
        release = m.data2 == 0;
        break;
    case 0x80:
        key = QStringLiteral("note:%1:%2").arg(ch).arg(m.data1);
        release = true;
        break;
    case 0xB0:
        key = QStringLiteral("cc:%1:%2").arg(ch).arg(m.data1);
        value = m.data2;
        continuous = true;
        break;
    case 0xC0:
        key = QStringLiteral("pc:%1:%2").arg(ch).arg(m.data1);
        press = true;
        break;
    case 0xE0:
        key = QStringLiteral("pb:%1").arg(ch);
        value = (m.data2 << 7) | m.data1;
        continuous = true;
        break;
    default:
        return;     // aftertouch: nem használjuk
    }
    emit messageSeen(key, value);
    const double norm = key.startsWith(QLatin1String("pb:")) ? value / 16383.0 : value / 127.0;
    // CC-gomb: a 64 fölé lépés a lenyomás.
    const int prev = lastValue_.value(key, -1);
    lastValue_.insert(key, value);
    const bool ccEdge = m.type() == 0xB0 && value >= 64 && (prev < 64);

    if (!learning_.isEmpty()) {
        const Action a = action(learning_);
        bool accept = false;
        if (a.kind == Button) accept = press || ccEdge;
        else accept = continuous;
        if (!accept) return;
        // Ugyanaz a vezérlő egy fajta műveletből csak egyhez tartozhat (egy fader lehet hangerő és fader-indítás is).
        QStringList moved;
        for (const QString &other : actionsFor(key))
            if (other != a.id && action(other).kind == a.kind) {
                bindings_.remove(other);
                moved << other;
            }
        const QString id = learning_;
        learning_.clear();
        setBinding(id, key);
        // A fader-indítás a következő mozdulattól figyel (a mostani állás az induló állapot).
        if (a.kind == FaderStart) faderOpen_.insert(id, norm * 100.0 > threshold_ ? 1 : 0);
        emit learned(id, key, moved);
        emit statusChanged();
        return;
    }
    Q_UNUSED(release);
    for (const QString &id : actionsFor(key)) {
        const Action a = action(id);
        switch (a.kind) {
        case Button:
            if (press || ccEdge) emit buttonPressed(id);
            break;
        case Volume:
            if (continuous) emit faderMoved(id, norm);
            break;
        case FaderStart: {
            if (!continuous) break;
            const int open = norm * 100.0 > threshold_ ? 1 : 0;
            const int was = faderOpen_.value(id, -1);
            faderOpen_.insert(id, open);
            // Ismeretlen előző állapotnál (az első üzenet) nem indít: a fader már fent lehetett.
            if (was >= 0 && was != open) emit faderStart(id, open == 1);
            break;
        }
        }
    }
}
