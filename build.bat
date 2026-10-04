@echo off
chcp 65001 >nul
rem build.bat [аргументы build.ps1], например: build.bat -QtDir C:\Qt\6.8.3\llvm-mingw_64
rem Лог каждой сборки: папка logs\ рядом с проектом.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
set RC=%errorlevel%
echo.
if not "%RC%"=="0" (
  echo BUILD FAILED / СБОРКА НЕ УДАЛАСЬ. Логи: %~dp0logs
) else (
  echo OK. Логи: %~dp0logs
)
pause
exit /b %RC%
