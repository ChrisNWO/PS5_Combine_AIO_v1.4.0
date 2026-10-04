#include "GameCheck.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "AppPaths.h"

namespace GameCheck {

const QList<AmprBuild> &amprBuilds()
{
    static const QList<AmprBuild> list = {
        {QStringLiteral("0.3.6.6"), QStringLiteral("362c84e31e3a06a42f50fe4d6ea124d15e73f77c901470f90823935d7f36ca76"), QStringLiteral("ampr.label.old")},
        {QStringLiteral("0.4.2.1"), QStringLiteral("69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a"), QStringLiteral("ampr.label.new")},
    };
    return list;
}

static QString bundledPath(const AmprBuild &b)
{
    return AppPaths::amprDir() + QLatin1Char('/') + b.version + QStringLiteral("/libSceAmpr.sprx");
}

QList<AmprBuild> availableAmprBuilds()
{
    QList<AmprBuild> out;
    for (const AmprBuild &b : amprBuilds())
        if (QFileInfo::exists(bundledPath(b))) out << b;
    return out;
}

static QString sha256Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

// Строка импорта лежит в таблице строк динамической секции — сразу за заголовками ELF, поэтому достаточно начала файла.
static bool fileContains(const QString &path, const QByteArray &needle, qint64 limit)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    constexpr qint64 kChunk = 1 << 20;
    const int overlap = int(needle.size()) - 1;
    QByteArray carry;
    qint64 scanned = 0;
    while (scanned < limit && !f.atEnd()) {
        const QByteArray chunk = f.read(kChunk);
        if (chunk.isEmpty()) break;
        const QByteArray window = carry + chunk;
        if (window.contains(needle)) return true;
        scanned += chunk.size();
        carry = window.right(overlap);
    }
    return false;
}

AmprStatus inspectAmpr(const QString &gameRoot)
{
    AmprStatus st;
    const QString module = gameRoot + QStringLiteral("/fakelib/libSceAmpr.sprx");
    st.modulePresent = QFileInfo::exists(module);
    st.indexPresent = QFileInfo::exists(gameRoot + QStringLiteral("/ampr_emu.index"));
    if (st.modulePresent) {
        const QString sha = sha256Of(module);
        st.presentVersion = QStringLiteral("?");
        for (const AmprBuild &b : amprBuilds())
            if (b.sha256 == sha) st.presentVersion = b.version;
    }
    const QDir fl(gameRoot + QStringLiteral("/fakelib"));
    for (const QFileInfo &fi : fl.entryInfoList({QStringLiteral("*.sprx")}, QDir::Files, QDir::Name)) st.fakelib << fi.completeBaseName();
    const QString eboot = gameRoot + QStringLiteral("/eboot.bin");
    st.ebootFound = QFileInfo::exists(eboot);
    if (st.ebootFound) {
        st.ebootSize = QFileInfo(eboot).size();
        QFile f(eboot);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray head = f.read(4);
            st.ebootKind = head == QByteArray("\x4F\x15\x3D\x1D", 4) ? QStringLiteral("SELF")
                           : head == QByteArray("\x7F" "ELF", 4) ? QStringLiteral("ELF") : QStringLiteral("?");
        }
        st.importsAmpr = fileContains(eboot, QByteArrayLiteral("libSceAmpr"), 64LL * 1024 * 1024);
    }
    return st;
}

bool restoreAmpr(const QString &gameRoot, const AmprBuild &build, QString *error)
{
    const QString src = bundledPath(build);
    if (!QFileInfo::exists(src)) { if (error) *error = QStringLiteral("not found: ") + QDir::toNativeSeparators(src); return false; }
    if (sha256Of(src) != build.sha256) {                              // файл подменён или повреждён — не копируем
        if (error) *error = QStringLiteral("SHA-256 mismatch: ") + QDir::toNativeSeparators(src);
        return false;
    }
    const QString dir = gameRoot + QStringLiteral("/fakelib");
    if (!QDir().mkpath(dir)) { if (error) *error = QStringLiteral("cannot create ") + QDir::toNativeSeparators(dir); return false; }
    const QString dst = dir + QStringLiteral("/libSceAmpr.sprx");
    if (QFileInfo::exists(dst)) QFile::remove(dst);
    if (!QFile::copy(src, dst)) { if (error) *error = QStringLiteral("cannot copy to ") + QDir::toNativeSeparators(dst); return false; }
    QFile::remove(gameRoot + QStringLiteral("/ampr_emu.index"));       // индекс привязан к набору файлов — MkPFS создаст заново
    return true;
}

// Список выверен по справочному инструменту сборки PKG (SDK 2.79 plaintext-профиль, create-gp5-from-folder.py) —
// это файлы, которые Publishing Tools генерирует сама при упаковке и которые публикатор исключает из входных данных.
const QStringList &packageOnlyNames()
{
    static const QStringList names = {
        QStringLiteral("license.dat"), QStringLiteral("license.info"), QStringLiteral("playgo-chunk.dat"), QStringLiteral("playgo-chunk.crc"),
        QStringLiteral("playgo-hash-table.dat"), QStringLiteral("playgo-ficm.dat"), QStringLiteral("playgo-scenario.json"),
        QStringLiteral("playgo-manifest.xml"), QStringLiteral("origin-param.json"), QStringLiteral("target-param.json"),
        QStringLiteral("origin-deltainfo.dat"), QStringLiteral("target-deltainfo.dat"), QStringLiteral("origin-relocinfo.dat"),
        QStringLiteral("target-relocinfo.dat"), QStringLiteral("pfs-region-hints.json"), QStringLiteral("imagedigs.dat"),
        QStringLiteral("pfsimage.xml"), QStringLiteral("param_cp_values.json"), QStringLiteral("pfs-version.dat"),
        QStringLiteral("disc_info.dat"), QStringLiteral("ext_info.dat"), QStringLiteral("lem_notification.dat"),
        QStringLiteral("param.sfo"), QStringLiteral("pronunciation.sig")};   // keystone НЕ включён: он нужен игре при упаковке в PFS/exFAT, в отличие от SCE-пакетов
    return names;
}

const QStringList &packageOnlyDirs()
{
    static const QStringList dirs = {QStringLiteral("sce_sys/about"), QStringLiteral("sce_suppl"), QStringLiteral("sce_sc"), QStringLiteral("playgo-languages")};
    return dirs;
}

const QStringList &packageOnlyFakeLibraries()
{
    static const QStringList libs = {QStringLiteral("fakelib/libSceAmpr.sprx"), QStringLiteral("fakelib/libScePlayGo.sprx"), QStringLiteral("ampr_emu.index")};
    return libs;
}

QStringList findPackageOnly(const QString &gameRoot)
{
    QStringList found;
    for (const QString &n : packageOnlyNames())
        if (QFileInfo::exists(gameRoot + QStringLiteral("/sce_sys/") + n)) found << QStringLiteral("sce_sys/") + n;
    for (const QString &d : packageOnlyDirs())
        if (QFileInfo(gameRoot + QLatin1Char('/') + d).isDir()) found << d;
    return found;
}

bool isPackageOnlyPath(const QString &relativePath)
{
    const QString p = relativePath.toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!p.startsWith(QLatin1String("sce_sys/"))) return false;
    return packageOnlyNames().contains(p.mid(8), Qt::CaseInsensitive);
}

bool movePackageOnly(const QString &gameRoot, const QStringList &relative, QString *backupDir, QString *error)
{
    const QFileInfo root(gameRoot);
    const QString backup = root.absoluteDir().absoluteFilePath(root.fileName() + QStringLiteral("_package_only"));
    if (backupDir) *backupDir = backup;
    for (const QString &rel : relative) {
        const QString src = gameRoot + QLatin1Char('/') + rel;
        const QString dst = backup + QLatin1Char('/') + rel;
        const QFileInfo srcInfo(src);
        if (!srcInfo.exists()) continue;         // могло уже уйти вместе с родительской папкой из этого же списка
        if (!QDir().mkpath(QFileInfo(dst).absolutePath())) { if (error) *error = QStringLiteral("cannot create ") + QDir::toNativeSeparators(backup); return false; }
        if (srcInfo.isDir()) {
            if (QFileInfo::exists(dst)) QDir(dst).removeRecursively();
            if (!QDir().rename(src, dst)) { if (error) *error = QStringLiteral("cannot move ") + QDir::toNativeSeparators(src); return false; }
        } else {
            if (QFileInfo::exists(dst)) QFile::remove(dst);
            if (!QFile::rename(src, dst)) { if (error) *error = QStringLiteral("cannot move ") + QDir::toNativeSeparators(src); return false; }
        }
    }
    return true;
}

bool hasDecryptedFolder(const QString &gameRoot)
{
    return QFileInfo(gameRoot + QStringLiteral("/decrypted")).isDir();
}

bool moveDecryptedFolder(const QString &gameRoot, QString *newPath, QString *error)
{
    const QFileInfo root(gameRoot);
    const QString dst = root.absoluteDir().absoluteFilePath(root.fileName() + QStringLiteral("_decrypted"));
    if (newPath) *newPath = dst;
    if (QFileInfo::exists(dst)) { if (error) *error = QStringLiteral("already exists: ") + QDir::toNativeSeparators(dst); return false; }
    if (!QDir().rename(gameRoot + QStringLiteral("/decrypted"), dst)) { if (error) *error = QStringLiteral("cannot move ") + QDir::toNativeSeparators(gameRoot + QStringLiteral("/decrypted")); return false; }
    return true;
}

}   // namespace GameCheck
