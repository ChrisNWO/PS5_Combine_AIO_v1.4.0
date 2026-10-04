# PS5 Combine AIO · by ChrisNWO

**RU** — Портативный «комбайн» для PS5 на C++/Qt 6: библиотека игр с метаданными из образов, упаковка PFS-образов, сборка FPKG, бэкпорт и fake-подпись, обслуживание и сверка образов, автозагрузка на консоль по FTP. Интерфейс на русском и английском (переключается на лету), тёмная и светлая темы, портативная сборка без установки.

**EN** — Portable all-in-one PS5 toolkit in C++/Qt 6: a game library with metadata read straight from images, PFS image packing, FPKG building, backport and fake-signing, image maintenance and comparison, and auto-upload to the console over FTP. Russian and English UI (switchable live), dark and light themes, portable — no installation.

Лицензия / License: **GPL-3.0** (see `LICENSE`). Sony- и другие сторонние компоненты — проприетарные или со своей лицензией, см. раздел «Авторы и использованные проекты» ниже и `CREDITS.md`.

---

## 🇷🇺 Русский

### Что это
PS5 Combine AIO — не новый движок, а оболочка: GUI на C++/Qt 6 вокруг нескольких проверенных инструментов сцены PS5, объединённых в одном окне с общим прогресс-баром, очередью задач и единым интерфейсом на двух языках. Не нужно держать под рукой десяток отдельных утилит и командную строку — всё делается мышкой, в портативной папке, без установки.

### Возможности по страницам

**Библиотека**
- Сканирует папки с дампами, пакетами `.pkg` и образами (`.exfat`, `.ffpfsc`, `.ffpkg`, `.ffpfs`) в фоне.
- Название, Title ID, Content ID, регион, платформа, размер, версия, требуемая прошивка и SDK читаются из `param.json` внутри самого образа/пакета, а не угадываются по имени файла.
- Роли: база, патч, устаревший патч, DLC, приложение; группировка по игре (база + патчи + DLC), региону, формату или прошивке — в виде дерева со сворачиваемыми группами.
- Подробности: обложки (`icon0`, `pic0`–`pic2`), трофеи с иконками и проверкой целостности, Activities и UDS (события, статистика, перечисления, правила), список файлов внутри образа с извлечением выбранного или целой папки в очередь, сведения об исполняемом файле (SELF/ELF, модули), сам `param.json`.
- Названия по всем языкам из `param.json` (с пометкой языка по умолчанию), «реальный минимум прошивки» (по `requiredSystemSoftwareVersion` и версии SDK) и сравнение с прошивкой вашей консоли и пределом вашего загрузчика.
- Поиск дубликатов (по Content ID и версии) — ничего не удаляется автоматически, лишние копии переносятся в Корзину только после вашего подтверждения.
- Переименование по шаблону (`{TITLE} [{TITLE_ID}] [{VERSION}]` и др.) с предпросмотром «сейчас → станет».
- Экспорт списка в CSV и JSON.
- Сверка двух образов: ваш образ против заведомо рабочего эталона — недостающие и лишние файлы, различия размеров и содержимого (побайтово), различия полей `param.json`, положение `eboot.bin`, структура самого exFAT (размер кластера, сектора, флаги тома); понятные выводы и сохранение отчёта в `.txt`.

**Упаковка PFS**
- Режимы: папка → `.ffpfsc` в обёртке exFAT (рекомендуется), папка → чистый PFS, папка → чистый образ exFAT (с выбором размера кластера), файл → `.ffpfsc`.
- Перед упаковкой папки программа проверяет: корень игры (`eboot.bin` и `sce_sys/param.json` должны лежать прямо в выбранной папке), служебные файлы пакета в `sce_sys` (остаются после распаковки FPKG — предлагается перенести в сторону, не удаляя), папку `decrypted` (остаётся после Dump Runner — тоже можно перенести), эмулятор AMPR (если игра использует `libSceAmpr`, а модуля нет — предложит добавить нужную версию), предупреждение про «чистый PFS» со сжатием.
- Уровень сжатия, регистрозависимость, проверка готового образа.

**Сборка FPKG**
- Встроенный движок (на базе PSVIETHOA fpkg-cli): сборка из папки, образа (`.exfat`/`.ffpkg`/`.ffpfsc`) или проекта `.gp5`; можно сохранить исходное требование прошивки или задать версию SDK напрямую.
- Второй, опциональный движок «SDK 2.79 plaintext» (если у вас есть собственные бинарники Sony Publishing Tools): AC-пакеты (дополнительный контент с ключом доступа) и сборка патча вместе с его `.remastered.pkg` за один проход, выбор языков PlayGo.
- Оценка размера пакета до запуска сборки: по несжатому размеру папки игры программа показывает ожидаемый уровень `attributePub` и нужный `kernel.addcontMountLevel`.

**Бэкпорт**
- Понижение версии SDK и fake-подпись исполняемых файлов дампа, чтобы игра с новой прошивки запускалась на консоли со старой. SDK-пары 1–13 (пары выше 10 отмечены как экспериментальные — на консоли не проверялись).
- Режим «только fake-подпись» без смены SDK — для дампа, снятого на консоли той же прошивки; встроен без сторонних файлов.
- Программа не содержит и не ищет системные библиотеки Sony (`fakelib`) — их нужно взять со своей консоли или из своих игр.

**Анализ и распаковка**
- Информация, список файлов, проверка и распаковка пакетов и образов; формат определяется по содержимому, а не по расширению.
- Обслуживание образов: проверка (exFAT / FFPKG / FFPFSC), ремонт exFAT, обновление индекса AMPR, правка файлов в exFAT и FFPKG (заменить / добавить / создать каталог / удалить, транзакционно), пересборка метаданных FFPKG, конвертация между форматами.

**Задачи**
- Очередь запусков, которая сохраняется между перезапусками программы: новую задачу можно поставить, пока выполняется другая; прерванная закрытием программы задача возвращается со статусом «Прервана» и её можно повторить.

**Автозагрузка на PS5 по FTP**
- После упаковки или сборки готовый файл сам уходит на консоль — не нужно вручную открывать FileZilla.
- Настраивается один раз в «Настройках» (IP-адрес, порт, папка на консоли — по умолчанию `/data/homebrew`, анонимный вход или логин/пароль); включено по умолчанию. Папка на консоли создаётся сама, если её ещё нет.
- На страницах «Упаковка PFS» и «Сборка FPKG» есть свой чекбокс, который можно выключить для конкретного запуска.

**Общее**
- Единый прогресс-бар, время выполнения и честное предупреждение, если утилита давно ничего не печатает, вместо молчаливого «зависания».
- Подсказки по типичным ошибкам: retail-пакет, неверный корень игры, нехватка места, занятые файлы.
- Все дочерние процессы корректно завершаются при закрытии программы (в том числе через Job Object Windows — переживает и аварийное закрытие).
- Настройки хранятся в `PS5CombineAIO.ini` рядом с exe — ничего не пишется в реестр.

### Чего нет
- Ремонт exFAT и обновление AMPR — только для exFAT; правка файлов — exFAT и FFPKG; пересборка метаданных — только FFPKG. Для FFPFSC доступны только проверка, конвертация и распаковка.
- Только русский и английский языки интерфейса; проверки обновлений из программы нет — оба пункта сделаны осознанно.
- Движки PFS/exFAT, сборки FPKG и Sony-тулчейн — закрытые или сторонние компоненты, вызываются как внешние утилиты, а не переписаны на C++ (см. «Авторы и использованные проекты»).

### Сборка из исходников (Windows x64)
Нужно на ПК сборки: Qt 6.4+, CMake 3.21+, MSVC или MinGW/LLVM-MinGW; для встроенных утилит — Python 3.9+ и .NET 10 SDK.

```
:: положите исходники в third_party\MkPFS-main и third_party\PSVIETHOA-FPKG-Builder-main
build.bat -QtDir C:\Qt\6.8.3\llvm-mingw_64
```

Список всего, что можно (и нужно) положить в `third_party` — в `third_party\PUT_SOURCES_HERE.txt`: обязательны MkPFS и PSVIETHOA FPKG Builder; `PS5PKGTool-1.2.0` рекомендуется (метаданные образов, трофеи, файлы, ремонт/правка, сверка); `Auto-Backpork-main` и набор SDK 2.79 — опциональны. Результат сборки — `dist\PS5_Combine_AIO_vX.Y.Z_win64\`, портативная папка для копирования на любой ПК с Windows 10/11 x64.

### Авторы и использованные проекты
Полная версия со ссылками и лицензиями — в `CREDITS.md`. Кратко:

| Проект | Автор(ы) | Что используется |
|---|---|---|
| PS5 Combine AIO | **ChrisNWO** | идея, C++/Qt-приложение, интерфейс, скрипты сборки |
| MkPFS | PSBrew | движок упаковки PFS/exFAT-образов (GPL-3.0) |
| PSVIETHOA FPKG Builder / fpkg-cli | Nguyễn Thanh Sơn и Ngô Phi Phương | встроенный движок сборки FPKG |
| LibProsperoPkg | Drakmor и SvenGDK | ядро FPKG (PFS v2/v3, Kraken, PlayGo, проверка) |
| Sony Publishing Tools | Sony Interactive Entertainment | тулчейн сборки пакетов (проприетарный, не под лицензией проекта) |
| Qt 6 | The Qt Company | GUI-библиотека (LGPL-3.0) |
| PS5 PKG Tool | pearlxcore (GPL-3.0) | библиотеки метаданных образов, трофеев, Activities, обслуживания exFAT/UFS2 — используются в `ps5aio-helper` |
| PS5-FPKG-Builder | Phoenixx1202 | источник идей по функциональности |
| ampr_emu | drakmor (GPL-3.0) | эмулятор AMPR |
| PS5Craft | axotk1k1 | источник наблюдений о причинах сбоев (freeze_support, AMPR, служебные файлы пакета); код не копировался |
| Auto-Backpork | Nazky | конвейер бэкпорта (понижение SDK, подпись, fakelib); опционально, кладётся пользователем |
| ps5_elf_sdk_downgrade.py | idlesauce | часть конвейера Auto-Backpork |
| make_fself.py (внутри Auto-Backpork) | John Törnblom (PS5 Payload SDK) | fake-подпись |
| BackPork | BestPig | payload для консоли, монтирующий fakelib |
| chmod_rec | zecoxao | часть конвейера Auto-Backpork |
| ps5-app-dumper | EchoStretch | payload для снятия дампа (источник дампов, в сборку не входит) |
| ps5-make-fself-recursive | Alex Free (BSD-3-Clause) | основа встроенного режима «только fake-подпись» |
| Второй движок сборки FPKG (SDK 2.79 plaintext) | сторонний набор, third_party | AC-пакеты, патч+`.remastered.pkg`, пороги размера пакета |

Отдельно: `ftpupload-cli` (автозагрузка по FTP) написан для этого проекта с нуля, использует только стандартную библиотеку Python (`ftplib`).

---

## 🇬🇧 English

### What it is
PS5 Combine AIO is not a new engine — it's a shell: a C++/Qt 6 GUI around several proven tools from the PS5 scene, brought together in one window with a shared progress bar, task queue, and a single interface in two languages. No need to keep a dozen separate utilities and a command line on hand — everything is done with the mouse, in a portable folder, no installation required.

### Features by page

**Library**
- Scans folders with dumps, `.pkg` packages and images (`.exfat`, `.ffpfsc`, `.ffpkg`, `.ffpfs`) in the background.
- Title, Title ID, Content ID, region, platform, size, version, required firmware and SDK are read from `param.json` inside the image/package itself, not guessed from the file name.
- Roles: base, update, outdated update, DLC, app; grouping by game (base + updates + DLC), region, format or firmware, shown as a collapsible tree.
- Details: artwork (`icon0`, `pic0`–`pic2`), trophies with icons and integrity check, Activities and UDS (events, stats, enum groups, rules), a file list inside the image with extraction of a selection or a whole folder queued as a task, executable info (SELF/ELF, modules), and `param.json` itself.
- Titles in every language found in `param.json` (default language marked), the "effective minimum firmware" (from `requiredSystemSoftwareVersion` and the SDK version), compared against your console's firmware and your loader's limit.
- Duplicate finder (by Content ID and version) — nothing is deleted automatically; extra copies go to the Recycle Bin only after you confirm.
- Rename by template (`{TITLE} [{TITLE_ID}] [{VERSION}]`, etc.) with a "now → will be" preview.
- Export the list to CSV and JSON.
- Compare two images: your image against a known-good reference — missing and extra files, size and byte-level content differences, `param.json` field differences, the `eboot.bin` location, the exFAT structure itself (cluster size, sector size, volume flags); plain-language findings and a saved `.txt` report.

**PFS packing**
- Modes: folder → `.ffpfsc` in an exFAT wrapper (recommended), folder → plain PFS, folder → plain exFAT image (with a cluster-size choice), file → `.ffpfsc`.
- Before packing a folder the program checks: the game root (`eboot.bin` and `sce_sys/param.json` must sit directly in the chosen folder), package-only files in `sce_sys` (left over after FPKG extraction — offered to be moved aside, not deleted), a `decrypted` folder (left by Dump Runner — can be moved aside too), the AMPR emulator (if the game uses `libSceAmpr` and the module is missing, offers to add the right build), and a warning about "plain PFS" with compression.
- Compression level, case sensitivity, verification of the finished image.

**FPKG building**
- Built-in engine (based on PSVIETHOA fpkg-cli): build from a folder, an image (`.exfat`/`.ffpkg`/`.ffpfsc`) or a `.gp5` project; keep the original required firmware or set an SDK version directly.
- A second, optional "SDK 2.79 plaintext" engine (if you have your own Sony Publishing Tools binaries): AC packages (add-on content with an entitlement key) and building a patch together with its `.remastered.pkg` in one pass, plus PlayGo language selection.
- A package-size estimate before building: from the uncompressed size of the game folder, the program shows the expected `attributePub` level and the required `kernel.addcontMountLevel`.

**Backport**
- Lowers the SDK version and fake-signs a dump's executables so a game from newer firmware can run on older firmware. SDK pairs 1–13 (pairs above 10 are marked experimental — not verified on a console).
- A "fake-sign only" mode with no SDK change — for a dump taken on a console of the same firmware; built in, no third-party files needed.
- The program does not contain or look for Sony's system libraries (`fakelib`) — take them from your own console or games.

**Analysis and extraction**
- Info, file list, verification and extraction for packages and images; the format is detected from its contents, not its extension.
- Image maintenance: verify (exFAT / FFPKG / FFPFSC), repair exFAT, refresh the AMPR index, edit files in exFAT and FFPKG (replace / add / create directory / delete, transactional), rebuild FFPKG metadata, convert between formats.

**Tasks**
- A run queue that survives restarts: a new task can be queued while another is running; a task interrupted by closing the program comes back marked "Interrupted" and can be retried.

**Auto-upload to PS5 over FTP**
- After packing or building, the finished file is sent to the console automatically — no need to open FileZilla by hand.
- Set up once in Settings (console IP, port, folder on the console — `/data/homebrew` by default, anonymous login or username/password); enabled by default. The folder on the console is created automatically if it doesn't exist.
- The "PFS packing" and "FPKG building" pages each have their own checkbox to skip it for a single run.

**General**
- One progress bar, elapsed time, and an honest warning if a tool has been silent for a while, instead of a silent freeze.
- Hints for common errors: a retail package, the wrong game root, low disk space, locked files.
- All child processes are properly terminated when the program closes (including via a Windows Job Object — survives a crash too).
- Settings live in `PS5CombineAIO.ini` next to the exe — nothing is written to the registry.

### What's missing
- exFAT repair and AMPR refresh are exFAT-only; file editing covers exFAT and FFPKG; metadata rebuilding is FFPKG-only. FFPFSC only supports verify, convert and extract.
- Only Russian and English in the interface; no in-app update check — both by design.
- The PFS/exFAT engines, the FPKG build engines and the Sony toolchain are closed-source or third-party components, called as external tools rather than rewritten in C++ (see "Credits" above).

### Building from source (Windows x64)
Needed on the build machine: Qt 6.4+, CMake 3.21+, MSVC or MinGW/LLVM-MinGW; for the bundled utilities, Python 3.9+ and the .NET 10 SDK.

```
:: put the sources into third_party\MkPFS-main and third_party\PSVIETHOA-FPKG-Builder-main
build.bat -QtDir C:\Qt\6.8.3\llvm-mingw_64
```

See `third_party\PUT_SOURCES_HERE.txt` for the full list of what can (and should) go into `third_party`: MkPFS and PSVIETHOA FPKG Builder are required; `PS5PKGTool-1.2.0` is recommended (image metadata, trophies, files, repair/edit, comparison); `Auto-Backpork-main` and the SDK 2.79 kit are optional. The build output is `dist\PS5_Combine_AIO_vX.Y.Z_win64\`, a portable folder you can copy to any Windows 10/11 x64 PC.

### Credits
See `CREDITS.md` for the full version with links and licenses. In short, see the table in the Russian section above — the same credits apply; everyone who contributed a component, library, or idea is listed there, including **ChrisNWO** as the author of PS5 Combine AIO itself.

---

*README generated for the GitHub repository / README подготовлен для репозитория на GitHub.*
