#include "i18n.h"

#include <string_view>
#include <unordered_map>

struct I18nPair { const char *hu; const char *en; };
extern const I18nPair kEnglish[];
extern const int kEnglishCount;

namespace i18n {

static bool english = true;
static std::unordered_map<std::string_view, QString> &table()
{
    static std::unordered_map<std::string_view, QString> t;
    return t;
}

void setLanguage(const QString &code)
{
    english = code != QLatin1String("hu");
    auto &t = table();
    if (english && t.empty()) {
        t.reserve(static_cast<size_t>(kEnglishCount) * 2);
        for (int i = 0; i < kEnglishCount; ++i) t.emplace(kEnglish[i].hu, QString::fromUtf8(kEnglish[i].en));
    }
    QLocale::setDefault(locale());
}

QString language() { return english ? QStringLiteral("en") : QStringLiteral("hu"); }

QString text(const char *hungarian)
{
    if (english) {
        const auto &t = table();
        const auto it = t.find(std::string_view(hungarian));
        if (it != t.end()) return it->second;
    }
    return QString::fromUtf8(hungarian);
}

std::string textStd(const char *hungarian) { return text(hungarian).toStdString(); }

QLocale locale()
{
    return english ? QLocale(QLocale::English, QLocale::UnitedKingdom) : QLocale(QLocale::Hungarian, QLocale::Hungary);
}

} // namespace i18n
