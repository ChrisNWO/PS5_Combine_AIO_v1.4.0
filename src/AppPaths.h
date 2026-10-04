#pragma once
#include <QSettings>
#include <QString>

// Всё привязано к папке программы -> полная переносимость (можно копировать на флешку).
namespace AppPaths {
QString appDir();
QString toolsDir();        // <app>/tools
QString mkpfsExe();        // tools/mkpfs/mkpfs.exe      (PyInstaller --onedir)
QString fpkgCliExe();      // tools/fpkg-cli/fpkg-cli.exe (.NET self-contained)
QString sonySdkDir();      // tools/sony-sdk             (переменная PSVIETHOA_SONY_SDK)
QString pubCmdExe();       // tools/sony-sdk/toolchain/prospero-pub-cmd.exe
QString pubToolsDll();     // tools/fpkg-cli/libScePubTools.dll
QString amprDir();         // tools/ampr_emu/<версия>/libSceAmpr.sprx (drakmor/ampr_emu, GPL-3.0)
QString fpkg279Dir();      // tools/fpkg279 (необязательный альтернативный движок сборки PKG: SDK 2.79 plaintext,
                           // AC-пакеты, патчи с .remastered.pkg, управление PlayGo) — свой toolchain\prospero-pub-cmd.exe
QString fpkg279Script();   // tools/fpkg279/build-from-folder.ps1
bool fpkg279Available();
QString ftpUploadExe();     // tools/ftpupload/ftpupload-cli.exe (автозагрузка готового файла на PS5 по FTP)
QString fselfExe();         // tools/fself/fself-cli.exe (рекурсивная fake-подпись, BSD-3 make_fself.py)
QString backporkExe();      // tools/backpork/backpork-cli.exe (Auto-Backpork, необязательно)
QString helperExe();       // tools/ps5aio/ps5aio-helper.exe  (метаданные из образов, трофеи, файлы, ремонт exFAT…)
bool helperAvailable();
QString dataDir();         // папка для tasks.json и временных файлов (рядом с ini)
QSettings &settings();     // PS5CombineAIO.ini рядом с exe (или в AppData, если папка только для чтения)
}
