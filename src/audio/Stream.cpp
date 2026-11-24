#include "Stream.h"

#include <cctype>
#include <mutex>

namespace eter {

static std::mutex gMtx;
static StreamFactory gFactory;

bool isStreamUrl(const std::string &p)
{
    auto starts = [&](const char *s) {
        const size_t n = std::char_traits<char>::length(s);
        if (p.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(p[i])) != s[i]) return false;
        return true;
    };
    return starts("http://") || starts("https://");
}

void setStreamFactory(StreamFactory f)
{
    std::lock_guard<std::mutex> lk(gMtx);
    gFactory = std::move(f);
}

std::shared_ptr<StreamInput> openStreamInput(const std::string &url)
{
    StreamFactory f;
    {
        std::lock_guard<std::mutex> lk(gMtx);
        f = gFactory;
    }
    return f ? f(url) : nullptr;
}

} // namespace eter
