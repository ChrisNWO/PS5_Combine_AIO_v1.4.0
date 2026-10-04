#!/usr/bin/env bash
# Сборка только GUI на Linux/macOS (для разработки). Утилиты-бэкенды и Sony SDK рассчитаны на Windows x64.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build/gui" -DCMAKE_BUILD_TYPE=Release ${QT_DIR:+-DCMAKE_PREFIX_PATH="$QT_DIR"}
cmake --build "$ROOT/build/gui" --parallel
echo "Готово: $ROOT/build/gui/PS5CombineAIO"
