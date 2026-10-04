<#
  Собирает переносимые бэкенды в папку tools\ (запускается автоматически из build.ps1).

    tools\mkpfs\mkpfs.exe          <- MkPFS (Python) через PyInstaller --onedir (Python на целевом ПК НЕ нужен)
    tools\fpkg-cli\fpkg-cli.exe    <- PSVIETHOA fpkg-cli (.NET 10, self-contained, single-file)
    tools\sony-sdk\toolchain\...   <- Sony Publishing Tools (копируется как есть из PSVIETHOA)

  Нужно на ПК СБОРКИ (на целевом ПК — ничего): Python 3.9+, .NET 10 SDK, интернет (pip / NuGet).
  Исходники: third_party\MkPFS-main и third_party\PSVIETHOA-FPKG-Builder-main
  (либо -ThirdParty <папка>, например ваша распакованная папка tools).
#>
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$ThirdParty = "",
    [switch]$SkipMkpfs, [switch]$SkipFpkg, [switch]$SkipSonySdk, [switch]$SkipHelper, [switch]$SkipBackpork, [switch]$SkipFself, [switch]$SkipFpkg279, [switch]$SkipFtpUpload
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\common.ps1"
Start-BuildLog $Root

try {
    $cands = @($ThirdParty, "$Root\third_party", "$Root\tools", "$Root\..\tools", "$Root\..") | Where-Object { $_ }
    $Src = $null
    foreach ($c in $cands) {
        if ((Test-Path "$c\MkPFS-main") -and (Test-Path "$c\PSVIETHOA-FPKG-Builder-main")) { $Src = (Resolve-Path $c).Path; break }
    }
    if (-not $Src) { throw "Не найдены папки MkPFS-main и PSVIETHOA-FPKG-Builder-main. Положите их в third_party\ или укажите -ThirdParty." }
    $MkPfs = "$Src\MkPFS-main"
    $Psv = "$Src\PSVIETHOA-FPKG-Builder-main"
    $Work = "$Root\build\tools-work"
    New-Item -ItemType Directory -Force $Out, $Work | Out-Null

    # Точечная правка исходника: ищем точный фрагмент (с учётом CRLF/LF); если исходник другой версии — патч НЕ применяем и пишем в лог.
    function Edit-Source([string]$File, [string]$Old, [string]$New, [string]$What) {
        $raw = [IO.File]::ReadAllText($File)
        $nl = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
        $o = $Old.Replace("`r`n", "`n").Replace("`n", $nl)
        $n = $New.Replace("`r`n", "`n").Replace("`n", $nl)
        if (-not $raw.Contains($o)) { Write-Log "  ! патч не применён («$What»): исходник отличается от ожидаемого — прогресс будет обновляться реже" 'Yellow'; return }
        [IO.File]::WriteAllText($File, $raw.Replace($o, $n), (New-Object System.Text.UTF8Encoding($false)))
        Write-Log "  + патч применён: $What"
    }
    Write-Log "Исходники утилит: $Src" 'Cyan'

    # ---------------- mkpfs ----------------
    if (-not $SkipMkpfs) {
        Write-Log "`n[1/8] mkpfs (PyInstaller)…" 'Cyan'
        $pyExe = $null; $pyArgs = @()
        if (Get-Command py -ErrorAction SilentlyContinue) { $pyExe = 'py'; $pyArgs = @('-3') }
        elseif (Get-Command python -ErrorAction SilentlyContinue) { $pyExe = 'python' }
        else { throw "Не найден Python 3.9+ (py / python). Установите его или запустите с -SkipTools." }

        $venv = "$Work\venv"
        if (-not (Test-Path "$venv\Scripts\python.exe")) { Invoke-Logged $pyExe ($pyArgs + @('-m', 'venv', $venv)) }
        $vpy = "$venv\Scripts\python.exe"
        Invoke-Logged $vpy @('-m', 'pip', 'install', '--upgrade', 'pip', 'pyinstaller')
        Invoke-Logged $vpy @('-m', 'pip', 'install', $MkPfs)

        $entry = "$Work\mkpfs_entry.py"
        # multiprocessing.freeze_support() ОБЯЗАТЕЛЕН для замороженного exe на Windows: MkPFS сжимает блоки пулом процессов
        # (mp.Pool), а каждый рабочий процесс заново запускает mkpfs.exe. Без этого вызова рабочие падают в argparse
        # ("parent_pid=...") и сжатие не работает / зависает.
        Set-Content -Path $entry -Encoding UTF8 -Value @(
            'import multiprocessing',
            'multiprocessing.freeze_support()',
            'from mkpfs.cli import main',
            'if __name__ == "__main__":',
            '    raise SystemExit(main())')
        Invoke-Logged $vpy @('-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'mkpfs',
            '--collect-all', 'zlib_ng', '--collect-all', 'isal', '--collect-submodules', 'mkpfs',
            '--hidden-import', 'zlib_ng.zlib_ng', '--hidden-import', 'isal.isal_zlib',
            '--exclude-module', 'customtkinter', '--exclude-module', 'tkinter', '--exclude-module', 'PIL',
            '--distpath', "$Work\dist", '--workpath', "$Work\pyi", '--specpath', $Work, $entry)
        if (Test-Path "$Out\mkpfs") { Remove-Item -Recurse -Force "$Out\mkpfs" }
        Copy-Item -Recurse "$Work\dist\mkpfs" "$Out\mkpfs"
        Invoke-Logged "$Out\mkpfs\mkpfs.exe" @('-V')    # проверка, что замороженный exe вообще запускается
    }

    # ---------------- fpkg-cli ----------------
    if (-not $SkipFpkg) {
        Write-Log "`n[2/8] fpkg-cli (.NET publish)…" 'Cyan'
        if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw "Не найден dotnet (нужен .NET 10 SDK). Установите его или запустите с -SkipTools." }

        # --- Рабочая копия исходников: оригинал не трогаем; исправляем «болячку» прогресса при перенаправленном выводе ---
        $psvWork = "$Work\psv"
        if (Test-Path $psvWork) { Remove-Item -Recurse -Force $psvWork }
        New-Item -ItemType Directory -Force "$psvWork\libs" | Out-Null
        Get-ChildItem $Psv -File | Where-Object { $_.Extension -in '.props', '.slnx', '.sln', '.json', '.config' } | Copy-Item -Destination $psvWork
        Copy-Item -Recurse "$Psv\src" "$psvWork\src"
        Get-ChildItem $psvWork -Recurse -Directory -Include bin, obj -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item "$Psv\libs\*.dll" "$psvWork\libs" -Force
        $cli = "$psvWork\src\PsViethoa.FpkgBuilder.Cli"
        $oldA = @'
                var step = (int)percent / 10 * 10;
                if (step != _lastPercent)
                {
                    _lastPercent = step;
                    Console.WriteLine(Loc.F("Cli.PkgProgress", Bar(percent), percent, value.DoneFiles, value.TotalFiles, Formatters.Size(value.DoneBytes), Formatters.Size(value.TotalBytes), Formatters.Clock(_elapsed.Elapsed), string.Empty));
                }
'@
        $newA = @'
                // [GUI-fix] Вывод перенаправлен (GUI / лог-файл). Раньше строка печаталась только при смене десятка процентов:
                // на пакете в десятки ГБ это минуты «тишины», и программа выглядит зависшей. Теперь: не чаще раза в секунду
                // и только если процент реально изменился (шаг 0.1 %), плюс финальная строка.
                var tenth = (int)(percent * 10);
                if ((tenth != _lastPercent && _lastDraw.ElapsedMilliseconds >= 1000) || value.DoneFiles >= value.TotalFiles)
                {
                    _lastPercent = tenth;
                    _lastDraw.Restart();
                    Console.WriteLine(Loc.F("Cli.PkgProgress", Bar(percent), percent, value.DoneFiles, value.TotalFiles, Formatters.Size(value.DoneBytes), Formatters.Size(value.TotalBytes), Formatters.Clock(_elapsed.Elapsed), value.CurrentFile ?? string.Empty));
                }
'@
        $oldB1 = @'
    private BuildProgress? _latest;
'@
        $newB1 = @'
    private BuildProgress? _latest;
    private readonly Stopwatch _lastRedirectedPrint = Stopwatch.StartNew();   // [GUI-fix]
'@
        $oldB2 = @'
        else if (p.IsComplete || p.PhasePercent % 10 == 0)
        {
            Console.WriteLine(text);
        }
'@
        $newB2 = @'
        else if (p.IsComplete || _lastRedirectedPrint.ElapsedMilliseconds >= 1000)
        {
            // [GUI-fix] PhasePercent — double, условие "% 10 == 0" почти никогда не срабатывало: при запуске из GUI прогресс
            // сборки не печатался вовсе. Теперь — раз в секунду.
            Console.WriteLine(text);
            _lastRedirectedPrint.Restart();
        }
'@
        Edit-Source "$cli\PackageCommands.cs" $oldA $newA 'прогресс pkg-extract (раз в секунду вместо раза в 10 %)'
        Edit-Source "$cli\CommandLine.cs" $oldB1 $newB1 'поле таймера прогресса сборки'
        Edit-Source "$cli\CommandLine.cs" $oldB2 $newB2 'прогресс сборки FPKG (раз в секунду вместо "% 10 == 0")'
        $dst = "$Out\fpkg-cli"
        Invoke-Logged dotnet @('publish', "$psvWork\src\PsViethoa.FpkgBuilder.Cli\PsViethoa.FpkgBuilder.Cli.csproj",
            '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
            '-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:DebugType=None', '-o', $dst)
        if (Test-Path "$Psv\libs\libScePubTools.dll") { Copy-Item "$Psv\libs\libScePubTools.dll" $dst -Force }
    }

    # ---------------- Sony toolchain ----------------
    if (-not $SkipSonySdk) {
        Write-Log "`n[3/8] Sony Publishing Tools…" 'Cyan'
        $sdk = "$Psv\libs\sony-sdk"
        if (Test-Path "$sdk\toolchain\prospero-pub-cmd.exe") {
            New-Item -ItemType Directory -Force "$Out\sony-sdk" | Out-Null
            Copy-Item -Path "$sdk\*" -Destination "$Out\sony-sdk" -Recurse -Force
        } else {
            Write-Log "Sony toolchain не найден в $sdk — сборка через Sony SDK будет недоступна (останется встроенный движок)." 'Yellow'
        }
    }


    # ---------------- ps5aio-helper (на базе библиотек PS5 PKG Tool) ----------------
    # Даёт Библиотеке метаданные из образов/пакетов, трофеи, Activities, файлы внутри образов, ремонт exFAT, AMPR, конвертацию.
    # Необязательный: без него программа работает, но эти функции скрыты.
    if (-not $SkipHelper) {
        Write-Log "`n[4/8] ps5aio-helper (PS5 PKG Tool)…" 'Cyan'
        $pkgToolSrc = $null
        foreach ($c in @($Src, "$Root\third_party", "$Root\..\tools", "$Root\..")) {
            if (-not $c) { continue }
            $hit = Get-ChildItem $c -Directory -Filter 'PS5PKGTool*' -ErrorAction SilentlyContinue |
                Where-Object { Test-Path "$($_.FullName)\PS5PKGTool.Core\PS5PKGTool.Core.csproj" } | Select-Object -First 1
            if ($hit) { $pkgToolSrc = $hit.FullName; break }
        }
        if (-not $pkgToolSrc) {
            Write-Log "  ! Исходники PS5 PKG Tool не найдены (папка PS5PKGTool-1.2.0 в third_party) — помощник пропущен: Библиотека останется на базовом режиме." 'Yellow'
        }
        elseif (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
            Write-Log "  ! dotnet не найден — помощник пропущен." 'Yellow'
        }
        else {
            Write-Log "  PS5 PKG Tool: $pkgToolSrc"
            # Рабочая копия только нужного (оригинал не трогаем, без bin/obj и без 35 МБ Magick.NET).
            $tw = "$Work\ps5tool"
            if (Test-Path $tw) { Remove-Item -Recurse -Force $tw }
            New-Item -ItemType Directory -Force "$tw\PS5PKGTool\ThirdParty" | Out-Null
            foreach ($d in 'PS5PKGTool.Core', 'PS5PKGTool.Ffpfsc', 'PS5PKGTool.Ufs2') { Copy-Item -Recurse "$pkgToolSrc\$d" "$tw\$d" }
            foreach ($d in 'ProsperoPkgTool', 'Oodle') { Copy-Item -Recurse "$pkgToolSrc\PS5PKGTool\ThirdParty\$d" "$tw\PS5PKGTool\ThirdParty\$d" }
            Get-ChildItem $tw -Recurse -Directory -Include bin, obj -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
            Get-ChildItem $pkgToolSrc -File | Where-Object { $_.Extension -eq '.props' } | Copy-Item -Destination $tw

            $dstHelper = "$Out\ps5aio"
            Invoke-Logged dotnet @('publish', "$Root\helper\Ps5AioHelper\Ps5AioHelper.csproj",
                '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true',
                '-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:DebugType=None',
                "-p:Ps5PkgToolRoot=$tw", '-o', $dstHelper)
        }
    }


    # ---------------- backpork-cli (Auto-Backpork, автор Nazky) ----------------
    # Необязательный. Нужна папка Auto-Backpork* (в ней Backport.py и src\) в third_party. Скрипты Auto-Backpork — только
    # преобразование ELF/SELF (понижение версии SDK, fake-подпись); системные библиотеки fakelib в них НЕ входят —
    # это файлы Sony, их пользователь берёт со своей консоли/своих игр. У проекта нет файла лицензии, поэтому собранный
    # backpork-cli.exe не публикуйте в составе своего релиза без разрешения автора.
    if (-not $SkipBackpork) {
        Write-Log "`n[5/8] backpork-cli (Auto-Backpork)…" 'Cyan'
        $bpSrc = $null
        foreach ($c in @($Src, "$Root\third_party", "$Root\..\tools", "$Root\..")) {
            if (-not $c) { continue }
            foreach ($d in @(Get-ChildItem $c -Directory -Filter 'Auto-Backpork*' -ErrorAction SilentlyContinue)) {
                $cands = @($d.FullName) + @(Get-ChildItem $d.FullName -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
                foreach ($cand in $cands) {
                    if ((Test-Path "$cand\Backport.py") -and (Test-Path "$cand\src\make_fself.py")) { $bpSrc = $cand; break }
                }
                if ($bpSrc) { break }
            }
            if ($bpSrc) { break }
        }
        if (-not $bpSrc) {
            Write-Log "  ! Auto-Backpork не найден (папка Auto-Backpork-main в third_party) — страница «Бэкпорт» будет недоступна." 'Yellow'
        }
        else {
            Write-Log "  Auto-Backpork: $bpSrc"
            $pyExe = $null; $pyArgs = @()
            if (Get-Command py -ErrorAction SilentlyContinue) { $pyExe = 'py'; $pyArgs = @('-3') }
            elseif (Get-Command python -ErrorAction SilentlyContinue) { $pyExe = 'python' }
            else { throw "Не найден Python 3.9+ (py / python). Установите его или запустите с -SkipBackpork." }
            $venv = "$Work\venv"
            if (-not (Test-Path "$venv\Scripts\python.exe")) { Invoke-Logged $pyExe ($pyArgs + @('-m', 'venv', $venv)) }
            $vpy = "$venv\Scripts\python.exe"
            Invoke-Logged $vpy @('-m', 'pip', 'install', '--upgrade', 'pip', 'pyinstaller')

            # Рабочая копия только нужного (оригинал не трогаем, без GUI и кэшей).
            $bw = "$Work\backpork-src"
            if (Test-Path $bw) { Remove-Item -Recurse -Force $bw }
            New-Item -ItemType Directory -Force "$bw\src" | Out-Null
            Copy-Item "$bpSrc\Backport.py" $bw
            Copy-Item "$bpSrc\src\*.py" "$bw\src"

            # SDK-пары 11-13: в оригинале таблица заканчивается на 10, а консоли уже на 11-13. Значения выведены из закономерности
            # таблицы (мажорная версия SDK в BCD, нулевой номер сборки — заведомо не выше SDK консоли той же мажорной версии),
            # PS4-версия как у пары 10. На консоли не проверены. Дописываем в КОНЕЦ модуля (формат файла не важен).
            $patcher = "$bw\src\ps5_sdk_version_patcher.py"
            if ([IO.File]::ReadAllText($patcher) -notmatch 'SDK_VERSION_PAIRS\s*=\s*\{') {
                throw "Auto-Backpork изменился: в ps5_sdk_version_patcher.py не найдена таблица SDK_VERSION_PAIRS (патч пар 11-13 не применён)."
            }
            $extra = @(
                '', '',
                '# --- PS5 Combine AIO: SDK pairs 11-13 (the original table ends at 10) ---',
                'SDK_VERSION_PAIRS.update({',
                '    11: (0x11000000, 0x12090001),',
                '    12: (0x12000000, 0x12090001),',
                '    13: (0x13000000, 0x12090001),',
                '})',
                'SDK_VERSION_PAIRS_MAX = 13', '') -join "`n"
            [IO.File]::AppendAllText($patcher, $extra, (New-Object System.Text.UTF8Encoding $false))

            # Вывод по каналу на Windows идёт в кодовой странице системы, а скрипты печатают «•», «⚠», рамки — без UTF-8 падают.
            $bentry = "$Work\backpork_entry.py"
            Set-Content -Path $bentry -Encoding UTF8 -Value @(
                'import sys',
                'for _s in (sys.stdout, sys.stderr):',
                '    try:',
                '        _s.reconfigure(encoding="utf-8", errors="replace")',
                '    except Exception:',
                '        pass',
                'from Backport import run_cli',
                'if __name__ == "__main__":',
                '    run_cli()')
            Invoke-Logged $vpy @('-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'backpork-cli',
                '--paths', $bw, '--collect-submodules', 'src',
                '--exclude-module', 'tkinter', '--exclude-module', 'customtkinter', '--exclude-module', 'PIL',
                '--distpath', "$Work\dist", '--workpath', "$Work\pyi-bp", '--specpath', $Work, $bentry)
            $dstBp = "$Out\backpork"
            if (Test-Path $dstBp) { Remove-Item -Recurse -Force $dstBp }
            Copy-Item -Recurse "$Work\dist\backpork-cli" $dstBp
            Invoke-Logged "$dstBp\backpork-cli.exe" @('--list-sdk-pairs')     # проверка, что замороженный exe запускается
            Write-Log "  ! У Auto-Backpork нет лицензии: не публикуйте tools\backpork в своём релизе без разрешения автора." 'Yellow'
        }
    }


    # ---------------- fself-cli (рекурсивная fake-подпись; make_fself.py из ps5-make-fself-recursive, BSD-3) ----------------
    # Исходники лежат в проекте (bundled\fself), сторонних папок не нужно — нужен только Python.
    if (-not $SkipFself) {
        Write-Log "`n[6/8] fself-cli (PyInstaller)…" 'Cyan'
        if (-not (Test-Path "$Root\bundled\fself\fself_cli.py")) {
            Write-Log "  ! bundled\fself не найден — режим «только fake-подпись» будет недоступен." 'Yellow'
        }
        else {
            $pyExe = $null; $pyArgs = @()
            if (Get-Command py -ErrorAction SilentlyContinue) { $pyExe = 'py'; $pyArgs = @('-3') }
            elseif (Get-Command python -ErrorAction SilentlyContinue) { $pyExe = 'python' }
            else { throw "Не найден Python 3.9+ (py / python). Установите его или запустите с -SkipFself." }
            $venv = "$Work\venv"
            if (-not (Test-Path "$venv\Scripts\python.exe")) { Invoke-Logged $pyExe ($pyArgs + @('-m', 'venv', $venv)) }
            $vpy = "$venv\Scripts\python.exe"
            Invoke-Logged $vpy @('-m', 'pip', 'install', '--upgrade', 'pip', 'pyinstaller')
            Invoke-Logged $vpy @('-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'fself-cli',
                '--paths', "$Root\bundled\fself",
                '--exclude-module', 'tkinter', '--exclude-module', 'PIL',
                '--distpath', "$Work\dist", '--workpath', "$Work\pyi-fself", '--specpath', $Work, "$Root\bundled\fself\fself_cli.py")
            $dstF = "$Out\fself"
            if (Test-Path $dstF) { Remove-Item -Recurse -Force $dstF }
            Copy-Item -Recurse "$Work\dist\fself-cli" $dstF
            Copy-Item "$Root\bundled\fself\LICENSE-ps5mfr.md", "$Root\bundled\fself\SOURCE.txt" $dstF    # лицензия BSD-3 обязана идти вместе с бинарником
            Invoke-Logged "$dstF\fself-cli.exe" @('--help')
        }
    }


    # ---------------- fpkg279 (необязательный альтернативный движок сборки PKG: SDK 2.79 plaintext) ----------------
    # Не компилируется: это уже готовые бинарники (prospero-pub-cmd.exe / libScePubTools.dll, как и sony-sdk) плюс
    # PowerShell/Python-скрипты. Просто копируется из third_party, если пользователь его туда положил.
    if (-not $SkipFpkg279) {
        Write-Log "`n[7/8] fpkg279 (SDK 2.79 plaintext toolkit)…" 'Cyan'
        $fp279Src = $null
        foreach ($c in @($Src, "$Root\third_party", "$Root\..\tools", "$Root\..")) {
            if (-not $c) { continue }
            $hit = Get-ChildItem $c -Directory -Filter '*fpkg279*' -ErrorAction SilentlyContinue |
                Where-Object { Test-Path "$($_.FullName)\build-from-folder.ps1" } | Select-Object -First 1
            if ($hit) { $fp279Src = $hit.FullName; break }
        }
        if (-not $fp279Src) {
            Write-Log "  ! fpkg279 не найден (третья сторона необязательна) — доп. движок сборки PKG (AC-пакеты, патч+remastered, PlayGo) будет недоступен." 'Yellow'
        }
        else {
            Write-Log "  fpkg279: $fp279Src"
            $dstFp = "$Out\fpkg279"
            if (Test-Path $dstFp) { Remove-Item -Recurse -Force $dstFp }
            Copy-Item -Recurse $fp279Src $dstFp
        }
    }


    # ---------------- ftpupload-cli (автозагрузка на PS5 по FTP; только Python stdlib) ----------------
    if (-not $SkipFtpUpload) {
        Write-Log "`n[8/8] ftpupload-cli (PyInstaller)…" 'Cyan'
        if (-not (Test-Path "$Root\bundled\ftpupload\ftpupload_cli.py")) {
            Write-Log "  ! bundled\ftpupload не найден — автозагрузка по FTP будет недоступна." 'Yellow'
        }
        else {
            $pyExe = $null; $pyArgs = @()
            if (Get-Command py -ErrorAction SilentlyContinue) { $pyExe = 'py'; $pyArgs = @('-3') }
            elseif (Get-Command python -ErrorAction SilentlyContinue) { $pyExe = 'python' }
            else { throw "Не найден Python 3.9+ (py / python). Установите его или запустите с -SkipFtpUpload." }
            $venv = "$Work\venv"
            if (-not (Test-Path "$venv\Scripts\python.exe")) { Invoke-Logged $pyExe ($pyArgs + @('-m', 'venv', $venv)) }
            $vpy = "$venv\Scripts\python.exe"
            Invoke-Logged $vpy @('-m', 'pip', 'install', '--upgrade', 'pip', 'pyinstaller')
            Invoke-Logged $vpy @('-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'ftpupload-cli',
                '--exclude-module', 'tkinter', '--exclude-module', 'PIL',
                '--distpath', "$Work\dist", '--workpath', "$Work\pyi-ftpupload", '--specpath', $Work, "$Root\bundled\ftpupload\ftpupload_cli.py")
            $dstFu = "$Out\ftpupload"
            if (Test-Path $dstFu) { Remove-Item -Recurse -Force $dstFu }
            Copy-Item -Recurse "$Work\dist\ftpupload-cli" $dstFu
            Invoke-Logged "$dstFu\ftpupload-cli.exe" @('--help')
        }
    }

    Set-Content -Path "$Out\README.txt" -Encoding UTF8 -Value @"
Эта папка содержит бэкенды PS5 Combine AIO. Структура должна сохраняться:
  mkpfs\mkpfs.exe
  fpkg-cli\fpkg-cli.exe (+ libScePubTools.dll)
  sony-sdk\toolchain\prospero-pub-cmd.exe
  ps5aio\ps5aio-helper.exe (+ oo2core_9_win64.dll)
  backpork\backpork-cli.exe (optional, Auto-Backpork)
  fself\fself-cli.exe (fake-signing only, BSD-3 make_fself.py)
  fpkg279\build-from-folder.ps1 (optional, SDK 2.79 plaintext toolkit)
  ftpupload\ftpupload-cli.exe (auto-upload to PS5 over FTP)
This folder holds the PS5 Combine AIO backends. Keep the layout above.
"@
    Write-Log "`nУтилиты готовы: $Out" 'Green'
}
catch {
    Write-Log "`nОШИБКА (build-tools): $($_.Exception.Message)" 'Red'
    Write-Log "Полный лог: $(Get-BuildLogPath)" 'Yellow'
    exit 1
}
