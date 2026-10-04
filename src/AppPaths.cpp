#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace AppPaths {

QString appDir() { return QCoreApplication::applicationDirPath(); }
QString toolsDir() { return appDir() + QStringLiteral("/tools"); }

static QString exe(const QString &n)
{
#ifdef Q_OS_WIN
    return n + QStringLiteral(".exe");
#else
    return n;
#endif
}

QString mkpfsExe() { return toolsDir() + QStringLiteral("/mkpfs/") + exe(QStringLiteral("mkpfs")); }
QString fpkgCliExe() { return toolsDir() + QStringLiteral("/fpkg-cli/") + exe(QStringLiteral("fpkg-cli")); }
QString sonySdkDir() { return toolsDir() + QStringLiteral("/sony-sdk"); }
QString pubCmdExe() { return sonySdkDir() + QStringLiteral("/toolchain/prospero-pub-cmd.exe"); }
QString amprDir() { return toolsDir() + QStringLiteral("/ampr_emu"); }
QString fpkg279Dir() { return toolsDir() + QStringLiteral("/fpkg279"); }
QString fpkg279Script() { return fpkg279Dir() + QStringLiteral("/build-from-folder.ps1"); }
bool fpkg279Available() { return QFileInfo::exists(fpkg279Script()) && QFileInfo::exists(fpkg279Dir() + QStringLiteral("/toolchain/prospero-pub-cmd.exe")); }

QString ftpUploadExe() { return toolsDir() + QStringLiteral("/ftpupload/") + exe(QStringLiteral("ftpupload-cli")); }
QString fselfExe() { return toolsDir() + QStringLiteral("/fself/") + exe(QStringLiteral("fself-cli")); }
QString backporkExe() { return toolsDir() + QStringLiteral("/backpork/") + exe(QStringLiteral("backpork-cli")); }
QString helperExe() { return toolsDir() + QStringLiteral("/ps5aio/") + exe(QStringLiteral("ps5aio-helper")); }
bool helperAvailable() { return QFileInfo::exists(helperExe()); }

QString dataDir()
{
    return QFileInfo(settings().fileName()).absolutePath();
}

QString pubToolsDll() { return toolsDir() + QStringLiteral("/fpkg-cli/libScePubTools.dll"); }

QSettings &settings()
{
    static QSettings *s = []() -> QSettings * {
        QString file = appDir() + QStringLiteral("/PS5CombineAIO.ini");
        if (!QFileInfo(appDir()).isWritable()) {
            const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(d);
            file = d + QStringLiteral("/PS5CombineAIO.ini");
        }
        return new QSettings(file, QSettings::IniFormat);
    }();
    return *s;
}

} // namespace AppPaths
