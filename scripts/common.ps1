# Общие функции для build.ps1 / build-tools.ps1: лог в файл + запуск внешних команд с захватом вывода.

$script:LogFile = $null

function Start-BuildLog([string]$Root) {
    if ($env:PS5AIO_LOG) { $script:LogFile = $env:PS5AIO_LOG; return }     # дочерний скрипт пишет в тот же файл
    $dir = Join-Path $Root 'logs'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $script:LogFile = Join-Path $dir ("build-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
    $env:PS5AIO_LOG = $script:LogFile
    Set-Content -Path $script:LogFile -Encoding UTF8 -Value ("PS5 Combine AIO build log  {0:s}" -f (Get-Date))
    Add-Content -Path $script:LogFile -Encoding UTF8 -Value ("PowerShell {0} | {1}" -f $PSVersionTable.PSVersion, [Environment]::OSVersion.VersionString)
}

function Get-BuildLogPath { return $script:LogFile }

function Write-Log([string]$Text, [string]$Color = '') {
    if ($Color) { Write-Host $Text -ForegroundColor $Color } else { Write-Host $Text }
    if ($script:LogFile) { Add-Content -Path $script:LogFile -Encoding UTF8 -Value $Text }
}

# Запуск exe: stdout+stderr идут и в консоль, и в лог; ненулевой код выхода -> исключение.
function Invoke-Logged {
    param([Parameter(Mandatory = $true)][string]$Exe, [string[]]$Arguments = @())
    Write-Log ("> {0} {1}" -f $Exe, ($Arguments -join ' ')) 'DarkGray'
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'      # иначе stderr нативной команды в PS 5.1 становится исключением
    $code = 0
    try {
        & $Exe @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) { Write-Log $_.Exception.Message } else { Write-Log ([string]$_) }
        }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($code -ne 0) { throw ("Команда завершилась с кодом {0}: {1}" -f $code, $Exe) }
}

function Find-FirstExisting([string[]]$Paths) {
    foreach ($p in $Paths) { if ($p -and (Test-Path $p)) { return (Resolve-Path $p).Path } }
    return $null
}
