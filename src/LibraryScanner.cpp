#include "LibraryScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include "ParamJson.h"

void LibraryScanner::run()
{
    for (const QString &r : std::as_const(roots_)) {
        if (cancel_.loadRelaxed()) break;
        scanDir(QDir::cleanPath(r), 0);
    }
}

void LibraryScanner::scanDir(const QString &dir, int depth)
{
    if (cancel_.loadRelaxed() || depth > 8) return;
    const QDir d(dir);

    // Папка с sce_sys/param.json — это дамп игры; внутрь не заходим.
    if (QFileInfo::exists(d.filePath(QStringLiteral("sce_sys/param.json")))) {
        addDump(dir);
        return;
    }
    emit scanning(dir);

    static const QStringList kExt = {QStringLiteral("pkg"), QStringLiteral("ffpfsc"), QStringLiteral("ffpkg"),
                                     QStringLiteral("exfat"), QStringLiteral("ffpfs")};
    const auto files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : files) {
        if (cancel_.loadRelaxed()) return;
        if (kExt.contains(fi.suffix().toLower())) addFile(fi);
    }
    const auto dirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &sub : dirs)
        scanDir(sub.absoluteFilePath(), depth + 1);
}

qint64 LibraryScanner::dirSize(const QString &dir)
{
    qint64 total = 0;
    int n = 0;
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        if ((++n & 255) == 0 && cancel_.loadRelaxed()) break;
    }
    return total;
}

void LibraryScanner::addDump(const QString &dir)
{
    const GameInfo g = readGameInfo(dir);
    LibraryItem it;
    it.isDump = true;
    it.source = QStringLiteral("dump");
    it.path = QDir::toNativeSeparators(dir);
    it.fileName = QFileInfo(dir).fileName();
    it.title = g.title.isEmpty() ? it.fileName : g.title;
    it.titleId = g.titleId;
    it.contentId = g.contentId;
    it.platform = g.platform;
    it.region = g.region;
    it.version = g.version;
    it.requiredFw = g.requiredFw;
    it.sdk = g.sdk;
    it.role = QStringLiteral("base");
    it.size = dirSize(dir);
    emit itemFound(it);
}

void LibraryScanner::addFile(const QFileInfo &fi)
{
    // Внутрь образов не заглядываем (это дорого) — идентификаторы берём из имени файла, как у Sony-пакетов.
    static const QRegularExpression reContent(QStringLiteral(R"([A-Z]{2}\d{4}-[A-Z]{4}\d{5}_\d{2}-[A-Z0-9]{16})"));
    static const QRegularExpression reTitle(QStringLiteral(R"((?:PPSA|CUSA)\d{5})"));
    LibraryItem it;
    const QString sfx = fi.suffix().toLower();
    it.source = (sfx == QLatin1String("ffpfs")) ? QStringLiteral("pfs") : sfx;
    it.path = QDir::toNativeSeparators(fi.absoluteFilePath());
    it.fileName = fi.fileName();
    it.title = fi.completeBaseName();
    it.size = fi.size();
    it.contentId = reContent.match(fi.fileName()).captured(0);
    it.titleId = reTitle.match(fi.fileName()).captured(0);
    if (it.titleId.isEmpty() && it.contentId.size() >= 16) it.titleId = it.contentId.mid(7, 9);
    it.region = regionFromContentId(it.contentId);
    it.platform = platformFromIds(it.titleId, it.contentId);
    it.role = QStringLiteral("unknown");
    emit itemFound(it);
}
