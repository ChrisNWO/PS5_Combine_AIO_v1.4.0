#include "FtpUpload.h"

#include <QFileInfo>

#include "AppPaths.h"
#include "I18n.h"

namespace FtpUpload {

bool isReady()
{
    return AppPaths::settings().value(QStringLiteral("ftp/enable"), true).toBool()
           && !AppPaths::settings().value(QStringLiteral("ftp/host")).toString().trimmed().isEmpty()
           && QFileInfo::exists(AppPaths::ftpUploadExe());
}

QList<JobSpec> makeJob(const QString &localFile)
{
    if (!isReady() || !QFileInfo::exists(localFile)) return {};

    auto &s = AppPaths::settings();
    const QString host = s.value(QStringLiteral("ftp/host")).toString().trimmed();
    const QString port = s.value(QStringLiteral("ftp/port"), QStringLiteral("1337")).toString().trimmed();
    const QString path = s.value(QStringLiteral("ftp/path"), QStringLiteral("/data/homebrew")).toString().trimmed();
    const bool anon = s.value(QStringLiteral("ftp/anon"), true).toBool();

    QStringList a{QStringLiteral("--host"), host, QStringLiteral("--port"), port.isEmpty() ? QStringLiteral("21") : port,
                  QStringLiteral("--remote-dir"), path.isEmpty() ? QStringLiteral("/data/homebrew") : path,
                  QStringLiteral("--file"), localFile};
    if (!anon) {
        a << QStringLiteral("--user") << s.value(QStringLiteral("ftp/user"), QStringLiteral("anonymous")).toString();
        a << QStringLiteral("--password") << s.value(QStringLiteral("ftp/pass")).toString();
    }

    const QString title = I18n::instance().t(QStringLiteral("ftp.job"), {QFileInfo(localFile).fileName(), host});
    return {JobSpec{title, AppPaths::ftpUploadExe(), a, QString(), 1}};
}

}   // namespace FtpUpload
