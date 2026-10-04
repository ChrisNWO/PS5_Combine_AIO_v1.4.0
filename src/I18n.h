#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

// Словари RU/EN лежат во встроенных ресурсах (:/i18n/*.json) — внешних файлов не нужно,
// программа остаётся переносимой. Язык переключается на лету.
class I18n : public QObject
{
    Q_OBJECT
public:
    static I18n &instance();
    bool setLanguage(const QString &code);          // "ru" | "en"
    QString language() const { return lang_; }
    QString t(const QString &key) const;
    QString t(const QString &key, const QVariantList &args) const;   // подставляет %1, %2...
signals:
    void languageChanged();
private:
    I18n() = default;
    static QHash<QString, QString> load(const QString &code);
    QHash<QString, QString> cur_, fallback_;
    QString lang_;
};

#define TR(key) I18n::instance().t(QStringLiteral(key))
