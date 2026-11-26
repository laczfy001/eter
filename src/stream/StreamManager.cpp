#include "stream/StreamManager.h"

#include "i18n.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QUuid>

using stream::Config;
using stream::Encoder;
using stream::State;

StreamManager::StreamManager(QObject *parent) : QObject(parent)
{
    QSettings s;
    limit_ = std::max(0, s.value(QStringLiteral("stream/uploadLimit"), 0).toInt());
    const QJsonArray arr = QJsonDocument::fromJson(s.value(QStringLiteral("stream/encoders")).toByteArray()).array();
    for (const QJsonValue &v : arr) {
        Config c = Config::fromJson(v.toObject());
        if (c.id.isEmpty()) c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        attach(new Encoder(c, this));
    }
    np_ = NowPlaying::loadConfig();
}

StreamManager::~StreamManager()
{
    qDeleteAll(encoders_);
    encoders_.clear();
}

void StreamManager::attach(Encoder *e)
{
    encoders_ << e;
    const QString id = e->config().id;
    connect(e, &Encoder::stateChanged, this, &StreamManager::statusChanged);
    connect(e, &Encoder::event, this, [this, id](const QString &line) { emit event(id, line); });
}

void StreamManager::save()
{
    QJsonArray arr;
    for (Encoder *e : encoders_) arr << e->config().toJson();
    QSettings().setValue(QStringLiteral("stream/encoders"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

Encoder *StreamManager::find(const QString &id) const
{
    for (Encoder *e : encoders_)
        if (e->config().id == id) return e;
    return nullptr;
}

Encoder *StreamManager::add()
{
    Config c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.name = TR("Stream %1").arg(encoders_.size() + 1);
    c.mount = encoders_.isEmpty() ? QStringLiteral("/stream") : QStringLiteral("/stream%1").arg(encoders_.size() + 1);
    // Az állomás adatai az első enkóderből (ugyanaz a rádió).
    if (!encoders_.isEmpty()) {
        const Config &f = encoders_.first()->config();
        c.server = f.server;
        c.host = f.host;
        c.port = f.port;
        c.user = f.user;
        c.tls = f.tls;
        c.stationName = f.stationName;
        c.description = f.description;
        c.genre = f.genre;
        c.url = f.url;
    }
    auto *e = new Encoder(c, this);
    attach(e);
    save();
    emit listChanged();
    return e;
}

void StreamManager::remove(const QString &id)
{
    Encoder *e = find(id);
    if (!e) return;
    encoders_.removeAll(e);
    sentTitle_.remove(id);
    delete e;
    save();
    emit listChanged();
}

void StreamManager::update(const Config &c)
{
    Encoder *e = find(c.id);
    if (!e) return;
    const bool titleChanged = e->config().sendTitle != c.sendTitle || e->config().titleLikeNowPlaying != c.titleLikeNowPlaying
                              || e->config().titleFormat != c.titleFormat;
    e->setConfig(c);
    save();
    if (titleChanged) {
        sentTitle_.remove(c.id);
        refreshTitles(false);
    }
    emit listChanged();
}

void StreamManager::startAll()
{
    for (Encoder *e : encoders_)
        if (!e->active()) e->start();
}

void StreamManager::stopAll()
{
    for (Encoder *e : encoders_)
        if (e->active() || e->state() == State::Error) e->stop();
}

void StreamManager::autoStart()
{
    for (Encoder *e : encoders_)
        if (e->config().autoStart) e->start();
}

bool StreamManager::wantsProgram() const
{
    for (Encoder *e : encoders_)
        if (!e->config().inputSource()) return true;
    return false;
}

QSet<QString> StreamManager::inputDevices() const
{
    QSet<QString> out;
    for (Encoder *e : encoders_)
        if (e->config().inputSource()) out.insert(e->config().inputDevice());
    return out;
}

void StreamManager::feedProgram(const float *stereo, int frames)
{
    for (Encoder *e : encoders_)
        if (!e->config().inputSource()) e->feed(stereo, frames);
}

void StreamManager::feedInput(const QString &device, const float *stereo, int frames)
{
    for (Encoder *e : encoders_)
        if (e->config().inputSource() && e->config().inputDevice() == device) e->feed(stereo, frames);
}

void StreamManager::setNowPlayingConfig(const NowPlaying::Config &c)
{
    np_ = c;
    refreshTitles(false);
}

bool StreamManager::wantsTitles() const
{
    for (Encoder *e : encoders_)
        if (e->config().sendTitle) return true;
    return false;
}

void StreamManager::setTracks(const NowPlaying::Track &onAir, const NowPlaying::Track &next)
{
    if (onAir == onAir_ && next == next_) return;
    onAir_ = onAir;
    next_ = next;
    if (onAir.valid()) {
        everPlayed_ = true;
        if (onAir.music) lastMusic_ = onAir;
    }
    refreshTitles(false);
}

void StreamManager::refreshTitles(bool force)
{
    for (Encoder *e : encoders_) {
        const Config &c = e->config();
        if (!c.sendTitle) continue;
        NowPlaying::Config np = np_;
        if (!c.titleLikeNowPlaying) {
            np.format = c.titleFormat;
            np.nonMusic = NowPlaying::KeepLast;
        }
        QString text;
        if (!NowPlaying::compose(np, onAir_, next_, lastMusic_, everPlayed_, &text)) continue;
        if (!force && sentTitle_.value(c.id) == text && sentTitle_.contains(c.id)) continue;
        sentTitle_.insert(c.id, text);
        e->setTitle(text);
    }
}

int StreamManager::activeCount() const
{
    int n = 0;
    for (Encoder *e : encoders_)
        if (e->active()) ++n;
    return n;
}

int StreamManager::onAirCount() const
{
    int n = 0;
    for (Encoder *e : encoders_)
        if (e->active() && e->state() == State::OnAir) ++n;
    return n;
}

bool StreamManager::anyError() const
{
    for (Encoder *e : encoders_)
        if (e->state() == State::Error) return true;
    return false;
}

int StreamManager::totalKbps() const
{
    int n = 0;
    for (Encoder *e : encoders_)
        if (e->active()) n += e->config().kbps;
    return n;
}

void StreamManager::setUploadLimitKbps(int kbps)
{
    limit_ = std::max(0, kbps);
    QSettings().setValue(QStringLiteral("stream/uploadLimit"), limit_);
    emit listChanged();
}
