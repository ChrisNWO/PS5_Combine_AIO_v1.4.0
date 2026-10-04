#pragma once
// Превращает «сырые» ошибки утилит в понятные подсказки на языке интерфейса.
#include <QString>

#include "I18n.h"

namespace ErrorHints {

inline QString find(const QString &text)
{
    struct Rule { const char *needle; const char *key; };
    static const Rule rules[] = {
        {"invalid CNT magic",        "hint.retail"},     // fpkg-cli verify/extract на retail-пакете
        {"outer PFS must use mode",  "hint.retail"},
        {"keystone",                 "hint.keystone"},
        {"param.json",               "hint.paramJson"},
        {"being used by another process", "hint.locked"},
        {"Access to the path",       "hint.access"},
        {"denied",                   "hint.access"},
        {"No space left",            "hint.disk"},
        {"not enough space",         "hint.disk"},
        {"There is not enough space","hint.disk"},
    };
    for (const Rule &r : rules)
        if (text.contains(QLatin1String(r.needle), Qt::CaseInsensitive))
            return I18n::instance().t(QString::fromLatin1(r.key));
    return {};
}

} // namespace ErrorHints
