<#
  Полная сборка PS5 Combine AIO в переносимую папку + ZIP (Windows x64).

  Примеры:
    scripts\build.ps1 -QtDir C:\Qt\6.8.3\llvm-mingw_64
    scripts\build.ps1 -QtDir C:\Qt\6.8.3\mingw_64
    scripts\build.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64 -ThirdParty D:\my\tools
    scripts\build.ps1 -QtDir ... -SkipTools      # только GUI (утилиты добавите сами)

  Компилятор подбирается АВТОМАТИЧЕСКИ под тип сборки Qt (llvm-mingw -> clang, mingw -> gcc из Qt\Tools, msvc -> Visual Studio).
  Смешивать нельзя: Qt llvm-mingw + gcc = ошибки линковки (std::function, __p___argc).
  Лог каждой сборки: logs\build-ГГГГММДД-ЧЧММСС.log
#>
param(
    [string]$QtDir = $env:QT_DIR,
    [string]$Generator = "",
    [string]$ThirdParty = "",
    [switch]$SkipTools, [switch]$NoZip, [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\common.ps1"
Start-BuildLog $Root

try {
    $Name = "PS5_Combine_AIO"
    $Version = (Select-String -Path "$Root\CMakeLists.txt" -Pattern 'VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1).Matches.Groups[1].Value
    $Build = "$Root\build\gui"
    $Dist = "$Root\dist\${Name}_v${Version}_win64"
    Write-Log "Лог: $(Get-BuildLogPath)" 'DarkGray'

    # ---------- поиск Qt ----------
    if (-not $QtDir) {
        $QtDir = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue | Where-Object Name -match '^6\.' |
            Sort-Object Name -Descending | ForEach-Object { Get-ChildItem $_.FullName -Directory | Where-Object Name -match 'msvc.*64|mingw.*64' } |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $QtDir -or -not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "Qt не найден. Укажите -QtDir C:\Qt\6.x.x\llvm-mingw_64 (или mingw_64 / msvc2022_64)." }
    $QtDir = (Resolve-Path $QtDir).Path
    $Kit = Split-Path $QtDir -Leaf
    $Tools = Join-Path (Split-Path (Split-Path $QtDir)) 'Tools'
    Write-Log "Qt:  $QtDir   (комплект: $Kit)" 'Cyan'
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        $cm = Get-ChildItem $Tools -Directory -Filter 'CMake*' -ErrorAction SilentlyContinue | ForEach-Object { "$($_.FullName)\bin" } | Where-Object { Test-Path "$_\cmake.exe" } | Select-Object -First 1
        if ($cm) { $env:PATH = "$cm;$env:PATH" } else { throw "Не найден cmake (ни в PATH, ни в $Tools)." }
    }

    # ---------- подбор компилятора под комплект Qt ----------
    function Find-ToolDir([string]$Filter) {
        Get-ChildItem $Tools -Directory -Filter $Filter -ErrorAction SilentlyContinue | Sort-Object Name -Descending |
            ForEach-Object { "$($_.FullName)\bin" } | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    $cfg = @("-S", $Root, "-B", $Build, "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$QtDir")
    $CompilerBin = $null
    $Runtime = @()
    $RuntimeDirs = @()

    if ($Kit -match 'llvm-mingw') {
        $CompilerBin = Find-ToolDir 'llvm-mingw*'
        if (-not $CompilerBin) { throw "Qt комплекта llvm-mingw найден, а компилятор — нет: поставьте в Qt Maintenance Tool «Tools → LLVM-MinGW» (папка $Tools\llvm-mingw*)." }
        $cc = "$CompilerBin\clang.exe"; $cxx = "$CompilerBin\clang++.exe"
        $rc = Find-FirstExisting @("$CompilerBin\llvm-windres.exe", "$CompilerBin\x86_64-w64-mingw32-windres.exe", "$CompilerBin\windres.exe")
        $cfg += "-DCMAKE_CXX_COMPILER=$cxx"
        if ($rc) { $cfg += "-DCMAKE_RC_COMPILER=$rc" }
        $Runtime = @('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll')
        $RuntimeDirs = @($CompilerBin, "$CompilerBin\..\x86_64-w64-mingw32\bin", "$QtDir\bin")
        $Stamp = "llvm-mingw|$cxx"
    }
    elseif ($Kit -match 'mingw') {
        $CompilerBin = Find-ToolDir 'mingw*'          # не совпадает с llvm-mingw*: фильтр привязан к началу имени
        if (-not $CompilerBin) { throw "Qt комплекта mingw найден, а компилятор — нет: поставьте в Qt Maintenance Tool «Tools → MinGW x.y.z 64-bit»." }
        $cc = "$CompilerBin\gcc.exe"; $cxx = "$CompilerBin\g++.exe"
        $cfg += "-DCMAKE_CXX_COMPILER=$cxx"
        $Runtime = @('libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll')
        $RuntimeDirs = @($CompilerBin, "$QtDir\bin")
        $Stamp = "mingw|$cxx"
    }
    else {
        $Stamp = "msvc|default"                        # MSVC: CMake сам найдёт Visual Studio
        # Переносимость: CRT-DLL кладём рядом с exe (app-local), а не vc_redist.x64.exe, который требует установки.
        $Runtime = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll')
        if ($env:VCToolsRedistDir) {
            $RuntimeDirs += Get-ChildItem "$env:VCToolsRedistDir\x64" -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
        }
        foreach ($pf in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
            if (-not $pf) { continue }
            $RuntimeDirs += Get-ChildItem "$pf\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | ForEach-Object { $_.FullName }
        }
        $RuntimeDirs += "$env:SystemRoot\System32"
    }

    if (-not $Generator -and $Kit -match 'mingw') {
        $ninja = Find-FirstExisting @(
            (Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source),
            "$Tools\Ninja\ninja.exe")
        if ($ninja) { $Generator = "Ninja"; $cfg += "-DCMAKE_MAKE_PROGRAM=$ninja" }
        else {
            $make = Find-FirstExisting @((Get-ChildItem $Tools -Directory -Filter 'mingw*' -ErrorAction SilentlyContinue | ForEach-Object { "$($_.FullName)\bin\mingw32-make.exe" }))
            if (-not $make) { throw "Не найден ни ninja, ни mingw32-make." }
            $Generator = "MinGW Makefiles"; $cfg += "-DCMAKE_MAKE_PROGRAM=$make"
        }
    }
    if ($Generator) { $cfg += @("-G", $Generator) }
    Write-Log "Компилятор: $(if ($CompilerBin) { $CompilerBin } else { 'Visual Studio (авто)' })   Генератор: $(if ($Generator) { $Generator } else { 'по умолчанию' })" 'Cyan'

    # ---------- GUI ----------
    # Кэш CMake помнит прежний компилятор: при смене комплекта Qt/компилятора папку сборки нужно пересоздать.
    $stampFile = "$Build\.kit-stamp"
    $oldStamp = if (Test-Path $stampFile) { (Get-Content $stampFile -Raw).Trim() } else { $null }
    if ($Clean -or ($oldStamp -and $oldStamp -ne $Stamp) -or ((Test-Path "$Build\CMakeCache.txt") -and -not $oldStamp)) {
        Write-Log "Очистка папки сборки (смена компилятора/комплекта Qt)…" 'Yellow'
        Remove-Item -Recurse -Force $Build -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force $Build | Out-Null
    Set-Content -Path $stampFile -Value $Stamp -Encoding ASCII

    # CMake записывает значения -D в свои .cmake-файлы; обратные слэши там — escape-символы (\Q -> ошибка). Нужны прямые.
    $cfg = @($cfg | ForEach-Object { if ($_ -like '-D*') { $_ -replace '\\', '/' } else { $_ } })
    Invoke-Logged cmake $cfg
    Invoke-Logged cmake @("--build", $Build, "--config", "Release", "--parallel")

    $exe = Get-ChildItem $Build -Recurse -Filter "PS5CombineAIO.exe" | Select-Object -First 1
    if (-not $exe) { throw "PS5CombineAIO.exe не найден после сборки." }

    # ---------- функции развёртывания ----------
    # Ручное развёртывание Qt (запасной путь, если windeployqt не справился): DLL Qt + минимальный набор плагинов.
    function Deploy-QtManual([string]$QtDir, [string]$Dist) {
        Write-Log "Ручное развёртывание Qt из $QtDir" 'Yellow'
        foreach ($m in 'Qt6Core', 'Qt6Gui', 'Qt6Widgets') {
            $f = "$QtDir\bin\$m.dll"
            if (-not (Test-Path $f)) { throw "Не найден $f" }
            Copy-Item $f $Dist -Force
        }
        $plugins = [ordered]@{
            'platforms'    = @('qwindows.dll')
            'styles'       = @('qmodernwindowsstyle.dll')
            'imageformats' = @('qjpeg.dll', 'qgif.dll', 'qico.dll')
        }
        foreach ($dir in $plugins.Keys) {
            foreach ($p in $plugins[$dir]) {
                $src = "$QtDir\plugins\$dir\$p"
                if (Test-Path $src) {
                    New-Item -ItemType Directory -Force "$Dist\$dir" | Out-Null
                    Copy-Item $src "$Dist\$dir" -Force
                    Write-Log "  + плагин: $dir\$p"
                } elseif ($dir -eq 'platforms') { throw "Не найден обязательный плагин $src — переустановите Qt (компонент $Kit)." }
                else { Write-Log "  ! нет плагина $dir\$p (пропущен)" 'Yellow' }
            }
        }
    }

    # Проверка: все DLL, от которых зависят наши exe/dll, лежат рядом или есть в System32 (иначе на чистом ПК не запустится).
    function Test-Dependencies([string]$Dist, [string]$Objdump) {
        if (-not $Objdump) { Write-Log "Проверка зависимостей пропущена (нет objdump для этого комплекта)" 'DarkGray'; return }
        $missing = @{}
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        try {
            foreach ($f in Get-ChildItem $Dist -Recurse -Include '*.exe', '*.dll') {
                $out = & $Objdump -p $f.FullName 2>$null
                foreach ($l in $out) {
                    if ("$l" -match 'DLL Name:\s*(\S+)') {
                        $n = $Matches[1]
                        if ($n -like 'api-ms-win-*' -or $n -like 'ext-ms-*') { continue }
                        if (Get-ChildItem $Dist -Recurse -Filter $n -ErrorAction SilentlyContinue | Select-Object -First 1) { continue }
                        if (Test-Path (Join-Path "$env:SystemRoot\System32" $n)) { continue }
                        $missing[$n] = $f.Name
                    }
                }
            }
        } finally { $ErrorActionPreference = $old }
        if ($missing.Count -eq 0) { Write-Log "Проверка зависимостей: OK (все DLL на месте)" 'Green' }
        else { foreach ($k in $missing.Keys) { Write-Log ("  ! не хватает {0} (нужна для {1})" -f $k, $missing[$k]) 'Yellow' } }
    }

    # ---------- переносимая папка ----------
    if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
    New-Item -ItemType Directory -Force $Dist | Out-Null
    Copy-Item $exe.FullName "$Dist\PS5CombineAIO.exe"

    $oldPath = $env:PATH
    $env:PATH = ((@($CompilerBin, "$QtDir\bin") | Where-Object { $_ }) -join ';') + ";$oldPath"
    # --compiler-runtime windeployqt умеет только для MSVC и gcc; для llvm-mingw рантайм копируется ниже вручную.
    # Лишнее не тащим (приложению не нужны, это ~30-40 МБ): софтверный OpenGL, компиляторы шейдеров,
    # плагины generic/tls/networkinformation (они тянут Qt6Network) и iconengines (тянет Qt6Svg).
    # Рантайм компилятора (CRT / libc++ / libstdc++) копируется ниже вручную — это надёжнее --compiler-runtime.
    $dq = @("--release", "--no-translations", "--no-opengl-sw", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
            "--skip-plugin-types", "generic,networkinformation,tls,iconengines")
    try {
        if ($Kit -match 'llvm-mingw') {
            # На llvm-mingw windeployqt стабильно падает («Unable to find the platform plugin») — сразу идём ручным путём.
            Write-Log "llvm-mingw: Qt разворачивается напрямую (windeployqt для этого комплекта не используется)" 'DarkGray'
        }
        else {
            try { Invoke-Logged "$QtDir\bin\windeployqt.exe" ($dq + "$Dist\PS5CombineAIO.exe") }
            catch { Write-Log "windeployqt не справился: $($_.Exception.Message)" 'Yellow' }
        }
    }
    finally { $env:PATH = $oldPath }

    # windeployqt мог упасть или отработать частично — проверяем результат и при необходимости доделываем вручную
    if (-not (Test-Path "$Dist\Qt6Core.dll") -or -not (Test-Path "$Dist\platforms\qwindows.dll")) {
        Deploy-QtManual $QtDir $Dist
    }
    if (-not (Test-Path "$Dist\platforms\qwindows.dll")) { throw "Не удалось развернуть Qt: нет platforms\qwindows.dll" }

    # страховка: рантайм компилятора должен лежать рядом с exe (на чистом ПК его нет)
    foreach ($dll in $Runtime) {
        if (-not (Test-Path "$Dist\$dll")) {
            $src = Find-FirstExisting @($RuntimeDirs | Where-Object { $_ } | ForEach-Object { Join-Path $_ $dll })
            if ($src) { Copy-Item $src $Dist; Write-Log "  + рантайм: $dll" } else { Write-Log "  ! не найден рантайм $dll (проверьте работу на чистом ПК)" 'Yellow' }
        }
    }
    Copy-Item "$Root\LICENSE", "$Root\README.md", "$Root\CREDITS.md" $Dist
    $objdump = Find-FirstExisting @("$CompilerBin\llvm-objdump.exe", "$CompilerBin\objdump.exe")
    Test-Dependencies $Dist $objdump

    # ---------- утилиты ----------
    if (-not $SkipTools) {
        & "$PSScriptRoot\build-tools.ps1" -Out "$Dist\tools" -ThirdParty $ThirdParty
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { throw "build-tools.ps1 завершился с ошибкой (см. лог)" }
    } else {
        New-Item -ItemType Directory -Force "$Dist\tools" | Out-Null
        Write-Log "Утилиты пропущены: скопируйте mkpfs\, fpkg-cli\, sony-sdk\ в $Dist\tools" 'Yellow'
    }

    # Эмулятор AMPR (drakmor/ampr_emu, GPL-3.0): при упаковке программа может добавить его в папку игры.
    if (Test-Path "$Root\bundled\ampr_emu") {
        New-Item -ItemType Directory -Force "$Dist\tools" | Out-Null
        Copy-Item -Recurse -Force "$Root\bundled\ampr_emu" "$Dist\tools\ampr_emu"
        Write-Log "  + ampr_emu (GPL-3.0) -> tools\\ampr_emu"
    }

    # ---------- ZIP ----------
    if (-not $NoZip) {
        $zip = "$Root\dist\${Name}_v${Version}_win64.zip"
        if (Test-Path $zip) { Remove-Item $zip }
        Compress-Archive -Path "$Dist\*" -DestinationPath $zip
        Write-Log "ZIP: $zip" 'Green'
    }
    Write-Log "`nГотово: $Dist" 'Green'
    Write-Log "Лог:    $(Get-BuildLogPath)" 'DarkGray'
}
catch {
    Write-Log "`nОШИБКА: $($_.Exception.Message)" 'Red'
    if ($_.InvocationInfo) { Write-Log ("  в {0}:{1}" -f $_.InvocationInfo.ScriptName, $_.InvocationInfo.ScriptLineNumber) 'DarkGray' }
    Write-Log "Полный лог: $(Get-BuildLogPath)" 'Yellow'
    exit 1
}
