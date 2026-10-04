#include "ParamJson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

static QString str(const QJsonObject &o, const char *k)
{
    const QJsonValue v = o.value(QLatin1String(k));
    if (v.isString()) return v.toString().trimmed();
    if (v.isDouble()) return QString::number(v.toDouble(), 'f', 0);
    return {};
}

GameInfo readGameInfo(const QString &folder)
{
    GameInfo g;
    const QString sys = folder + QStringLiteral("/sce_sys");
    g.keystoneOk = QFileInfo(sys + QStringLiteral("/keystone")).size() == 96;

    QFile f(sys + QStringLiteral("/param.json"));
    if (!f.exists()) { g.error = QStringLiteral("param.json"); return g; }
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) { g.error = f.errorString(); return g; }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) { g.error = pe.errorString(); return g; }
    const QJsonObject root = doc.object();

    g.contentId = str(root, "contentId");
    g.titleId = str(root, "titleId");
    if (g.titleId.isEmpty() && g.contentId.size() >= 16)       // UP0000-PPSA00000_00-XXXX → PPSA00000
        g.titleId = g.contentId.mid(7, 9);
    g.version = str(root, "contentVersion");
    if (g.version.isEmpty()) g.version = str(root, "masterVersion");
    g.requiredFw = formatSystemVersion(str(root, "requiredSystemSoftwareVersion"));
    g.sdk = formatSystemVersion(str(root, "sdkVersion"));
    g.region = regionFromContentId(g.contentId);
    g.platform = platformFromIds(g.titleId, g.contentId);

    const QJsonObject loc = root.value(QLatin1String("localizedParameters")).toObject();
    const QString def = loc.value(QLatin1String("defaultLanguage")).toString();
    g.title = loc.value(def).toObject().value(QLatin1String("titleName")).toString().trimmed();
    if (g.title.isEmpty()) {
        for (auto it = loc.begin(); it != loc.end() && g.title.isEmpty(); ++it)
            if (it.value().isObject())
                g.title = it.value().toObject().value(QLatin1String("titleName")).toString().trimmed();
    }
    g.found = true;
    return g;
}

QString formatSystemVersion(const QString &value)
{
    const QString v = value.trimmed();
    if (v.isEmpty()) return {};
    const QString hex = v.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? v.mid(2) : v;
    bool ok = false;
    hex.toULongLong(&ok, 16);
    if (hex.size() < 4 || !ok) return v;
    QString major = hex.left(2);
    while (major.startsWith(QLatin1Char('0'))) major.remove(0, 1);
    if (major.isEmpty()) major = QStringLiteral("0");
    return major + QLatin1Char('.') + hex.mid(2, 2);
}

int fwToNumber(const QString &fw)
{
    const int dot = fw.indexOf(QLatin1Char('.'));
    if (dot <= 0) return -1;
    bool a = false, b = false;
    const int major = fw.left(dot).toInt(&a);
    const int minor = fw.mid(dot + 1, 2).toInt(&b);
    return (a && b) ? major * 100 + minor : -1;
}

QString regionFromContentId(const QString &contentId)
{
    if (contentId.size() < 2) return {};
    const QString r = contentId.left(2).toUpper();
    return (r.at(0).isLetter() && r.at(1).isLetter()) ? r : QString();
}

QString platformFromIds(const QString &titleId, const QString &contentId)
{
    const QString code = titleId.size() >= 4 ? titleId.left(4).toUpper()
                       : contentId.size() >= 11 ? contentId.mid(7, 4).toUpper() : QString();
    return (code.startsWith(QLatin1String("CU")) || code.startsWith(QLatin1String("PC")) || code.startsWith(QLatin1String("PL")))
               ? QStringLiteral("PS4") : QStringLiteral("PS5");
}

static bool isGameRoot(const QDir &d, bool *eboot = nullptr, bool *param = nullptr)
{
    const bool e = QFileInfo::exists(d.filePath(QStringLiteral("eboot.bin")));
    const bool p = QFileInfo::exists(d.filePath(QStringLiteral("sce_sys/param.json")));
    if (eboot) *eboot = e;
    if (param) *param = p;
    return e && p;
}

GameRootCheck checkGameRoot(const QString &dir)
{
    GameRootCheck r;
    const QDir d(dir);
    r.ok = isGameRoot(d, &r.hasEboot, &r.hasParam);
    if (r.ok) return r;
    // Не корень? Ищем корень игры в подпапках (типичная ошибка — выбрать папку уровнем выше).
    const auto lvl1 = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &a : lvl1) {
        if (isGameRoot(QDir(a.absoluteFilePath()))) { r.candidates << a.absoluteFilePath(); continue; }
        const auto lvl2 = QDir(a.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QFileInfo &b : lvl2)
            if (isGameRoot(QDir(b.absoluteFilePath()))) r.candidates << b.absoluteFilePath();
        if (r.candidates.size() > 8) break;
    }
    return r;
}
