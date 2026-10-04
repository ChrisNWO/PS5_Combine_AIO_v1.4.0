# Credits / Авторы

## Author of PS5 Combine AIO / Автор программы
**ChrisNWO** — idea, combined C++/Qt application, interface, build scripts.
Идея, объединённое приложение на C++/Qt, интерфейс, скрипты сборки.

## Source projects / Исходные проекты
| Project | Author(s) | Role | License |
|---|---|---|---|
| **MkPFS** (github.com/PSBrew/MkPFS) | PSBrew | PFS / exFAT image engine (`mkpfs`) | GPL-3.0 |
| **PSVIETHOA FPKG Builder** + `fpkg-cli` | Nguyễn Thanh Sơn & Ngô Phi Phương (PSVIETHOA) | FPKG builder application, CLI, exFAT support | not stated in the provided archive — check upstream |
| **LibProsperoPkg** | Drakmor & SvenGDK | FPKG core: PFS v2/v3, Kraken, PlayGo, verification | third-party binary (`LibProsperoPkg.dll`), used unmodified |
| **Sony Publishing Tools** (`prospero-pub-cmd.exe`, `libScePubTools.dll`) | Sony Interactive Entertainment | package creation toolchain | proprietary — **not** covered by this project's license |
| **Qt 6** | The Qt Company | GUI toolkit | LGPL-3.0 |

## Native C++ code derived from the sources / Нативный C++-код по мотивам исходников
- `src/ParamJson.cpp` — reading of `sce_sys/param.json`, modelled on `MetadataReader.cs` (PSVIETHOA) and `game_metadata.py` (MkPFS).

## License / Лицензия
GNU GPL v3 (see `LICENSE`) — required because MkPFS is GPL-3.0 and this program ships and derives from it.

## Modifications to upstream / Изменения исходников при сборке
`scripts/build-tools.ps1` applies three small text patches to a *work copy* of PSVIETHOA `fpkg-cli` (`PackageCommands.cs`, `CommandLine.cs`) — progress-line throttling for redirected output only. Upstream sources on disk are not modified.

## Idea and format sources / Источники идей и форматов (v1.1.0)
- **PS5 PKG Tool** — pearlxcore (GPL-3.0): образец менеджера библиотеки; его библиотеки (`PS5PKGTool.Core/Ffpfsc/Ufs2`) используются в `ps5aio-helper` (метаданные из образов, трофеи, Activities, файлы, ремонт/правка exFAT, конвертация); формат версий прошивки (`FormatSystemVersion`) портирован на C++ в `src/ParamJson.cpp`.
- **PS5-FPKG-Builder** — Phoenixx1202: перечень возможностей (импорт образов, автоочистка временных файлов, Reader).

## ampr_emu — drakmor (GPL-3.0)
Эмулятор AMPR для запуска игр как папки/образа: https://github.com/drakmor/ampr_emu. В `bundled/ampr_emu` лежат немодифицированные
релизные сборки `libSceAmpr.sprx` (0.3.6.6 и 0.4.2.1) с текстом лицензии; перед копированием в папку игры проверяется SHA-256.

## PS5Craft — axotk1k1
https://github.com/axotk1k1/ps5craft — независимая утилита; из неё взяты только наблюдения о причинах сбоев (нужен `freeze_support()` в замороженном mkpfs,
эмулятор AMPR и служебные файлы пакета в `sce_sys`). Её исходный код в PS5 Combine AIO не копировался (в архиве проекта лицензия не указана).

## Бэкпорт (страница «Бэкпорт»)
- **Auto-Backpork** — Nazky (https://github.com/Nazky/Auto-Backpork): оболочка конвейера «расшифровка → понижение SDK → fake-подпись → fakelib». В репозиторий проекта не включён (нет лицензии): пользователь кладёт его в `third_party`, сборка делает `backpork-cli.exe` локально.
- Скрипты внутри Auto-Backpork: **idlesauce** (ps5_elf_sdk_downgrade.py), **john-tornblom** (make_fself.py из ps5-payload-dev/sdk), **BestPig** (BackPork), **zecoxao** (chmod_rec), **EchoStretch** (ps5-app-dumper).
- Системные библиотеки (fakelib) — файлы Sony; ни программа, ни её авторы их не распространяют.

## fself-cli (режим «Только fake-подпись»)
- **ps5-make-fself-recursive** — Alex Free (BSD-3-Clause, https://github.com/alex-free/ps5-make-fself-recursive): `make_fself.py` и идея рекурсивной обработки дампа; текст лицензии лежит в `bundled/fself/LICENSE-ps5mfr.md` и поставляется вместе с `tools\fself`.
- **make_fself.py** — John Törnblom (PS5 Payload SDK, https://github.com/ps5-payload-dev/sdk).
- **ps5-app-dumper** — EchoStretch (https://github.com/EchoStretch/ps5-app-dumper): payload для консоли, снимающий дамп на USB. В проект не включён и не используется при сборке; упомянут как источник дампов.

## Второй движок сборки FPKG (SDK 2.79 plaintext, опционально)
Сторонний набор скриптов и утилит для сборки PKG поверх Sony Publishing Tools (AC-пакеты, патчи с `.remastered.pkg`,
управление PlayGo, авто-выбор `attributePub`/`kernel.addcontMountLevel`). Из него также взяты (и адаптированы в чистую
математику без проприетарного кода) пороги размеров пакета для `PkgSizeEstimate.cpp`, и расширен список служебных
файлов/каталогов пакета в `GameCheck.cpp`. Сами бинарники Sony Publishing Tools в проект не входят — пользователь
кладёт набор в `third_party/` самостоятельно, как и `sony-sdk` встроенного движка.

## Автозагрузка по FTP
`ftpupload-cli` — код, написанный для PS5 Combine AIO с нуля, использует только стандартную библиотеку Python (`ftplib`), сторонних пакетов не задействует.
