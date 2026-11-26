#include "core/CrashReport.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <csignal>
#include <exception>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

QString g_path;
int g_fd = -1;
std::atomic<bool> g_inside{false};
char g_header[256];

void out(const char *s, size_t n)
{
#ifdef _WIN32
    _write(2, s, unsigned(n));
    if (g_fd >= 0) _write(g_fd, s, unsigned(n));
#else
    (void)!write(2, s, n);
    if (g_fd >= 0) (void)!write(g_fd, s, n);
#endif
}
void out(const char *s) { out(s, std::strlen(s)); }

[[maybe_unused]] void outNum(long long v)
{
    char buf[32];
    int i = 31;
    buf[i] = 0;
    bool neg = v < 0;
    unsigned long long u = neg ? 0ull - (unsigned long long)v : (unsigned long long)v;
    do { buf[--i] = char('0' + u % 10); u /= 10; } while (u && i > 1);
    if (neg) buf[--i] = '-';
    out(buf + i);
}

const char *sigName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (érvénytelen memóriacím)";
    case SIGABRT: return "SIGABRT (a program megszakította magát, például hibás free())";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
#ifndef _WIN32
    case SIGBUS: return "SIGBUS";
#endif
    }
    return "?";
}

void dumpStack()
{
#ifdef _WIN32
    void *frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &mod))
            GetModuleFileNameA(mod, name, MAX_PATH);
        const char *base = std::strrchr(name, '\\');
        char line[400];
        std::snprintf(line, sizeof line, "  #%02u %s+0x%llx\n", unsigned(i), base ? base + 1 : name,
                      (unsigned long long)(static_cast<char *>(frames[i]) - reinterpret_cast<char *>(mod)));
        out(line);
    }
#else
    void *frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, 2);
    if (g_fd >= 0) backtrace_symbols_fd(frames, n, g_fd);
#endif
}

thread_local char t_activity[768] = {0};

void report(int sig)
{
    if (g_inside.exchange(true)) return;
    out("\n==== Éter összeomlott ====\n");
    out(g_header);
    out("jel: ");
    out(sigName(sig));
    out("\n");
#ifndef _WIN32
    out("szál: ");
    outNum((long long)syscall(SYS_gettid));
    out(sig == SIGABRT ? " (a hibás free() ebben a szálban történt)\n" : "\n");
#endif
    if (t_activity[0]) {
        out("ez a szál éppen: ");
        out(t_activity);
        out("\n");
    }
    out("hívási lánc:\n");
    dumpStack();
    out("==== vége ====\n");
}

void handler(int sig)
{
    report(sig);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#ifdef _WIN32
LONG WINAPI sehHandler(EXCEPTION_POINTERS *ep)
{
    // A kivétel kódja és helye (modul + eltolás): ebből a kiadott programhoz tartozó szimbólumokkal visszakereshető.
    if (ep && ep->ExceptionRecord && !g_inside.load()) {
        const EXCEPTION_RECORD *er = ep->ExceptionRecord;
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(er->ExceptionAddress), &mod))
            GetModuleFileNameA(mod, name, MAX_PATH);
        const char *base = std::strrchr(name, '\\');
        char line[512];
        std::snprintf(line, sizeof line, "\nkivétel: 0x%08lx itt: %s+0x%llx", (unsigned long)er->ExceptionCode, base ? base + 1 : name,
                      (unsigned long long)(static_cast<char *>(er->ExceptionAddress) - reinterpret_cast<char *>(mod)));
        out(line);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            std::snprintf(line, sizeof line, " (%s a 0x%llx címen)", er->ExceptionInformation[0] == 1 ? "írás" : "olvasás",
                          (unsigned long long)er->ExceptionInformation[1]);
            out(line);
        }
        std::snprintf(line, sizeof line, " · szál: %lu\n", (unsigned long)GetCurrentThreadId());
        out(line);
    }
    report(SIGSEGV);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Elkapatlan C++ kivétel: a szövege is a naplóba kerül (utána a szokásos SIGABRT-jelentés).
void onTerminate()
{
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception &ex) {
            out("\nelkapatlan kivétel: ");
            out(ex.what());
            out("\n");
        } catch (...) {
            out("\nelkapatlan kivétel (ismeretlen típus)\n");
        }
    } else {
        out("\nstd::terminate kivétel nélkül\n");
    }
    std::abort();
}

} // namespace

namespace crash {

QString logPath() { return g_path; }

void install(const QString &path)
{
    g_path = path;
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray native = QFile::encodeName(path);
    // Újbóli hívás (új napi napló): az előző fájl bezárul.
#ifdef _WIN32
    if (g_fd >= 0) _close(g_fd);
#else
    if (g_fd >= 0) close(g_fd);
#endif
    g_fd = -1;
#ifdef _WIN32
    g_fd = _open(native.constData(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, 0644);
#else
    g_fd = open(native.constData(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
#endif
    std::snprintf(g_header, sizeof g_header, "verzió: %s · indítva: %s\n", ETER_VERSION,
                  QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8().constData());
#ifndef _WIN32
    void *warm[2];
    backtrace(warm, 2);                       // a libgcc előre betöltődik (a jelkezelőben már nem foglalhat memóriát)
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &sa, nullptr);
#else
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(s, handler);
    SetUnhandledExceptionFilter(sehHandler);
#endif
    std::set_terminate(onTerminate);
}

void setActivity(const char *what, const char *detail)
{
    size_t n = 0;
    auto add = [&](const char *t) {
        for (; t && *t && n + 1 < sizeof t_activity; ++t) t_activity[n++] = *t;
    };
    add(what);
    if (detail && *detail) {
        add(": ");
        add(detail);
    }
    t_activity[n] = 0;
}

} // namespace crash
