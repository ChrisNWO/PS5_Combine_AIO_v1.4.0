#include "PkgSizeEstimate.h"

#include <algorithm>

namespace PkgSizeEstimate {

namespace {
constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024LL * kMiB;
constexpr qint64 kStandardLimit = 162'514'599'936LL;
constexpr qint64 kLargeLv1Limit = 195'878'191'104LL;
constexpr qint64 kLargeLv2Limit = 260'436'918'272LL;
constexpr qint64 kAddcontLv2Limit = 97'945'387'008LL;
constexpr int kLargeLv2FileLimit = 500'000;
}   // namespace

Result estimate(qint64 unpackedBytes, int fileCount, bool sdk313, int requestedMountLevel)
{
    Result r;
    if (unpackedBytes < 0) unpackedBytes = 0;
    if (fileCount < 0) fileCount = 0;
    requestedMountLevel = std::clamp(requestedMountLevel, 0, 2);

    const qint64 allowance = std::max<qint64>(512 * kMiB, (unpackedBytes + 99) / 100) + qint64(fileCount) * 4096;
    const qint64 estimated = unpackedBytes + allowance;
    r.estimatedBytes = estimated;

    int mount = requestedMountLevel;
    if (mount == 2 && estimated > kAddcontLv2Limit) mount = 1;      // lv2 ограничен отдельным, меньшим пределом
    if (mount == 1 && estimated > kLargeLv1Limit) mount = 0;
    r.mountLevel = mount;

    if (mount == 2) { r.attributePubLevel = 0; return r; }          // addcontMountLevel=2 сам поднимает предел
    if (estimated <= kStandardLimit) { r.attributePubLevel = 0; return r; }
    if (estimated <= kLargeLv1Limit) { r.attributePubLevel = 1; return r; }
    if (estimated <= kLargeLv2Limit) {
        r.attributePubLevel = 2;
        r.fileCountExceeded = fileCount > kLargeLv2FileLimit;
        return r;
    }
    if (!sdk313) {                                                  // SDK 2.79: дальше уровней нет, lv2 — потолок
        r.attributePubLevel = 2;
        r.fileCountExceeded = fileCount > kLargeLv2FileLimit;
        return r;
    }
    r.attributePubLevel = 4;
    r.appSizeInGib = std::clamp<int>(int((estimated + kGiB - 1) / kGiB), 256, 320);
    return r;
}

}   // namespace PkgSizeEstimate
