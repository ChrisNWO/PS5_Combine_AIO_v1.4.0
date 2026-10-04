#pragma once
#include <QString>
#include <QStringList>

// Нативный C++-порт чтения sce_sys/param.json
// (по мотивам MetadataReader.cs из PSVIETHOA, game_metadata.py из MkPFS и Ps5ParamReader.cs из PS5 PKG Tool).
struct GameInfo {
    bool found = false;       // param.json найден и разобран
    QString title, titleId, contentId, version, error;
    QString requiredFw;       // минимальная прошивка консоли, "9.00"
    QString sdk;              // версия SDK, "9.00"
    QString region;           // код региона из Content ID: UP / EP / JP ...
    QString platform;         // "PS5" | "PS4"
    bool keystoneOk = false;  // sce_sys/keystone ровно 96 байт (нужен Sony SDK-сборке)
};

GameInfo readGameInfo(const QString &sourceFolder);

// "0x0900000000000000" -> "9.00"; "0x0761..." -> "7.61"
QString formatSystemVersion(const QString &value);
// "7.61" -> 761, неразборчиво -> -1 (для сравнения версий прошивок)
int fwToNumber(const QString &fw);
QString regionFromContentId(const QString &contentId);
QString platformFromIds(const QString &titleId, const QString &contentId);

// Проверка «корня игры»: образ PS5 должен содержать eboot.bin и sce_sys/param.json на верхнем уровне.
struct GameRootCheck {
    bool ok = false;
    bool hasEboot = false, hasParam = false;
    QStringList candidates;   // подпапки (глубина <= 2), которые являются корнем игры
};
GameRootCheck checkGameRoot(const QString &dir);
