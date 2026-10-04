#include "I18n.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

I18n &I18n::instance()
{
    static I18n inst;
    return inst;
}

QHash<QString, QString> I18n::load(const QString &code)
{
    QHash<QString, QString> out;
    QFile f(QStringLiteral(":/i18n/%1.json").arg(code));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

bool I18n::setLanguage(const QString &code)
{
    const QString c = (code == QLatin1String("ru")) ? QStringLiteral("ru") : QStringLiteral("en");
    if (fallback_.isEmpty())
        fallback_ = load(QStringLiteral("en"));
    cur_ = load(c);
    const bool changed = (c != lang_);
    lang_ = c;
    if (changed)
        emit languageChanged();
    return !cur_.isEmpty();
}

QString I18n::t(const QString &key) const
{
    auto it = cur_.constFind(key);
    if (it != cur_.constEnd()) return *it;
    it = fallback_.constFind(key);
    return it != fallback_.constEnd() ? *it : key;   // видимый ключ = сразу заметно, что перевода нет
}

QString I18n::t(const QString &key, const QVariantList &args) const
{
    QString s = t(key);
    for (const QVariant &a : args)
        s = s.arg(a.toString());
    return s;
}
