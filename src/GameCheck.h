#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Проверки папки игры перед упаковкой в образ, которые часто объясняют «образ собрался, а игра падает при старте»:
//  1) AMPR: если eboot.bin использует libSceAmpr, а в папке нет fakelib/libSceAmpr.sprx (при сборке FPKG он вырезается),
//     на загрузчике, запускающем образ как папку, игре нужен эмулятор drakmor/ampr_emu (GPL-3.0); MkPFS сам создаёт к нему
//     ampr_emu.index, когда модуль лежит в папке.
//  2) служебные файлы пакета в sce_sys (license.dat, playgo-*, pfsimage.xml…): fpkg-cli подмешивает их при распаковке FPKG,
//     в файловую систему игры они не входят и в образе быть не должны.
namespace GameCheck {

struct AmprBuild {
    QString version, sha256, labelKey;
};

struct AmprStatus {
    bool ebootFound = false, importsAmpr = false, modulePresent = false, indexPresent = false;
    QString presentVersion;                       // версия по SHA-256, "?" — неизвестная сборка
    qint64 ebootSize = 0;
    QString ebootKind;                            // "SELF" / "ELF" / "?" — по первым байтам eboot.bin
    QStringList fakelib;                          // имена модулей в fakelib (без .sprx)
    bool needsModule() const { return importsAmpr && !modulePresent; }
};

const QList<AmprBuild> &amprBuilds();             // известные сборки (SHA-256 закреплены)
QList<AmprBuild> availableAmprBuilds();           // те из них, чей файл лежит в tools/ampr_emu/<версия>/
AmprStatus inspectAmpr(const QString &gameRoot);  // читает начало eboot.bin (до 64 МБ), без полного чтения
bool restoreAmpr(const QString &gameRoot, const AmprBuild &build, QString *error);

const QStringList &packageOnlyNames();            // имена файлов в sce_sys
const QStringList &packageOnlyDirs();              // каталоги в sce_sys/ и в корне игры, которые генерирует паблишинг (about, sce_suppl, sce_sc, playgo-languages)
const QStringList &packageOnlyFakeLibraries();      // fakelib-модули, которые упаковщики исключают из PKG (libSceAmpr.sprx, libScePlayGo.sprx)
QStringList findPackageOnly(const QString &gameRoot);                   // "sce_sys/имя" — только существующие
bool isPackageOnlyPath(const QString &relativePath);                    // для отчётов сверки
// Не удаляет, а переносит в «<папка>_package_only» рядом с игрой (можно вернуть обратно).
bool movePackageOnly(const QString &gameRoot, const QStringList &relative, QString *backupDir, QString *error);

// Dump Runner после подписи переносит исходные расшифрованные бинарники в «decrypted» внутри дампа: в образе они не нужны
// и сильно его раздувают. Переносим не внутрь, а рядом с игрой: «<папка>_decrypted» (можно вернуть).
bool hasDecryptedFolder(const QString &gameRoot);
bool moveDecryptedFolder(const QString &gameRoot, QString *newPath, QString *error);

}   // namespace GameCheck
