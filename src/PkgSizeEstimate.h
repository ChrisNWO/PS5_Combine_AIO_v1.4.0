#pragma once
#include <QtGlobal>

// Грубая прикидка, какой уровень "большого пакета" (attributePub) и kernel.addcontMountLevel
// понадобится готовому PKG, по размеру НЕупакованных входных файлов — до запуска Publishing Tools.
// Пороги взяты из документированных ограничений Prospero SDK (встречаются в сторонних инструментах
// сборки PKG, например в профиле SDK 2.79/3.13 с плоским PFS); это не гарантия точного результата —
// реальный размер зависит от сжатия и выравнивания, которые считает сам SDK.
namespace PkgSizeEstimate {

struct Result {
    int attributePubLevel = 0;     // 0,1,2 — для SDK 2.79; 0..4 (4 = appSizeInGib) начиная с SDK 3.13
    int appSizeInGib = 0;          // только когда attributePubLevel == 4
    qint64 estimatedBytes = 0;     // unpackedBytes + разумный запас на метаданные/выравнивание
    int mountLevel = 0;            // рекомендуемый kernel.addcontMountLevel (0,1,2) — может быть ниже запрошенного
    bool fileCountExceeded = false; // lv2 требует не более ~500000 файлов в пропатченном SDK
};

// sdk313: разрешает дополнительный уровень 4 (appSizeInGib, до 320 ГиБ) сверх lv0/1/2.
Result estimate(qint64 unpackedBytes, int fileCount, bool sdk313, int requestedMountLevel = 0);

}   // namespace PkgSizeEstimate
