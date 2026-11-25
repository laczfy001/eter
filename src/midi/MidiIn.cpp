#include "MidiIn.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <unistd.h>
#endif

namespace midi {

#ifdef _WIN32

// ---- Windows (winmm) --------------------------------------------------------------------------------------------------

static std::string narrow(const wchar_t *w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? size_t(n - 1) : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

struct Input::Impl {
    HMIDIIN handle = nullptr;
    Callback cb;
    ErrorCallback onError;

    static void CALLBACK proc(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR)
    {
        auto *self = reinterpret_cast<Impl *>(inst);
        if (!self) return;
        if (msg == MIM_DATA) {
            const uint8_t status = uint8_t(p1 & 0xFF);
            if (status < 0x80 || status >= 0xF0) return;
            Message m{status, uint8_t((p1 >> 8) & 0x7F), uint8_t((p1 >> 16) & 0x7F)};
            if (self->cb) self->cb(m);
        } else if (msg == MIM_CLOSE) {
            // A meghajtó zárta (pl. kihúzták): a hívó újracsatlakozhat.
        }
    }
};

Input::Input() : d_(std::make_unique<Impl>()) {}
Input::~Input() { close(); }

std::vector<Port> Input::ports()
{
    std::vector<Port> out;
    const UINT n = midiInGetNumDevs();
    std::map<std::string, int> seen;
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        std::string name = narrow(caps.szPname);
        // Azonos nevű eszközöknél sorszám: „Név (2)”.
        const int k = ++seen[name];
        if (k > 1) name += " (" + std::to_string(k) + ")";
        out.push_back({name, name});
    }
    return out;
}

bool Input::open(const std::string &id, Callback cb, ErrorCallback onError, std::string *err)
{
    close();
    const std::vector<Port> list = ports();
    UINT index = UINT(-1);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id || list[i].name == id) index = UINT(i);
    if (index == UINT(-1)) {
        if (err) *err = "not found";
        return false;
    }
    d_->cb = std::move(cb);
    d_->onError = std::move(onError);
    MMRESULT r = midiInOpen(&d_->handle, index, DWORD_PTR(&Impl::proc), DWORD_PTR(d_.get()), CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        d_->handle = nullptr;
        if (err) {
            wchar_t text[MAXERRORLENGTH] = {};
            midiInGetErrorTextW(r, text, MAXERRORLENGTH);
            *err = narrow(text);
        }
        return false;
    }
    midiInStart(d_->handle);
    openId_ = id;
    return true;
}

void Input::close()
{
    if (d_ && d_->handle) {
        midiInStop(d_->handle);
        midiInReset(d_->handle);
        midiInClose(d_->handle);
        d_->handle = nullptr;
    }
    openId_.clear();
}

bool Input::isOpen() const { return d_ && d_->handle; }

#else

// ---- Linux (ALSA nyers MIDI: /dev/snd/midiC*D*) -----------------------------------------------------------------------

struct Input::Impl {
    int fd = -1;
    std::thread reader;
    std::atomic<bool> stop{false};
    Callback cb;
    ErrorCallback onError;
};

Input::Input() : d_(std::make_unique<Impl>()) {}
Input::~Input() { close(); }

// A hangkártyák neve a /proc/asound/cards-ból: „ 1 [Pro            ]: USB-Audio - RODECaster Pro”.
static std::map<int, std::string> cardNames()
{
    std::map<int, std::string> out;
    std::ifstream f("/proc/asound/cards");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        int num = -1;
        if (!(ss >> num)) continue;
        const size_t dash = line.find(" - ");
        if (dash == std::string::npos) continue;
        std::string name = line.substr(dash + 3);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) name.pop_back();
        out[num] = name;
    }
    return out;
}

std::vector<Port> Input::ports()
{
    std::vector<Port> out;
    const std::map<int, std::string> names = cardNames();
    if (DIR *dir = opendir("/dev/snd")) {
        std::vector<std::pair<std::pair<int, int>, std::string>> found;
        while (dirent *e = readdir(dir)) {
            int c = -1, dv = -1;
            if (std::sscanf(e->d_name, "midiC%dD%d", &c, &dv) == 2) found.push_back({{c, dv}, e->d_name});
        }
        closedir(dir);
        std::sort(found.begin(), found.end());
        for (const auto &[cd, file] : found) {
            auto it = names.find(cd.first);
            std::string name = it != names.end() ? it->second : "MIDI " + std::to_string(cd.first);
            if (cd.second > 0) name += " #" + std::to_string(cd.second + 1);
            out.push_back({"/dev/snd/" + file, name});
        }
    }
    // Fejlesztéshez: egy fájl vagy FIFO is lehet „eszköz” (ETER_MIDI_DEVICE).
    if (const char *extra = std::getenv("ETER_MIDI_DEVICE"); extra && *extra)
        out.push_back({extra, std::string("Test MIDI (") + extra + ")"});
    return out;
}

bool Input::open(const std::string &id, Callback cb, ErrorCallback onError, std::string *err)
{
    close();
    std::string path = id;
    for (const Port &p : ports())
        if (p.id == id || p.name == id) path = p.id;
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (err) *err = std::strerror(errno);
        return false;
    }
    d_->fd = fd;
    d_->cb = std::move(cb);
    d_->onError = std::move(onError);
    d_->stop = false;
    Impl *d = d_.get();
    d_->reader = std::thread([d] {
        Parser parser;
        uint8_t buf[256];
        while (!d->stop) {
            pollfd p{d->fd, POLLIN, 0};
            const int r = ::poll(&p, 1, 200);
            if (r < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (r == 0) continue;
            if (p.revents & (POLLERR | POLLNVAL)) {
                if (d->onError && !d->stop) d->onError("device error");
                break;
            }
            const ssize_t n = ::read(d->fd, buf, sizeof(buf));
            if (n > 0) {
                parser.feed(buf, size_t(n), [d](const Message &m) {
                    if (d->cb) d->cb(m);
                });
            } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                // Fájl vége (FIFO: az író bezárta) vagy eltűnt eszköz.
                if (p.revents & POLLHUP || n == 0) {
                    if (d->onError && !d->stop) d->onError("device closed");
                    break;
                }
                if (d->onError && !d->stop) d->onError(std::strerror(errno));
                break;
            }
        }
    });
    openId_ = id;
    return true;
}

void Input::close()
{
    if (!d_) return;
    d_->stop = true;
    if (d_->reader.joinable()) d_->reader.join();
    if (d_->fd >= 0) ::close(d_->fd);
    d_->fd = -1;
    openId_.clear();
}

bool Input::isOpen() const { return d_ && d_->fd >= 0; }

#endif

} // namespace midi
